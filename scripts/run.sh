#!/usr/bin/env bash
# =============================================================================
#  Black OS - Phase 7 harness equivalent: automated QEMU boot + interaction
#
#  Usage:
#     ./scripts/run.sh              # VGA window (SDL/GTK), serial -> file
#     ./scripts/run.sh --nographic  # headless: serial console on stdio
#     ./scripts/run.sh --debug      # + gdbstub on tcp::1234
#
#  Flags passed to this script are forwarded to qemu-system-x86_64.
# =============================================================================
set -euo pipefail

cd "$(dirname "$0")/.."

ISO=${ISO:-blackos.iso}
MEM=${MEM:-512}
LOG=${LOG:-build/serial.log}
DISPLAY_MODE="${1:-}"
shift || true

QEMU=qemu-system-x86_64
command -v "$QEMU" >/dev/null 2>&1 || {
    echo "error: qemu-system-x86_64 not installed" >&2
    exit 1
}
[ -f "$ISO" ] || {
    echo "error: $ISO missing - run 'make iso' first" >&2
    exit 1
}

# KVM acceleration when /dev/kvm exists (Linux host); TCG otherwise.
EXTRA=()
if [ -c /dev/kvm ] && [ -w /dev/kvm ]; then
    EXTRA+=(-enable-kvm -cpu host)
    echo "[run ] KVM acceleration enabled"
else
    EXTRA+=(-cpu max)
    echo "[run ] KVM unavailable - using TCG emulation"
fi

mkdir -p build

if [ "$DISPLAY_MODE" = "--nographic" ]; then
    exec "$QEMU" "${EXTRA[@]}" \
        -m "$MEM" \
        -cdrom "$ISO" \
        -display none -serial mon:stdio \
        "$@"
elif [ "$DISPLAY_MODE" = "--debug" ]; then
    exec "$QEMU" "${EXTRA[@]}" \
        -m "$MEM" \
        -cdrom "$ISO" \
        -serial file:"$LOG" \
        -gdb tcp::1234 \
        -monitor stdio \
        "$@"
else
    exec "$QEMU" "${EXTRA[@]}" \
        -m "$MEM" \
        -cdrom "$ISO" \
        -serial file:"$LOG" \
        -monitor stdio \
        "$@"
fi
