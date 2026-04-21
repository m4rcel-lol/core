#!/usr/bin/env bash
# =============================================================================
# CORE ISO Build Framework  —  tools/build-iso.sh
# =============================================================================
#
# Builds a bootable, hybrid BIOS+UEFI ISO image of the CORE kernel.
#
# Usage:
#   ./tools/build-iso.sh [OPTIONS]
#
# Options:
#   --arch=x86_64|arm64     Target architecture               (default: x86_64)
#   --variant=debug|release|all
#                           Build variant                      (default: release)
#   --output=DIR            Output directory                   (default: dist/)
#   --version=VER           Override version string
#   --staging=DIR           initrd staging tree               (default: staging/)
#   --initrd-compress=gz|xz|none
#                           initrd compression                 (default: none)
#   --jobs=N                Parallel make jobs                 (default: nproc)
#   --serial                Enable GRUB serial console in cfg
#   --no-kernel-build       Skip kernel compilation (use existing ELF)
#   --no-initrd             Skip initrd generation
#   --uefi                  Embed UEFI GRUB2 stub (requires grub-efi-amd64-bin)
#   --cmdline=STR           Extra kernel command-line args
#   --sign                  Sign the ISO with gpg (requires GPG_KEY_ID env var)
#   --quiet                 Suppress verbose output
#   --dry-run               Print steps without executing
#   --help                  Show this help and exit
#
# Environment variables:
#   GPG_KEY_ID              GPG key fingerprint for --sign
#   GRUB_MKRESCUE           Override grub-mkrescue binary
#   XORRISO                 Override xorriso binary
#
# Output files (in OUTPUT_DIR/):
#   core-VERSION-ARCH-VARIANT.iso   Bootable hybrid ISO
#   core-VERSION-ARCH-VARIANT.iso.sha256   SHA-256 checksum
#   core-VERSION-ARCH-VARIANT.iso.md5      MD5 checksum
#   core-VERSION-ARCH-VARIANT.json         Build manifest (JSON)
#   build-VERSION-VARIANT.log              Full build log
#
# =============================================================================
set -euo pipefail

# ── Colour codes ──────────────────────────────────────────────────────────────
if [ -t 1 ] && command -v tput >/dev/null 2>&1; then
    RED=$(tput setaf 1); GREEN=$(tput setaf 2); YELLOW=$(tput setaf 3)
    BLUE=$(tput setaf 4); CYAN=$(tput setaf 6); BOLD=$(tput bold)
    RESET=$(tput sgr0)
else
    RED="" GREEN="" YELLOW="" BLUE="" CYAN="" BOLD="" RESET=""
fi

# ── Logging helpers ───────────────────────────────────────────────────────────
QUIET=0
DRY_RUN=0
LOG_FILE=""

log()  { [ "$QUIET" -eq 0 ] && echo "${BLUE}[build]${RESET} $*"; }
ok()   { echo "${GREEN}[  ok ]${RESET} $*"; }
warn() { echo "${YELLOW}[ warn]${RESET} $*" >&2; }
err()  { echo "${RED}[error]${RESET} $*" >&2; }
step() { echo "${BOLD}${CYAN}==> $*${RESET}"; }
die()  { err "$*"; exit 1; }

run() {
    if [ "$DRY_RUN" -eq 1 ]; then
        echo "${YELLOW}[dry]${RESET} $*"
    else
        if [ -n "$LOG_FILE" ]; then
            mkdir -p "$(dirname "$LOG_FILE")"
            "$@" 2>&1 | tee -a "$LOG_FILE"
        else
            "$@"
        fi
    fi
}

# ── Defaults ──────────────────────────────────────────────────────────────────
ARCH="x86_64"
VARIANT="release"
OUTPUT_DIR="dist"
VERSION=""
STAGING_DIR="staging"
INITRD_COMPRESS="none"
JOBS="$(nproc 2>/dev/null || echo 4)"
SERIAL=0
SKIP_KERNEL_BUILD=0
SKIP_INITRD=0
UEFI=0
CMDLINE=""
SIGN=0

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"

