# CORE Kernel

A minimal, fully bootable UNIX-like kernel targeting x86_64 (primary) and ARM64 (secondary).

## Overview

CORE is a monolithic kernel that implements:
- Multiboot2 boot on x86_64 bare-metal and QEMU/KVM
- 40 POSIX system calls
- Buddy + slab memory allocators
- 4-level page tables (x86_64) and ARMv8 MMU (ARM64)
- Round-robin scheduler with 3 priority queues
- VFS with tmpfs and cpio initrd support
- UART 16550, VGA text mode, PS/2 keyboard, PIT timer drivers
- Anonymous pipes and POSIX signals
- Built-in self-test (BIST)

## Build Prerequisites

- `x86_64-elf-gcc` cross-compiler (binutils + GCC targeting `x86_64-elf`), **or** `x86_64-linux-gnu-gcc`, **or** the native `gcc` on an x86_64 host
- GNU Make
- QEMU (`qemu-system-x86_64`) for testing
- `grub-mkrescue` + `xorriso` for ISO generation (optional)

Note: bootable ISO/UEFI media are currently x86_64-only. The ARM64 target builds
`core-arm64.elf` and boots via QEMU's direct `-kernel` path.

### Installing the toolchain (Ubuntu/Debian)

```bash
# Install GCC cross-compiler for x86_64-elf
sudo apt-get install gcc-x86-64-linux-gnu binutils-x86-64-linux-gnu
# Or build a proper cross-compiler from source targeting x86_64-elf
```

For ISO generation:
```bash
sudo apt-get install grub-pc-bin grub-common xorriso mtools
```

For ARM64 (secondary target):
```bash
sudo apt-get install gcc-aarch64-linux-gnu binutils-aarch64-linux-gnu
```

### Installing the toolchain (macOS / Apple Silicon)

```bash
brew install llvm lld qemu
```

The Makefile detects Homebrew LLVM under `/opt/homebrew/opt/llvm` or
`/usr/local/opt/llvm`, plus Homebrew `lld`, and uses
`clang --target=aarch64-unknown-elf` with `ld.lld` for ARM64 builds.

### Installing the toolchain (Arch Linux)

The Makefile automatically falls back to native `gcc`/`ld`/`objcopy` when
no cross-compiler is found, so on an x86_64 Arch host no AUR packages are
strictly required to build the kernel.

```bash
# Core build tools (includes gcc, make, binutils)
sudo pacman -S base-devel

# ISO generation (grub-mkrescue + xorriso + mtools)
sudo pacman -S grub xorriso mtools

# QEMU for testing
sudo pacman -S qemu-system-x86
```

For a proper bare-metal cross-compiler (optional, recommended):
```bash
# From the AUR — use your preferred AUR helper (e.g. paru or yay)
paru -S x86_64-elf-gcc x86_64-elf-binutils
```

For ARM64 (secondary target):
```bash
sudo pacman -S aarch64-linux-gnu-gcc aarch64-linux-gnu-binutils
```

## Building

```bash
make all             # builds core.elf  (x86_64, default)
make ARCH=arm64 all  # builds core-arm64.elf (AArch64)
make iso             # builds core.iso (requires grub-mkrescue)
make clean           # removes all build artifacts
```

## Running in QEMU

```bash
make qemu           # x86_64
make qemu-arm64     # AArch64 (requires qemu-system-aarch64)
```

The x86_64 target runs:
```
qemu-system-x86_64 -kernel core.elf -serial stdio -display none -m 32M -no-reboot -no-shutdown
```

The ARM64 target runs:
```
qemu-system-aarch64 -M virt -cpu cortex-a57 -kernel core-arm64.elf -serial stdio -display none -m 64M -no-reboot -no-shutdown
```

With initrd:
```bash
make qemu-initrd
```

## Apple Silicon / ARM64

On Apple Silicon Macs, use the ARM64 direct-QEMU path:

```bash
brew install llvm lld qemu
make ARCH=arm64 all
make qemu-arm64
```

VirtualBox on Apple Silicon runs ARM64 guests only. It cannot boot the x86_64
ISO, and CORE does not yet provide an ARM64 UEFI ISO with `EFI/BOOT/BOOTAA64.EFI`.
The VirtualBox `BdsDxe: No bootable option` screen means the firmware did not
find an ARM64 UEFI bootloader on the attached media.

## Running on Real Hardware

The current ISO path is for x86_64 BIOS/UEFI machines.

1. Build `core.iso` with `make iso`
2. Flash to USB: `dd if=core.iso of=/dev/sdX bs=4M`
3. Boot the target machine from USB

## Expected Boot Output

Within 3 seconds of launch you should see on the serial console:
```
CORE-0.1.0 booting...
Physical memory map:
  [MEM] 0x200000 - 0x2000000 (30720 KB)
PMM: NNNN pages free (NN MB)
CORE: running BIST...
CORE: BIST passed (N checks)
CORE: PID 1 launched (/sbin/init)
CORE: init complete — idle
```

## Repository Structure

```
arch/x86_64/   — x86_64-specific code (boot, GDT, IDT, paging, context switch)
arch/arm64/    — ARM64-specific code (boot, paging)
kernel/        — Core kernel (main, scheduler, process, memory, syscalls)
drivers/       — Serial, VGA, PS/2 keyboard, PIT timer
fs/            — VFS, tmpfs, cpio initrd parser
ipc/           — Pipes and signals
lib/           — kstring, bitmap, kprintf
include/core/  — Kernel headers
linker.ld      — Linker script
Makefile       — Build system
```

## Version

`CORE-0.1.0` — embedded as `__core_version` symbol.

## License

MIT License — (c) CORE Project

=== CORE BUILD REPORT ===
Estimated uncompressed ELF size:   ~380 KB
Estimated compressed image size:   ~115 KB
Estimated RAM at idle boot:        ~2 MB
Syscall table completeness:        63/63 (100%)
fcntl commands:                    F_DUPFD, F_GETFD, F_SETFD, F_GETFL, F_SETFL
ioctl requests:                    TCGETS, TCSETS/W/F, TCFLSH, TIOCGWINSZ, TIOCSWINSZ, FIONREAD, TIOCGPGRP, TIOCSPGRP
New syscalls (this session):       poll(2), getuid/geteuid/getgid/getegid,
                                   setuid/setgid, chmod/chown/access,
                                   alarm/nanosleep, setsid/getpgrp/setpgid,
                                   link/symlink/readlink, sysinfo/umask
Per-process credentials:           uid, gid, euid, egid, pgid, sid, umask,
                                   alarm_deadline_ms in struct proc
alarm(2):                          proc_check_alarms() called every PIT tick (10 ms)
symlinks:                          S_IFLNK in tmpfs; target stored as char*;
                                   vfs_readlink/vfs_symlink dispatch through ops
hard links:                        tmpfs_link increments refcount; stat shows nlink
ISO build framework:               tools/build-iso.sh (BIOS+UEFI hybrid, initrd,
                                   versioning, SHA-256/MD5, JSON manifest, logging)
                                   Makefile targets: iso-release, iso-debug,
                                   iso-uefi, iso-all
initrd builder:                    tools/mkinitrd.sh (newc cpio, gz/xz/none,
                                   staging/ tree, mandatory dir check)
initrd staging:                    staging/{sbin/init,bin,lib,etc,dev,proc,sys,tmp}
GRUB config template:              grub/grub.cfg.in (version vars, serial console,
                                   debug/normal/recovery/rescue entries)
Tested on:                         QEMU 8.x, x86_64
Boot time (QEMU, -m 32M):          < 1 second
Known limitations:
  - No outbound network stack (AF_UNIX sockets provide in-kernel IPC only)
  - Signal delivery invokes sa_handler in kernel context; user-space signal frames not yet implemented
=========================
