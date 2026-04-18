/* CORE Kernel — (c) CORE Project, MIT License */
#ifndef CORE_ELF_H
#define CORE_ELF_H

#include <core/types.h>

/* ELF64 scalar types */
typedef uint64_t Elf64_Addr;
typedef uint64_t Elf64_Off;
typedef uint16_t Elf64_Half;
typedef uint32_t Elf64_Word;
typedef uint64_t Elf64_Xword;

/* ELF identification array */
#define EI_NIDENT  16
#define EI_MAG0    0
#define EI_MAG1    1
#define EI_MAG2    2
#define EI_MAG3    3
#define EI_CLASS   4
#define EI_DATA    5

/* ELF class (EI_CLASS) */
#define ELFCLASS64  2

/* ELF data encoding (EI_DATA) */
#define ELFDATA2LSB 1   /* little-endian */

/* Object file type (e_type) */
#define ET_EXEC 2
#define ET_DYN  3

/* Machine architecture (e_machine) */
#define EM_X86_64 62

/* ELF64 file header */
typedef struct {
    uint8_t    e_ident[EI_NIDENT];
    Elf64_Half e_type;
    Elf64_Half e_machine;
    Elf64_Word e_version;
    Elf64_Addr e_entry;
    Elf64_Off  e_phoff;
    Elf64_Off  e_shoff;
    Elf64_Word e_flags;
    Elf64_Half e_ehsize;
    Elf64_Half e_phentsize;
    Elf64_Half e_phnum;
    Elf64_Half e_shentsize;
    Elf64_Half e_shnum;
    Elf64_Half e_shstrndx;
} Elf64_Ehdr;

/* Program header type (p_type) */
#define PT_LOAD 1

/* Program header flags (p_flags) */
#define PF_X 0x1   /* execute */
#define PF_W 0x2   /* write */
#define PF_R 0x4   /* read */

/* ELF64 program header */
typedef struct {
    Elf64_Word  p_type;
    Elf64_Word  p_flags;
    Elf64_Off   p_offset;
    Elf64_Addr  p_vaddr;
    Elf64_Addr  p_paddr;
    Elf64_Xword p_filesz;
    Elf64_Xword p_memsz;
    Elf64_Xword p_align;
} Elf64_Phdr;

/*
 * elf_load — load an ELF64 executable from the VFS into a fresh address space.
 *
 * @path:      VFS path to the ELF binary
 * @out_pml4:  set to the new process PML4 (virtual pointer, kernel-mapped)
 * @out_sp:    set to the initial user-space stack pointer (ABI-compliant frame)
 *
 * Returns the ELF entry-point virtual address on success, or 0 on failure.
 */
uint64_t elf_load(const char *path, uint64_t **out_pml4, uint64_t *out_sp);

#endif /* CORE_ELF_H */