# ── Argument parsing ──────────────────────────────────────────────────────────
for arg in "$@"; do
    case "$arg" in
        --arch=*)              ARCH="${arg#*=}" ;;
        --variant=*)           VARIANT="${arg#*=}" ;;
        --output=*)            OUTPUT_DIR="${arg#*=}" ;;
        --version=*)           VERSION="${arg#*=}" ;;
        --staging=*)           STAGING_DIR="${arg#*=}" ;;
        --initrd-compress=*)   INITRD_COMPRESS="${arg#*=}" ;;
        --jobs=*)              JOBS="${arg#*=}" ;;
        --serial)              SERIAL=1 ;;
        --no-kernel-build)     SKIP_KERNEL_BUILD=1 ;;
        --no-initrd)           SKIP_INITRD=1 ;;
        --uefi)                UEFI=1 ;;
        --cmdline=*)           CMDLINE="${arg#*=}" ;;
        --sign)                SIGN=1 ;;
        --quiet)               QUIET=1 ;;
        --dry-run)             DRY_RUN=1 ;;
        --help|-h)
            sed -n '/^# Usage/,/^# ====/p' "$0" | sed 's/^# \?//'
            exit 0
            ;;
        *) die "Unknown argument: $arg" ;;
    esac
done

# ── Validate ──────────────────────────────────────────────────────────────────
case "$ARCH" in
    x86_64|arm64) ;;
    *) die "Unsupported architecture: $ARCH (use x86_64 or arm64)" ;;
esac
case "$VARIANT" in
    debug|release|all) ;;
    *) die "Unknown variant: $VARIANT (use debug, release, or all)" ;;
esac
case "$INITRD_COMPRESS" in
    gz|xz|none) ;;
    *) die "Unknown initrd compression: $INITRD_COMPRESS (use gz, xz, or none)" ;;
esac

if [ "$SKIP_INITRD" -eq 0 ] && [ "$INITRD_COMPRESS" != "none" ]; then
    warn "Compressed initrd modules are not bootable yet; using raw cpio instead"
    INITRD_COMPRESS="none"
fi

# ── Version detection ─────────────────────────────────────────────────────────
detect_version() {
    if [ -n "$VERSION" ]; then
        echo "$VERSION"
        return
    fi
    # Try git describe (e.g. v0.3.0-12-gabcdef)
    if command -v git >/dev/null 2>&1 && git -C "$REPO_DIR" rev-parse --git-dir >/dev/null 2>&1; then
        local v
        v="$(git -C "$REPO_DIR" describe --tags --always --dirty='+dirty' 2>/dev/null || true)"
        [ -n "$v" ] && { echo "$v"; return; }
    fi
    # Try VERSION file
    if [ -f "$REPO_DIR/VERSION" ]; then
        cat "$REPO_DIR/VERSION"
        return
    fi
    echo "dev-$(date +%Y%m%d)"
}

VERSION="$(detect_version)"
BUILD_DATE="$(date -u +%Y-%m-%dT%H:%M:%SZ)"
BUILD_DATE_SHORT="$(date -u +%Y-%m-%d)"

# ── Paths ─────────────────────────────────────────────────────────────────────
[ "${OUTPUT_DIR:0:1}" != "/" ] && OUTPUT_DIR="$REPO_DIR/$OUTPUT_DIR"
[ "${STAGING_DIR:0:1}" != "/" ] && STAGING_DIR="$REPO_DIR/$STAGING_DIR"

if [ "$ARCH" = "arm64" ]; then
    KERNEL_ELF="$REPO_DIR/core-arm64.elf"
    KERNEL_BIN="$REPO_DIR/core-arm64.bin"
    KERNEL_BASE="core-arm64"
