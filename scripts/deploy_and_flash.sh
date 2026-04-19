#!/bin/bash
# ============================================================
# deploy_and_flash.sh
# ------------------------------------------------------------
# One-shot build → deploy userspace ELFs onto FAT32 → flash
# MLO + kernel onto the raw sectors.
#
# Usage: ./deploy_and_flash.sh /dev/sda
#   (argument is the SD card block device, not the partition)
# ============================================================

set -e

DEVICE=${1:-/dev/sda}
PART="${DEVICE}1"
MOUNT=/media/$USER/VINIX

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
TOPDIR="$(dirname "$SCRIPT_DIR")"

echo "========================================"
echo " 1/3  Build kernel + userspace"
echo "========================================"
make -C "$TOPDIR/VinixOS/userspace"
make -C "$TOPDIR/VinixOS/kernel"

echo ""
echo "========================================"
echo " 2/3  Deploy ELFs onto $MOUNT"
echo "========================================"
if [ ! -d "$MOUNT" ]; then
    echo "SD not mounted at $MOUNT — mount it then retry."
    exit 1
fi

APPS="shell ls cat echo ps kill pwd free uname hello rm mv"
for app in $APPS; do
    dest=$app
    [ "$app" = "shell" ] && dest=sh
    src="$TOPDIR/VinixOS/userspace/build/apps/$app/$app.elf"
    [ -f "$src" ] || { echo "missing $src"; exit 1; }
    cp "$src" "$MOUNT/$dest"
    echo "  $(printf '%-6s' $app) -> $MOUNT/$dest  ($(stat -c%s "$src") bytes)"
done
sync

echo ""
echo "========================================"
echo " 3/3  Flash MLO + kernel.bin to $DEVICE"
echo "========================================"
# Need to unmount the FAT32 partition before raw-writing the card.
if mountpoint -q "$MOUNT"; then
    udisksctl unmount -b "$PART" || sudo umount "$PART"
fi
"$SCRIPT_DIR/flash_sdcard.sh" "$DEVICE"

echo ""
echo "Eject the card and boot the BBB."
