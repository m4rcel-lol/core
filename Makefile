# CORE Kernel — (c) CORE Project, MIT License
# Makefile — GNU Make, single top-level, no recursive makes

ARCH    ?= x86_64

ifeq ($(ARCH),arm64)

# ── ARM64 toolchain ──────────────────────────────────────────────────────────
ifneq ($(shell which aarch64-elf-gcc 2>/dev/null),)
CC      := aarch64-elf-gcc
LD      := aarch64-elf-ld
OBJCOPY := aarch64-elf-objcopy
else
CC      := aarch64-linux-gnu-gcc
LD      := aarch64-linux-gnu-ld
OBJCOPY := aarch64-linux-gnu-objcopy
endif
AS      := $(CC)

CFLAGS  := -Os -std=c11 -ffreestanding -fno-builtin -fno-stack-protector \
           -ffunction-sections -fdata-sections -fno-unwind-tables \
           -fno-asynchronous-unwind-tables -fno-exceptions \
           -fomit-frame-pointer \
           -Wall -Wextra -Werror -Iinclude

ASFLAGS := -ffreestanding -Iinclude

LDFLAGS := --gc-sections -T linker-arm64.ld

TARGET  := core-arm64
ELF     := $(TARGET).elf
BIN     := $(TARGET).bin

# ARM64 source files:
#   arch/arm64/platform.c provides all x86_64-specific stubs (GDT, IDT, PIC,
#   VGA, PS/2, SYSCALL MSR, VMM) plus PL011 UART and Generic Timer.
#   arch/arm64/paging.c  provides the ARMv8 4-level page tables (optional).
#   The x86_64 driver files and kernel/syscall.c are excluded.
C_SRCS := \
    arch/arm64/paging.c      \
    arch/arm64/platform.c    \
    kernel/main.c            \
    kernel/sched.c           \
    kernel/proc.c            \
    kernel/elf.c             \
    kernel/mm.c              \
    kernel/vmm.c             \
    kernel/panic.c           \
    kernel/selftest.c        \
    fs/vfs.c                 \
    fs/tmpfs.c               \
    fs/initrd.c              \
    ipc/pipe.c               \
    ipc/signal.c             \
    lib/kstring.c            \
    lib/bitmap.c             \
    lib/kprintf.c

S_SRCS := \
    arch/arm64/boot.S        \
    arch/arm64/sched.S

else

# ── x86_64 toolchain (default) ───────────────────────────────────────────────
# Prefer x86_64-elf-gcc, fall back to x86_64-linux-gnu-gcc, then native gcc
ifneq ($(shell which x86_64-elf-gcc 2>/dev/null),)
CC      := x86_64-elf-gcc
LD      := x86_64-elf-ld
OBJCOPY := x86_64-elf-objcopy
else ifneq ($(shell which x86_64-linux-gnu-gcc 2>/dev/null),)
CC      := x86_64-linux-gnu-gcc
LD      := x86_64-linux-gnu-ld
OBJCOPY := x86_64-linux-gnu-objcopy
else
CC      := gcc
LD      := ld
OBJCOPY := objcopy
endif
AS      := $(CC)

CFLAGS  := -Os -std=c11 -ffreestanding -fno-builtin -fno-stack-protector \
           -ffunction-sections -fdata-sections -fno-unwind-tables \
           -fno-asynchronous-unwind-tables -fno-exceptions \
           -fomit-frame-pointer -mno-red-zone \
           -Wall -Wextra -Werror -Iinclude

ASFLAGS := -ffreestanding -Iinclude

LDFLAGS := --gc-sections -T linker.ld

TARGET  := core
ELF     := $(TARGET).elf
BIN     := $(TARGET).bin
ISO     := $(TARGET).iso

# Source files
C_SRCS := \
    arch/x86_64/gdt.c        \
    arch/x86_64/idt.c        \
    arch/x86_64/paging.c     \
    kernel/main.c            \
    kernel/sched.c           \
    kernel/proc.c            \
    kernel/elf.c             \
    kernel/mm.c              \
    kernel/vmm.c             \
    kernel/syscall.c         \
    kernel/panic.c           \
    kernel/selftest.c        \
    drivers/serial.c         \
    drivers/vga.c            \
    drivers/ps2.c            \
    drivers/timer.c          \
    fs/vfs.c                 \
    fs/tmpfs.c               \
    fs/initrd.c              \
    ipc/pipe.c               \
    ipc/signal.c             \
    lib/kstring.c            \
    lib/bitmap.c             \
    lib/kprintf.c

S_SRCS := \
    arch/x86_64/boot.S       \
    arch/x86_64/sched.S      \
    arch/x86_64/syscall.S

endif  # ARCH