else
    KERNEL_ELF="$REPO_DIR/core.elf"
    KERNEL_BIN="$REPO_DIR/core.bin"
    KERNEL_BASE="core"
fi

ISO_NAME="core-${VERSION}-${ARCH}-${VARIANT}.iso"
ISO_PATH="$OUTPUT_DIR/$ISO_NAME"
LOG_FILE="$OUTPUT_DIR/build-${VERSION}-${VARIANT}.log"
MANIFEST_PATH="$OUTPUT_DIR/core-${VERSION}-${ARCH}-${VARIANT}.json"

GRUB_MKRESCUE="${GRUB_MKRESCUE:-grub-mkrescue}"
XORRISO="${XORRISO:-xorriso}"

# ── Dependency check ──────────────────────────────────────────────────────────
check_deps() {
    local missing=0
    local deps=("make" "$GRUB_MKRESCUE" "$XORRISO")
    [ "$INITRD_COMPRESS" = "gz"   ] && deps+=("gzip")
    [ "$INITRD_COMPRESS" = "xz"   ] && deps+=("xz")
    [ "$SKIP_INITRD"      -eq 0   ] && deps+=("find" "cpio")
    [ "$SIGN"             -eq 1   ] && deps+=("gpg")
    [ "$UEFI"             -eq 1   ] && deps+=("grub-mkimage")

    for cmd in "${deps[@]}"; do
        if ! command -v "$cmd" >/dev/null 2>&1; then
            warn "Missing dependency: $cmd"
            missing=$((missing + 1))
        fi
    done
    [ "$missing" -gt 0 ] && die "Install missing tools and retry."
}

# ── Build variants ────────────────────────────────────────────────────────────
# If --variant=all, recurse for debug and release
if [ "$VARIANT" = "all" ]; then
    step "Building all variants for $ARCH"
    for v in release debug; do
        log "--- variant: $v ---"
        run bash "$0" \
            --arch="$ARCH" \
            --variant="$v" \
            --output="$OUTPUT_DIR" \
            --version="$VERSION" \
            --staging="$STAGING_DIR" \
            --initrd-compress="$INITRD_COMPRESS" \
            --jobs="$JOBS" \
            $( [ "$SERIAL"            -eq 1 ] && echo "--serial" ) \
            $( [ "$SKIP_KERNEL_BUILD" -eq 1 ] && echo "--no-kernel-build" ) \
            $( [ "$SKIP_INITRD"       -eq 1 ] && echo "--no-initrd" ) \
            $( [ "$UEFI"              -eq 1 ] && echo "--uefi" ) \
            $( [ "$SIGN"              -eq 1 ] && echo "--sign" ) \
            $( [ "$QUIET"             -eq 1 ] && echo "--quiet" ) \
            $( [ -n "$CMDLINE"              ] && echo "--cmdline=$CMDLINE" )
    done
    ok "All variants built."
    exit 0
fi

# ── Setup ─────────────────────────────────────────────────────────────────────
mkdir -p "$OUTPUT_DIR"
: > "$LOG_FILE"

echo "==============================================================="  | tee -a "$LOG_FILE"
echo " CORE ISO Build Framework"                                          | tee -a "$LOG_FILE"
echo " Version  : $VERSION"                                              | tee -a "$LOG_FILE"
echo " Arch     : $ARCH"                                                 | tee -a "$LOG_FILE"
echo " Variant  : $VARIANT"                                              | tee -a "$LOG_FILE"
echo " Date     : $BUILD_DATE"                                           | tee -a "$LOG_FILE"
echo " Output   : $ISO_PATH"                                             | tee -a "$LOG_FILE"
echo "==============================================================="  | tee -a "$LOG_FILE"

step "Checking dependencies..."
check_deps

