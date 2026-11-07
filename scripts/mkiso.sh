#!/usr/bin/env bash
# =============================================================================
#  Black OS - manual ISO packet writer (fallback if grub-mkrescue fails or a
#  bespoke xorriso pipeline is wanted).  Assumptions like grub-mkrescue:
#    * iso/boot/grub/i386-pc/eltorito.img  (BIOS eltorito, from grub-pc-bin)
#    * iso/boot/grub/grub.cfg
#
#  Main documented path remains:  make iso      (grub-mkrescue, BIOS+UEFI)
# =============================================================================
set -euo pipefail
cd "$(dirname "$0")/.."

ENAME=${ELTORITO:-iso/boot/grub/i386-pc/eltorito.img}
[ -f "$ENAME" ] || {
    echo "error: $ENAME missing." >&2
    echo "hint: install grub-pc-bin then run 'grub-mkrescue -o x.iso iso' once" >&2
    echo "      to materialise the eltorito image, or simply use 'make iso'." >&2
    exit 1
}

xorriso -as mkisofs \
    -R -r -J -joliet-long -V "BLACK_OS" \
    -b boot/grub/i386-pc/eltorito.img \
    -no-emul-boot -boot-load-size 4 -boot-info-table \
    -o blackos.iso \
    iso
