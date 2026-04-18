#!/usr/bin/env bash
# =============================================================================
# CORE mkinitrd — tools/mkinitrd.sh
# =============================================================================
#
# Builds a cpio initrd archive from a staging directory.
#
# Usage:
#   ./tools/mkinitrd.sh [OPTIONS]
#
# Options:
#   --staging=DIR       Source staging tree            (required)
#   --output=FILE       Output cpio file (before ext)  (default: initrd.cpio)
#   --compress=gz|xz|none  Compression                 (default: gz)
#   --owner=UID:GID     Force owner in archive         (default: 0:0)
#   --verbose           Verbose cpio output
#   --help              Show this help and exit
#
# The script:
#   1. Validates the staging tree and creates missing required directories
#   2. Sets correct permissions on well-known entries (sbin/init must be 0755)
#   3. Builds a newc-format cpio archive
#   4. Compresses with the chosen algorithm
#   5. Prints the archive size and a file manifest summary
# =============================================================================
set -euo pipefail

# ── Colour ────────────────────────────────────────────────────────────────────
if [ -t 1 ] && command -v tput >/dev/null 2>&1; then
    GREEN=$(tput setaf 2); YELLOW=$(tput setaf 3)
    BLUE=$(tput setaf 4); BOLD=$(tput bold); RESET=$(tput sgr0)
else
    GREEN="" YELLOW="" BLUE="" BOLD="" RESET=""
fi

log()  { echo "${BLUE}[initrd]${RESET} $*"; }
ok()   { echo "${GREEN}[  ok  ]${RESET} $*"; }
warn() { echo "${YELLOW}[ warn ]${RESET} $*" >&2; }
die()  { echo "ERROR: $*" >&2; exit 1; }

# ── Defaults ──────────────────────────────────────────────────────────────────
STAGING_DIR=""
OUTPUT_FILE="initrd.cpio"
COMPRESS="gz"
OWNER="0:0"
VERBOSE=0

for arg in "$@"; do
    case "$arg" in
        --staging=*)   STAGING_DIR="${arg#*=}" ;;
        --output=*)    OUTPUT_FILE="${arg#*=}" ;;
        --compress=*)  COMPRESS="${arg#*=}" ;;
        --owner=*)     OWNER="${arg#*=}" ;;
        --verbose)     VERBOSE=1 ;;
        --help|-h)
            sed -n '/^# Usage/,/^# ====/p' "$0" | sed 's/^# \?//'
            exit 0
            ;;
        *) die "Unknown argument: $arg" ;;
    esac
done

[ -n "$STAGING_DIR" ] || die "--staging=DIR is required"
[ -d "$STAGING_DIR"  ] || die "Staging directory does not exist: $STAGING_DIR"

case "$COMPRESS" in
    gz|xz|none) ;;
    *) die "Unknown compression: $COMPRESS (use gz, xz, or none)" ;;
esac

# ── Ensure required directories ───────────────────────────────────────────────
for d in sbin bin lib dev proc sys tmp etc var var/log; do
    [ -d "$STAGING_DIR/$d" ] || { log "Creating missing dir: $d"; mkdir -p "$STAGING_DIR/$d"; }
done

# ── Ensure /sbin/init exists ──────────────────────────────────────────────────
if [ ! -f "$STAGING_DIR/sbin/init" ]; then
    warn "/sbin/init not found — creating minimal placeholder"
    cat > "$STAGING_DIR/sbin/init" <<'INIT'
#!/bin/sh
echo "CORE: minimal init (replace staging/sbin/init)"
while true; do sleep 1; done
INIT
fi
chmod 755 "$STAGING_DIR/sbin/init"

# ── Ensure /etc/hostname ──────────────────────────────────────────────────────
[ -f "$STAGING_DIR/etc/hostname" ] || echo "core" > "$STAGING_DIR/etc/hostname"

# ── Build cpio archive ────────────────────────────────────────────────────────
log "Building cpio archive from $STAGING_DIR → $OUTPUT_FILE"

CPIO_OPTS=(-o -H newc)
[ "$VERBOSE" -eq 1 ] && CPIO_OPTS+=(-v)

OWNER_UID="${OWNER%%:*}"
OWNER_GID="${OWNER##*:}"

# Find all files relative to staging dir
(
    cd "$STAGING_DIR"
    find . -mindepth 0 | sort
) | \
    (
        cd "$STAGING_DIR"
        cpio "${CPIO_OPTS[@]}" --owner="${OWNER_UID}:${OWNER_GID}"
    ) > "$OUTPUT_FILE"

RAW_SIZE="$(stat -c%s "$OUTPUT_FILE" 2>/dev/null || stat -f%z "$OUTPUT_FILE")"
ok "cpio archive: $OUTPUT_FILE ($RAW_SIZE bytes, $(( RAW_SIZE / 1024 )) KB)"

# ── Compress ──────────────────────────────────────────────────────────────────
case "$COMPRESS" in
    gz)
        log "Compressing with gzip..."
        gzip -9 -f -k "$OUTPUT_FILE"
        FINAL="${OUTPUT_FILE}.gz"
        ;;
    xz)
        log "Compressing with xz..."
        xz -9 -f -k "$OUTPUT_FILE"
        FINAL="${OUTPUT_FILE}.xz"
        ;;
    none)
        FINAL="$OUTPUT_FILE"
        ;;
esac

if [ "$COMPRESS" != "none" ] && [ -f "$FINAL" ]; then
    COMP_SIZE="$(stat -c%s "$FINAL" 2>/dev/null || stat -f%z "$FINAL")"
    RATIO=$(( RAW_SIZE > 0 ? (RAW_SIZE - COMP_SIZE) * 100 / RAW_SIZE : 0 ))
    ok "Compressed: $FINAL ($COMP_SIZE bytes, ${RATIO}% reduction)"
fi

# ── Print file tree summary ───────────────────────────────────────────────────
ENTRY_COUNT="$(find "$STAGING_DIR" | wc -l)"
log "Staging tree: $ENTRY_COUNT entries"
if [ "$VERBOSE" -eq 1 ]; then
    find "$STAGING_DIR" | sort | sed "s|$STAGING_DIR||" | head -60
fi