# ── Step 1: Build kernel ──────────────────────────────────────────────────────
if [ "$SKIP_KERNEL_BUILD" -eq 0 ]; then
    step "Building kernel ($ARCH, $VARIANT)..."
    CFLAGS_EXTRA=""
    if [ "$VARIANT" = "debug" ]; then
        CFLAGS_EXTRA="-g -DDEBUG"
    fi
    run make -C "$REPO_DIR" -j"$JOBS" ARCH="$ARCH" \
        EXTRA_CFLAGS="$CFLAGS_EXTRA" all 2>&1 | tee -a "$LOG_FILE"
    ok "Kernel built: $KERNEL_ELF"
else
    log "Skipping kernel build (--no-kernel-build)"
    [ -f "$KERNEL_ELF" ] || die "Kernel ELF not found: $KERNEL_ELF"
fi

KERNEL_SIZE="$(stat -c%s "$KERNEL_ELF" 2>/dev/null || stat -f%z "$KERNEL_ELF")"

# ── Step 2: Build initrd ──────────────────────────────────────────────────────
INITRD_PATH=""
INITRD_SIZE=0

if [ "$SKIP_INITRD" -eq 0 ] && [ -d "$STAGING_DIR" ]; then
    step "Building initrd from $STAGING_DIR..."
    run bash "$SCRIPT_DIR/mkinitrd.sh" \
        --staging="$STAGING_DIR" \
        --output="$OUTPUT_DIR/initrd.cpio" \
        --compress="$INITRD_COMPRESS"
    case "$INITRD_COMPRESS" in
        gz)   INITRD_PATH="$OUTPUT_DIR/initrd.cpio.gz" ;;
        xz)   INITRD_PATH="$OUTPUT_DIR/initrd.cpio.xz" ;;
        none) INITRD_PATH="$OUTPUT_DIR/initrd.cpio" ;;
    esac
    if [ -f "$INITRD_PATH" ]; then
        INITRD_SIZE="$(stat -c%s "$INITRD_PATH" 2>/dev/null || stat -f%z "$INITRD_PATH")"
        ok "initrd: $INITRD_PATH ($INITRD_SIZE bytes)"
    else
        warn "initrd not found at expected path; continuing without initrd"
        INITRD_PATH=""
    fi
else
    log "Skipping initrd (--no-initrd or staging dir missing)"
fi

# ── Step 3: Assemble ISO staging directory ────────────────────────────────────
step "Assembling ISO staging directory..."
ISO_STAGING="$(mktemp -d /tmp/core-iso-XXXXXX)"
trap 'rm -rf "$ISO_STAGING"' EXIT

run mkdir -p \
    "$ISO_STAGING/boot/grub" \
    "$ISO_STAGING/boot/grub/fonts" \
    "$ISO_STAGING/.disk"

# Copy kernel
run cp "$KERNEL_ELF" "$ISO_STAGING/boot/${KERNEL_BASE}.elf"

# Copy initrd if present
INITRD_ISO_PATH=""
if [ -n "$INITRD_PATH" ] && [ -f "$INITRD_PATH" ]; then
    INITRD_BASENAME="$(basename "$INITRD_PATH")"
    run cp "$INITRD_PATH" "$ISO_STAGING/boot/$INITRD_BASENAME"
    INITRD_ISO_PATH="/boot/$INITRD_BASENAME"
fi

# ── Step 4: Generate GRUB config ──────────────────────────────────────────────
step "Generating GRUB configuration..."
GRUB_CFG_IN="$REPO_DIR/grub/grub.cfg.in"
GRUB_CFG_OUT="$ISO_STAGING/boot/grub/grub.cfg"

KERNEL_ISO_PATH="/boot/${KERNEL_BASE}.elf"
INITRD_REF="${INITRD_ISO_PATH:-(no initrd)}"
MODULE_LINE=""
if [ -n "$INITRD_ISO_PATH" ]; then
    MODULE_LINE="    module2 ${INITRD_ISO_PATH} initrd"
fi

