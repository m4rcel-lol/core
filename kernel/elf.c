/* CORE Kernel — (c) CORE Project, MIT License */
#include <core/types.h>
#include <core/elf.h>
#include <core/mm.h>
#include <core/vfs.h>
#include <core/drivers.h>

extern void *memcpy(void *, const void *, size_t);
extern void *memset(void *, int, size_t);
extern int   vmm_map(uint64_t *pml4, uint64_t vaddr, uint64_t paddr, uint64_t flags);
extern uint64_t *vmm_get_kernel_pml4(void);
extern size_t strlen(const char *s);

/* User-space stack configuration */
#define ELF_STACK_PAGES  8U
#define USER_STACK_TOP   0x0000800000000000ULL

/* Sanity cap on program header count */
#define ELF_PHNUM_MAX    64U

/* Compile-time guard: stack must fit below USER_STACK_TOP */
_Static_assert(ELF_STACK_PAGES > 0U, "ELF_STACK_PAGES must be > 0");
_Static_assert((uint64_t)ELF_STACK_PAGES * PAGE_SIZE <= USER_STACK_TOP,
               "ELF stack exceeds USER_STACK_TOP");

/*
 * elf_create_pml4 — allocate a fresh PML4 for a user process.
 * The kernel half (entries 256–511) is shared with the kernel PML4;
 * the user half (entries 0–255) starts empty.
 */
static uint64_t *elf_create_pml4(void) {
    void *phys = pmm_alloc(0);
    if (!phys) return NULL;
    uint64_t *pml4  = (uint64_t *)PHYS_TO_VIRT(phys);
    uint64_t *kpml4 = vmm_get_kernel_pml4();
    for (int i = 0; i < 256; i++) pml4[i] = 0;
    if (kpml4) {
        for (int i = 256; i < 512; i++) pml4[i] = kpml4[i];
    } else {
        for (int i = 256; i < 512; i++) pml4[i] = 0;
    }
    return pml4;
}

/*
 * map_segment — map a single PT_LOAD segment into pml4.
 *
 * Pages in [ALIGN_DOWN(vaddr), ALIGN_UP(vaddr+memsz)) are allocated, zeroed,
 * and the [vaddr, vaddr+filesz) portion is filled from fd at file offset foff.
 * Returns 0 on success, -1 on allocation failure or overflow.
 */
static int map_segment(uint64_t *pml4, uint64_t vaddr, uint64_t memsz,
                       int fd, off_t foff, uint64_t filesz) {
    /* Guard against integer overflow in range calculations */
    if (memsz > UINT64_MAX - vaddr) return -1;
    if (filesz > memsz) return -1;   /* filesz <= memsz by ELF spec */

    uint64_t vstart = ALIGN_DOWN(vaddr, PAGE_SIZE);
    uint64_t vend   = ALIGN_UP(vaddr + memsz, PAGE_SIZE);
    uint64_t flags  = VMM_FLAG_PRESENT | VMM_FLAG_WRITE | VMM_FLAG_USER;

    for (uint64_t va = vstart; va < vend; va += PAGE_SIZE) {
        void *phys = pmm_alloc(0);
        if (!phys) return -1;
        uint8_t *kp = (uint8_t *)PHYS_TO_VIRT(phys);
        memset(kp, 0, PAGE_SIZE);

        /*
         * Determine which bytes of the file data fall within this page.
         * File data occupies virtual range [vaddr, vaddr + filesz).
         * This page covers [va, va + PAGE_SIZE).
         * Intersection: [copy_start, copy_end).
         */
        uint64_t copy_start = (va > vaddr) ? va : vaddr;
        uint64_t file_end   = vaddr + filesz;   /* overflow guarded above */
        uint64_t page_end   = va + PAGE_SIZE;
        uint64_t copy_end   = (page_end < file_end) ? page_end : file_end;

        if (copy_end > copy_start) {
            size_t dst_off  = (size_t)(copy_start - va);
            off_t  src_off  = foff + (off_t)(copy_start - vaddr);
            size_t copy_len = (size_t)(copy_end - copy_start);
            vfs_lseek(fd, src_off, SEEK_SET);
            vfs_read(fd, kp + dst_off, copy_len);
        }

        vmm_map(pml4, va, (uint64_t)phys, flags);
    }
    return 0;
}

/* Maximum number of argv/envp entries we will pass to the new process */
#define ELF_MAX_ARGV 64
#define ELF_MAX_ENVP 128

/* Auxiliary-vector entry types (System V AMD64 ABI) */
#define AT_NULL    0ULL
#define AT_PAGESZ  6ULL