C_OBJS  := $(C_SRCS:.c=.o)
S_OBJS  := $(S_SRCS:.S=.o)
OBJS    := $(S_OBJS) $(C_OBJS)

.PHONY: all clean qemu qemu-initrd qemu-arm64 \
        iso iso-release iso-debug iso-uefi iso-all img

all: $(ELF)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

%.o: %.S
	$(AS) $(ASFLAGS) -c $< -o $@

$(ELF): $(OBJS)
	$(LD) $(LDFLAGS) -o $@ $^

$(BIN): $(ELF)
	$(OBJCOPY) -O binary $< $@

iso: $(ELF)
	mkdir -p isodir/boot/grub
	cp $(ELF) isodir/boot/$(TARGET).elf
	printf 'set timeout=0\nset default=0\n\nmenuentry "CORE" {\n  multiboot2 /boot/$(TARGET).elf\n  boot\n}\n' \
	    > isodir/boot/grub/grub.cfg
	grub-mkrescue -o $(ISO) isodir

# ── Advanced ISO framework (tools/build-iso.sh) ───────────────────────────────
ISO_ARCH     ?= $(ARCH)
ISO_OUTPUT   ?= dist
ISO_STAGING  ?= staging
ISO_COMPRESS ?= gz
ISO_CMDLINE  ?=
ISO_JOBS     ?= $(shell nproc 2>/dev/null || echo 4)

# Standard release ISO (BIOS + optional UEFI, gzip-compressed initrd)
iso-release: all
	bash tools/build-iso.sh \
	    --arch=$(ISO_ARCH) \
	    --variant=release \
	    --output=$(ISO_OUTPUT) \
	    --staging=$(ISO_STAGING) \
	    --initrd-compress=$(ISO_COMPRESS) \
	    --jobs=$(ISO_JOBS) \
	    $(if $(ISO_CMDLINE),--cmdline='$(ISO_CMDLINE)')

# Debug ISO (same as release but with -g and DEBUG defined)
iso-debug: all
	bash tools/build-iso.sh \
	    --arch=$(ISO_ARCH) \
	    --variant=debug \
	    --output=$(ISO_OUTPUT) \
	    --staging=$(ISO_STAGING) \
	    --initrd-compress=$(ISO_COMPRESS) \
	    --jobs=$(ISO_JOBS) \
	    $(if $(ISO_CMDLINE),--cmdline='$(ISO_CMDLINE)')

# UEFI-capable release ISO (embeds grub-mkimage EFI stub)
iso-uefi: all
	bash tools/build-iso.sh \
	    --arch=$(ISO_ARCH) \
	    --variant=release \
	    --output=$(ISO_OUTPUT) \
	    --staging=$(ISO_STAGING) \
	    --initrd-compress=$(ISO_COMPRESS) \
	    --jobs=$(ISO_JOBS) \
	    --uefi \
	    $(if $(ISO_CMDLINE),--cmdline='$(ISO_CMDLINE)')

# Build both debug and release ISOs
iso-all: all
	bash tools/build-iso.sh \
	    --arch=$(ISO_ARCH) \
	    --variant=all \
	    --output=$(ISO_OUTPUT) \
	    --staging=$(ISO_STAGING) \
	    --initrd-compress=$(ISO_COMPRESS) \
	    --jobs=$(ISO_JOBS) \
	    $(if $(ISO_CMDLINE),--cmdline='$(ISO_CMDLINE)')

img: $(BIN)
	dd if=/dev/zero of=$(TARGET).img bs=512 count=2880
	mkdosfs $(TARGET).img
	mcopy -i $(TARGET).img $(BIN) ::kernel.bin

qemu: $(ELF)
	qemu-system-x86_64 \
	    -kernel $(ELF) \
	    -serial stdio \
	    -display none \
	    -m 32M \
	    -no-reboot \
	    -no-shutdown

qemu-initrd: $(ELF) initrd.cpio
	qemu-system-x86_64 \
	    -kernel $(ELF) \
	    -initrd initrd.cpio \
	    -serial stdio \
	    -display none \
	    -m 32M \
	    -no-reboot \
	    -no-shutdown \
	    -append "root=/dev/ram0"

qemu-arm64: ARCH=arm64
qemu-arm64: $(ELF)
	qemu-system-aarch64 \
	    -M virt \
	    -cpu cortex-a57 \
	    -kernel $(ELF) \
	    -serial stdio \
	    -display none \
	    -m 64M \
	    -no-reboot \
	    -no-shutdown

clean:
	rm -f $(OBJS) $(ELF) $(BIN)
	rm -f $(if $(ISO),$(ISO)) core.iso core-arm64.elf core-arm64.bin
	rm -f core.img core-arm64.img
	rm -rf isodir
	rm -rf dist/