if [ -f "$GRUB_CFG_IN" ]; then
    sed \
        -e "s|@@VERSION@@|$VERSION|g" \
        -e "s|@@ARCH@@|$ARCH|g" \
        -e "s|@@DATE@@|$BUILD_DATE_SHORT|g" \
        -e "s|@@KERNEL@@|$KERNEL_ISO_PATH|g" \
        -e "s|@@INITRD@@|${INITRD_ISO_PATH:-/boot/no-initrd}|g" \
        -e "s|@@MODULE_LINE@@|$MODULE_LINE|g" \
        -e "s|@@CMDLINE@@|$CMDLINE|g" \
        "$GRUB_CFG_IN" > "$GRUB_CFG_OUT"
else
    # Fallback minimal grub.cfg
    cat > "$GRUB_CFG_OUT" <<GRUBEOF
set default=0
set timeout=5

menuentry "CORE OS $VERSION ($ARCH)" {
    multiboot2 $KERNEL_ISO_PATH $CMDLINE
$([ -n "$INITRD_ISO_PATH" ] && echo "    module2 $INITRD_ISO_PATH initrd")
    boot
}
GRUBEOF
fi

# Enable serial console in GRUB if requested
if [ "$SERIAL" -eq 1 ]; then
    sed -i "s/^# serial /serial /;s/^# terminal_/terminal_/" "$GRUB_CFG_OUT"
fi

ok "GRUB config: $GRUB_CFG_OUT"

# .disk metadata (informational)
cat > "$ISO_STAGING/.disk/info" <<DISKEOF
CORE OS $VERSION — $ARCH — built $BUILD_DATE_SHORT
DISKEOF
cat > "$ISO_STAGING/.disk/cd_type" <<CDEOF
full_cd
CDEOF

# ── Step 5: UEFI GRUB stub (optional) ─────────────────────────────────────────
if [ "$UEFI" -eq 1 ] && [ "$ARCH" = "x86_64" ]; then
    step "Embedding UEFI GRUB stub..."
    run mkdir -p "$ISO_STAGING/EFI/BOOT"
    # Build GRUB EFI image embedding our grub.cfg
    if command -v grub-mkimage >/dev/null 2>&1; then
        run grub-mkimage \
            -O x86_64-efi \
            -o "$ISO_STAGING/EFI/BOOT/BOOTX64.EFI" \
            -p /boot/grub \
            -c "$GRUB_CFG_OUT" \
            part_gpt part_msdos iso9660 fat ext2 \
            multiboot2 linux normal search echo ls \
            gfxterm videoinfo video \
            || warn "grub-mkimage failed; UEFI boot may not work"
        ok "UEFI stub: $ISO_STAGING/EFI/BOOT/BOOTX64.EFI"
    else
        warn "grub-mkimage not found; UEFI stub not embedded"
    fi
fi

# ── Step 6: Build ISO ─────────────────────────────────────────────────────────
step "Building ISO: $ISO_NAME..."

GRUB_OPTS=(
    --output="$ISO_PATH"
    --compress=gz
)

# xorriso options for BIOS+UEFI hybrid
if [ "$UEFI" -eq 1 ] && [ -f "$ISO_STAGING/EFI/BOOT/BOOTX64.EFI" ]; then
    GRUB_OPTS+=(
        -- -volid "CORE-OS"
           -appid "CORE OS $VERSION"
           -publisher "CORE Project"
           -preparer "build-iso.sh"
           -input-charset UTF-8
    )
fi

run "$GRUB_MKRESCUE" "${GRUB_OPTS[@]}" "$ISO_STAGING" 2>&1 | tee -a "$LOG_FILE" \
    || die "grub-mkrescue failed — is xorriso installed?"

ISO_SIZE="$(stat -c%s "$ISO_PATH" 2>/dev/null || stat -f%z "$ISO_PATH")"
ok "ISO: $ISO_PATH ($ISO_SIZE bytes)"