/*
 * elf_build_stack — write the System V AMD64 ABI initial stack frame into the
 * topmost physical page of the user stack, which is mapped at
 *   virtual = [USER_STACK_TOP - PAGE_SIZE, USER_STACK_TOP)
 * i.e. the kernel can access it via PHYS_TO_VIRT(top_page_phys).
 *
 * Layout (from high to low within that one page):
 *
 *   [high]  envp string data (NUL-terminated, packed)
 *           argv string data (NUL-terminated, packed)
 *           padding to 8-byte alignment
 *           AT_NULL  (two uint64_t = 0, 0)
 *           AT_PAGESZ (two uint64_t: 6, PAGE_SIZE)
 *           NULL     (envp[envc] terminator)
 *           envp[envc-1] … envp[0]  (user virtual pointers)
 *           NULL     (argv[argc] terminator)
 *           argv[argc-1] … argv[0]  (user virtual pointers)
 *           argc     (uint64_t)
 *   [rsp]
 *
 * Returns the initial rsp (user virtual address of argc) or 0 on overflow.
 */
static uint64_t elf_build_stack(void *top_page_phys, char *argv[], char *envp[]) {
    uint8_t *kpage = (uint8_t *)PHYS_TO_VIRT(top_page_phys);

    /* Virtual address of the first byte of this page */
    uint64_t page_virt = USER_STACK_TOP - PAGE_SIZE;

    /* Count argc, envc (capped for safety) */
    int argc = 0;
    while (argv && argv[argc] && argc < ELF_MAX_ARGV) argc++;
    int envc = 0;
    while (envp && envp[envc] && envc < ELF_MAX_ENVP) envc++;

    /*
     * Write string data from the top of the page downward.
     * str_off is the byte offset within kpage; it starts at PAGE_SIZE and
     * decreases as we prepend each string.
     */
    size_t str_off = PAGE_SIZE;

    /* User virtual pointers for each string */
    uint64_t argv_va[ELF_MAX_ARGV + 1];
    uint64_t envp_va[ELF_MAX_ENVP + 1];

    for (int i = envc - 1; i >= 0; i--) {
        size_t slen = strlen(envp[i]) + 1;
        if (str_off < slen) return 0;   /* overflow */
        str_off -= slen;
        memcpy(kpage + str_off, envp[i], slen);
        envp_va[i] = page_virt + str_off;
    }

    for (int i = argc - 1; i >= 0; i--) {
        size_t slen = strlen(argv[i]) + 1;
        if (str_off < slen) return 0;
        str_off -= slen;
        memcpy(kpage + str_off, argv[i], slen);
        argv_va[i] = page_virt + str_off;
    }

    /*
     * Align str_off down to 8 bytes — the pointer/word frame below must be
     * 8-byte aligned (and ultimately 16-byte aligned at rsp).
     */
    str_off &= ~(size_t)7;

    /*
     * Words needed below the string area:
     *   argc (1) + argv[0..argc] (argc+1) + envp[0..envc] (envc+1) +
     *   auxv AT_PAGESZ (2) + auxv AT_NULL (2)  =  argc + envc + 7
     */
    size_t nwords  = (size_t)(argc + envc) + 7U;
    size_t nbytes  = nwords * 8U;

    if (str_off < nbytes) return 0;   /* overflow */
    size_t frame_off = str_off - nbytes;

    /* 16-byte align the frame (rsp must be 16-byte aligned at process entry) */
    frame_off &= ~(size_t)15;
    if (frame_off + nbytes > str_off) {
        /* After alignment we might have slightly less room — recalculate */
        if (frame_off < nbytes) return 0;
    }

    uint64_t *f = (uint64_t *)(kpage + frame_off);
    size_t fi = 0;

    f[fi++] = (uint64_t)argc;

    for (int i = 0; i < argc; i++) f[fi++] = argv_va[i];
    f[fi++] = 0ULL;   /* argv[argc] = NULL */

    for (int i = 0; i < envc; i++) f[fi++] = envp_va[i];
    f[fi++] = 0ULL;   /* envp[envc] = NULL */

    /* Auxiliary vector */
    f[fi++] = AT_PAGESZ; f[fi++] = PAGE_SIZE;
    f[fi++] = AT_NULL;   f[fi++] = 0ULL;

    return page_virt + frame_off;   /* initial rsp (points at argc) */
}

