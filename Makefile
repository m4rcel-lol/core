# CORE Kernel — (c) CORE Project, MIT License
# Makefile — GNU Make, single top-level, no recursive makes

ARCH    ?= x86_64

# Support both x86_64-elf-gcc (preferred) and x86_64-linux-gnu-gcc (fallback)
ifneq ($(shell which $(ARCH)-elf-gcc 2>/dev/null),)
CC      := $(ARCH)-elf-gcc
LD      := $(ARCH)-elf-ld
OBJCOPY := $(ARCH)-elf-objcopy
else
CC      := $(ARCH)-linux-gnu-gcc
LD      := $(ARCH)-linux-gnu-ld
OBJCOPY := $(ARCH)-linux-gnu-objcopy
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

C_OBJS  := $(C_SRCS:.c=.o)
S_OBJS  := $(S_SRCS:.S=.o)
OBJS    := $(S_OBJS) $(C_OBJS)

.PHONY: all clean qemu iso img

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

clean:
	rm -f $(OBJS) $(ELF) $(BIN) $(ISO) $(TARGET).img
	rm -rf isodir