# ── Step 7: Checksums ─────────────────────────────────────────────────────────
step "Computing checksums..."
(
    cd "$OUTPUT_DIR"
    sha256sum "$ISO_NAME" > "${ISO_NAME}.sha256" && ok "SHA-256: ${ISO_NAME}.sha256"
    md5sum    "$ISO_NAME" > "${ISO_NAME}.md5"    && ok "MD5:    ${ISO_NAME}.md5"
) 2>&1 | tee -a "$LOG_FILE"

# ── Step 8: GPG signature ──────────────────────────────────────────────────────
if [ "$SIGN" -eq 1 ]; then
    step "Signing ISO with GPG..."
    GPG_KEY="${GPG_KEY_ID:-}"
    if [ -z "$GPG_KEY" ]; then
        warn "GPG_KEY_ID not set; using default key"
    fi
    run gpg ${GPG_KEY:+--default-key "$GPG_KEY"} \
        --detach-sign --armor \
        --output "$ISO_PATH.asc" \
        "$ISO_PATH" 2>&1 | tee -a "$LOG_FILE" \
        && ok "Signature: ${ISO_NAME}.asc"
fi

# ── Step 9: Build manifest ────────────────────────────────────────────────────
step "Writing build manifest..."
INITRD_SECTION=""
if [ -n "$INITRD_PATH" ] && [ -f "$INITRD_PATH" ]; then
    INITRD_SECTION=",
    \"initrd\": {
      \"path\":        \"${INITRD_ISO_PATH}\",
      \"size_bytes\":  ${INITRD_SIZE},
      \"compression\": \"${INITRD_COMPRESS}\"
    }"
fi

cat > "$MANIFEST_PATH" <<JSONEOF
{
  "schema_version": "1",
  "project":        "CORE OS",
  "version":        "${VERSION}",
  "arch":           "${ARCH}",
  "variant":        "${VARIANT}",
  "build_date":     "${BUILD_DATE}",
  "kernel": {
    "elf":        "${KERNEL_ISO_PATH}",
    "size_bytes": ${KERNEL_SIZE},
    "cmdline":    "${CMDLINE}"
  }${INITRD_SECTION},
  "iso": {
    "filename":   "${ISO_NAME}",
    "size_bytes": ${ISO_SIZE},
    "sha256":     "$(awk '{print $1}' "$OUTPUT_DIR/${ISO_NAME}.sha256" 2>/dev/null || echo unknown)",
    "md5":        "$(awk '{print $1}' "$OUTPUT_DIR/${ISO_NAME}.md5"    2>/dev/null || echo unknown)"
  },
  "tools": {
    "grub_mkrescue": "$("$GRUB_MKRESCUE" --version 2>&1 | head -1 || echo unknown)",
    "xorriso":       "$("$XORRISO" --version 2>&1 | head -1 || echo unknown)"
  }
}
JSONEOF
ok "Manifest: $MANIFEST_PATH"

# ── Done ──────────────────────────────────────────────────────────────────────
echo ""
echo "${BOLD}${GREEN}Build complete!${RESET}"
echo ""
printf "  %-14s %s\n" "ISO:"       "$ISO_PATH"
printf "  %-14s %s bytes\n" "Size:"      "$ISO_SIZE"
printf "  %-14s %s\n" "SHA-256:"   "$(awk '{print $1}' "$OUTPUT_DIR/${ISO_NAME}.sha256" 2>/dev/null)"
printf "  %-14s %s\n" "Manifest:"  "$MANIFEST_PATH"
printf "  %-14s %s\n" "Build log:" "$LOG_FILE"
echo ""
echo "Test with QEMU:"
if [ "$ARCH" = "x86_64" ]; then
    echo "  qemu-system-x86_64 -cdrom $ISO_PATH -m 64M -serial stdio -display none -no-reboot"
else
    echo "  qemu-system-aarch64 -M virt -cpu cortex-a57 -cdrom $ISO_PATH -m 128M -serial stdio"
fi
echo ""