uint64_t elf_load(const char *path, char *argv[], char *envp[],
                  uint64_t **out_pml4, uint64_t *out_sp) {
    int fd = vfs_open(path, O_RDONLY, 0);
    if (fd < 0) {
        kprintf("elf: cannot open %s\n", path);
        return 0;
    }

    /* Read and validate ELF64 header */
    Elf64_Ehdr ehdr;
    vfs_lseek(fd, 0, SEEK_SET);
    if (vfs_read(fd, &ehdr, sizeof(ehdr)) != (ssize_t)sizeof(ehdr)) {
        kprintf("elf: short read on header\n");
        vfs_close(fd);
        return 0;
    }

    if (ehdr.e_ident[EI_MAG0] == (uint8_t)'#' &&
        ehdr.e_ident[EI_MAG1] == (uint8_t)'!') {
        kprintf("elf: scripts are not executable yet: %s\n", path);
        vfs_close(fd);
        return 0;
    }

    if (ehdr.e_ident[EI_MAG0] != 0x7fU ||
        ehdr.e_ident[EI_MAG1] != (uint8_t)'E' ||
        ehdr.e_ident[EI_MAG2] != (uint8_t)'L' ||
        ehdr.e_ident[EI_MAG3] != (uint8_t)'F' ||
        ehdr.e_ident[EI_CLASS] != ELFCLASS64    ||
        ehdr.e_ident[EI_DATA]  != ELFDATA2LSB   ||
        ehdr.e_machine         != EM_X86_64     ||
        (ehdr.e_type != ET_EXEC && ehdr.e_type != ET_DYN)) {
        kprintf("elf: not a valid x86-64 ELF64 executable\n");
        vfs_close(fd);
        return 0;
    }

    if (!ehdr.e_entry || !ehdr.e_phnum ||
        ehdr.e_phnum > ELF_PHNUM_MAX    ||
        ehdr.e_phentsize < (Elf64_Half)sizeof(Elf64_Phdr)) {
        kprintf("elf: bad program header configuration\n");
        vfs_close(fd);
        return 0;
    }

    /* Guard against overflow in total program header table size */
    if ((uint64_t)ehdr.e_phnum * ehdr.e_phentsize > UINT64_MAX - ehdr.e_phoff) {
        kprintf("elf: program header table overflows file offset\n");
        vfs_close(fd);
        return 0;
    }

    /* Create a fresh address space for the new process */
    uint64_t *pml4 = elf_create_pml4();
    if (!pml4) {
        kprintf("elf: out of memory for PML4\n");
        vfs_close(fd);
        return 0;
    }

    /* Process PT_LOAD segments */
    for (uint16_t i = 0; i < ehdr.e_phnum; i++) {
        Elf64_Phdr phdr;
        off_t phoff = (off_t)((uint64_t)ehdr.e_phoff +
                               (uint64_t)i * (uint64_t)ehdr.e_phentsize);
        vfs_lseek(fd, phoff, SEEK_SET);
        if (vfs_read(fd, &phdr, sizeof(phdr)) != (ssize_t)sizeof(phdr))
            continue;
        if (phdr.p_type != PT_LOAD || !phdr.p_memsz)
            continue;

        if (map_segment(pml4, phdr.p_vaddr, phdr.p_memsz,
                        fd, (off_t)phdr.p_offset, phdr.p_filesz) < 0) {
            kprintf("elf: map_segment failed\n");
            vfs_close(fd);
            return 0;
        }
    }

    vfs_close(fd);

    /*
     * Allocate the user stack: ELF_STACK_PAGES pages ending at USER_STACK_TOP.
     * Keep the physical address of the topmost page so we can write the
     * initial ABI stack frame through its PHYS_TO_VIRT mapping.
     */
    uint64_t stack_base  = USER_STACK_TOP - (uint64_t)ELF_STACK_PAGES * PAGE_SIZE;
    uint64_t stk_flags   = VMM_FLAG_PRESENT | VMM_FLAG_WRITE | VMM_FLAG_USER;
    void    *top_page_phys = NULL;

    for (uint32_t i = 0; i < ELF_STACK_PAGES; i++) {
        void *phys = pmm_alloc(0);
        if (!phys) {
            kprintf("elf: out of memory for stack\n");
            return 0;
        }
        memset(PHYS_TO_VIRT(phys), 0, PAGE_SIZE);
        vmm_map(pml4, stack_base + (uint64_t)i * PAGE_SIZE, (uint64_t)phys, stk_flags);
        if (i == ELF_STACK_PAGES - 1U) top_page_phys = phys;
    }

    /*
     * Build the System V AMD64 ABI initial stack frame in the topmost page.
     * If argv is NULL (no arguments), synthesise a minimal frame with argc=0.
     */
    static char *empty_argv[] = { NULL };
    static char *empty_envp[] = { NULL };
    char **av = (argv && argv[0]) ? argv : empty_argv;
    char **ev = (envp && envp[0]) ? envp : empty_envp;

    uint64_t sp = elf_build_stack(top_page_phys, av, ev);
    if (!sp) {
        /* Fallback: minimal 4-word frame (argc=0, argv=NULL, envp=NULL, AT_NULL) */
        sp     = USER_STACK_TOP - 4ULL * sizeof(uint64_t);
        size_t sp_off = (size_t)(sp & (PAGE_SIZE - 1ULL));
        uint64_t *frame = (uint64_t *)((uint8_t *)PHYS_TO_VIRT(top_page_phys) + sp_off);
        frame[0] = 0ULL;
        frame[1] = 0ULL;
        frame[2] = 0ULL;
        frame[3] = 0ULL;
    }

    *out_pml4 = pml4;
    *out_sp   = sp;
    return ehdr.e_entry;
}
