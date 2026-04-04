#!/bin/bash
# ============================================================
# wipe_and_flash.sh
# ------------------------------------------------------------
# Build VinixOS, create a complete disk image (.img), then
# write it to SD card with dd.
#
# Uses the same approach as official BeagleBone images:
# pre-built .img file → dd to card. No fdisk/mkfs on user
# machine, so the result is byte-identical on any Linux distro.
#
# Compatible: Ubuntu 20.04 / 22.04 / 24.04 (any Linux with dd)
#
# Usage: sudo ./wipe_and_flash.sh [OPTIONS] /dev/sdX
#
# Options:
#   --skip-build    Skip building, use existing MLO + kernel.bin
#   --img-only      Only create .img file, don't flash to SD card
#   --help          Show this help message
#
# WARNING: This will DESTROY ALL DATA on the target device!
# ============================================================

set -e

# ============================================================
# Color output (disabled if not a terminal)
# ============================================================
if [ -t 1 ]; then
    RED='\033[0;31m'
    GREEN='\033[0;32m'
    YELLOW='\033[1;33m'
    CYAN='\033[0;36m'
    NC='\033[0m'
else
    RED='' GREEN='' YELLOW='' CYAN='' NC=''
fi

# ============================================================
# Resolve paths
# ============================================================
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
TOPDIR="$(dirname "$SCRIPT_DIR")"
MLO="$TOPDIR/VinixOS/bootloader/MLO"
KERNEL="$TOPDIR/VinixOS/kernel/build/kernel.bin"
IMG="$TOPDIR/VinixOS/build/vinixos.img"

# ============================================================
# Parse arguments
# ============================================================
SKIP_BUILD=0
IMG_ONLY=0
DEVICE=""

show_help()
{
    echo "Usage: sudo $0 [OPTIONS] <device>"
    echo ""
    echo "Build VinixOS and flash to SD card for BeagleBone Black (AM335x)."
    echo "Works on any Linux distro — uses dd to write a pre-built disk image."
    echo ""
    echo "Options:"
    echo "  --skip-build    Skip building, use existing MLO + kernel.bin"
    echo "  --img-only      Only create vinixos.img, don't flash to SD card"
    echo "  --help          Show this help message"
    echo ""
    echo "Examples:"
    echo "  sudo $0 /dev/sdb              # Build + flash"
    echo "  sudo $0 --skip-build /dev/sdb # Flash only"
    echo "  sudo $0 --img-only            # Create .img only"
    echo ""
    echo "Find your SD card device with: lsblk"
}

for arg in "$@"; do
    case "$arg" in
        --skip-build) SKIP_BUILD=1 ;;
        --img-only)   IMG_ONLY=1 ;;
        --help|-h)    show_help; exit 0 ;;
        -*)           echo -e "${RED}Error: Unknown option: $arg${NC}"; exit 1 ;;
        *)            DEVICE="$arg" ;;
    esac
done

if [ -z "$DEVICE" ] && [ "$IMG_ONLY" -eq 0 ]; then
    show_help
    exit 1
fi

# ============================================================
# Must run as root
# ============================================================
if [ "$(id -u)" -ne 0 ]; then
    echo -e "${RED}Error: This script must be run as root (sudo).${NC}"
    exit 1
fi

# ============================================================
# Check and auto-install required tools
# ============================================================
MISSING=""
for cmd in dd losetup mkfs.vfat; do
    command -v "$cmd" >/dev/null 2>&1 || MISSING="$MISSING $cmd"
done
if [ "$SKIP_BUILD" -eq 0 ]; then
    for cmd in make arm-none-eabi-gcc python3; do
        command -v "$cmd" >/dev/null 2>&1 || MISSING="$MISSING $cmd"
    done
fi

if [ -n "$MISSING" ]; then
    echo -e "${YELLOW}Missing tools:${MISSING}${NC}"
    echo "Installing dependencies automatically..."
    echo ""
    bash "$TOPDIR/scripts/setup-environment.sh"

    STILL_MISSING=""
    for cmd in dd losetup mkfs.vfat make arm-none-eabi-gcc python3; do
        command -v "$cmd" >/dev/null 2>&1 || STILL_MISSING="$STILL_MISSING $cmd"
    done
    if [ -n "$STILL_MISSING" ]; then
        echo -e "${RED}Error: Still missing after install:${STILL_MISSING}${NC}"
        exit 1
    fi
    echo -e "${GREEN}All dependencies installed.${NC}"
    echo ""
fi

# ============================================================
# Header
# ============================================================
echo -e "${CYAN}========================================${NC}"
echo " VinixOS SD Card Wipe & Flash"
echo -e "${CYAN}========================================${NC}"
echo "Project Root : $TOPDIR"
if [ "$IMG_ONLY" -eq 0 ]; then
    echo "Device       : $DEVICE"
fi
echo ""

# ============================================================
# Build VinixOS (unless --skip-build)
# ============================================================
if [ "$SKIP_BUILD" -eq 0 ]; then
    echo -e "${CYAN}[1/4] Building VinixOS (bootloader + userspace + kernel)...${NC}"
    make -C "$TOPDIR/VinixOS" bootloader || { echo -e "${RED}Bootloader build failed!${NC}"; exit 1; }
    make -C "$TOPDIR/VinixOS" userspace  || { echo -e "${RED}Userspace build failed!${NC}"; exit 1; }
    make -C "$TOPDIR/VinixOS" kernel     || { echo -e "${RED}Kernel build failed!${NC}"; exit 1; }
    echo -e "${GREEN}[1/4] Build complete.${NC}"
    echo ""
else
    echo -e "${YELLOW}[1/4] Build skipped (--skip-build).${NC}"
    echo ""
fi

# Verify build artifacts
if [ ! -f "$MLO" ]; then
    echo -e "${RED}Error: MLO not found at $MLO${NC}"
    exit 1
fi
if [ ! -f "$KERNEL" ]; then
    echo -e "${RED}Error: kernel.bin not found at $KERNEL${NC}"
    exit 1
fi

MLO_SIZE=$(stat -c%s "$MLO")
KERNEL_SIZE=$(stat -c%s "$KERNEL")
echo "MLO    : $MLO_SIZE bytes (TOC + GP header + code)"
echo "Kernel : $KERNEL_SIZE bytes"
echo ""

# ============================================================
# Step 2: Create disk image
# ------------------------------------------------------------
# Build a complete disk image in memory with:
#   - MBR partition table
#   - Kernel at raw sector 2048 (1MB offset)
#   - FAT32 boot partition at sector 4096 (128MB)
#   - MLO as first file on FAT32
#
# This image is identical no matter what Linux distro builds it.
# ============================================================
echo -e "${CYAN}[2/4] Creating disk image (vinixos.img)...${NC}"

mkdir -p "$(dirname "$IMG")"

# Image size: 4096 sectors gap + 128MB FAT32 = ~130MB
# 266240 sectors * 512 bytes = 136,314,880 bytes (~130MB)
IMG_SECTORS=266240
dd if=/dev/zero of="$IMG" bs=512 count=$IMG_SECTORS status=none

# Write MBR partition table
# Partition 1: start=4096, size=262144 sectors (128MB), type=0x0C (FAT32 LBA), bootable
# MBR format: partition entry at offset 446, 16 bytes per entry
python3 -c "
import struct, sys

# Build MBR
mbr = bytearray(512)

# Partition 1 entry at offset 446
# Status: 0x80 (bootable)
# Type: 0x0C (FAT32 LBA)
# Start LBA: 4096
# Size: 262144 sectors
start_lba = 4096
size_sectors = 262144

# CHS values (dummy, LBA is used)
entry = struct.pack('<BBBBBBBBII',
    0x80,       # bootable
    0xFE, 0xFF, 0xFF,  # CHS start (dummy)
    0x0C,       # type: FAT32 LBA
    0xFE, 0xFF, 0xFF,  # CHS end (dummy)
    start_lba,
    size_sectors
)
mbr[446:446+16] = entry

# Boot signature
mbr[510] = 0x55
mbr[511] = 0xAA

sys.stdout.buffer.write(mbr)
" | dd of="$IMG" bs=512 seek=0 conv=notrunc status=none

echo "  MBR written."

# Write MLO at RAW mode offsets (TRM 26.1.8.5.5)
# ROM checks 4 locations: 0x0, 0x20000, 0x40000, 0x60000
# Sector 0 has MBR → ROM skips it (first bytes = 0x00000000)
# Write to remaining 3 offsets for redundancy
dd if="$MLO" of="$IMG" bs=512 seek=256 conv=notrunc status=none   # 0x20000 (128KB)
dd if="$MLO" of="$IMG" bs=512 seek=512 conv=notrunc status=none   # 0x40000 (256KB)
dd if="$MLO" of="$IMG" bs=512 seek=768 conv=notrunc status=none   # 0x60000 (384KB)
echo "  MLO written at sectors 256, 512, 768 (RAW mode x3)."

# Write kernel at sector 2048
dd if="$KERNEL" of="$IMG" bs=512 seek=2048 conv=notrunc status=none
echo "  Kernel written at sector 2048."

# Format FAT32 partition inside image using loop device
LOOP=$(losetup --find --show --offset=$((4096 * 512)) --sizelimit=$((262144 * 512)) "$IMG")
mkfs.vfat -F 32 -s 8 -n "BOOT" "$LOOP" >/dev/null
echo "  FAT32 formatted (4KB clusters)."

# Mount and copy MLO (must be first file — fallback for FAT mode boot)
MOUNTPOINT="/tmp/vinixos_img_$$"
mkdir -p "$MOUNTPOINT"
mount "$LOOP" "$MOUNTPOINT"
cp "$MLO" "$MOUNTPOINT/MLO"
sync
umount "$MOUNTPOINT"
rmdir "$MOUNTPOINT"
losetup -d "$LOOP"
echo "  MLO copied to FAT32 (FAT mode fallback)."

echo -e "${GREEN}[2/4] Image created: $IMG ($(stat -c%s "$IMG") bytes)${NC}"
echo ""

# ============================================================
# --img-only: stop here
# ============================================================
if [ "$IMG_ONLY" -eq 1 ]; then
    echo "Image ready at: $IMG"
    echo ""
    echo "Flash manually with:"
    echo "  sudo dd if=$IMG of=/dev/sdX bs=4M status=progress && sync"
    echo ""
    echo "Done."
    exit 0
fi

# ============================================================
# Validate device
# ============================================================
if [ ! -b "$DEVICE" ]; then
    echo -e "${RED}Error: $DEVICE is not a block device.${NC}"
    exit 1
fi

# Safety: refuse if device has system mounts
SYSTEM_MOUNTS=$(lsblk -no MOUNTPOINT "$DEVICE" 2>/dev/null | grep -E "^/$|^/home$|^/boot$|^/boot/efi$" || true)
if [ -n "$SYSTEM_MOUNTS" ]; then
    echo -e "${RED}Error: $DEVICE contains system mount points!${NC}"
    exit 1
fi

# Warn if not removable
REMOVABLE=$(cat "/sys/block/$(basename "$DEVICE")/removable" 2>/dev/null || echo "")
if [ "$REMOVABLE" != "1" ]; then
    echo -e "${YELLOW}Warning: $DEVICE is not marked as removable.${NC}"
    read -p "Are you sure this is an SD card? (y/N): " RMCHECK
    if [ "$RMCHECK" != "y" ] && [ "$RMCHECK" != "Y" ]; then
        echo "Aborted."; exit 0
    fi
fi

# Show device info
echo -e "${YELLOW}Current device layout:${NC}"
lsblk -o NAME,SIZE,FSTYPE,LABEL,MOUNTPOINT "$DEVICE" 2>/dev/null || true
echo ""
echo -e "${RED}WARNING: ALL DATA on $DEVICE will be DESTROYED!${NC}"
echo ""
read -p "Type 'YES' to confirm: " CONFIRM
if [ "$CONFIRM" != "YES" ]; then
    echo "Aborted."; exit 0
fi

# ============================================================
# Step 3: Unmount + write image to SD card
# ============================================================
echo ""
echo -e "${CYAN}[3/4] Writing image to $DEVICE...${NC}"

# Unmount all partitions
umount "${DEVICE}"* 2>/dev/null || true
umount "${DEVICE}p"* 2>/dev/null || true

# Write entire image — this is the only tool that touches the SD card
dd if="$IMG" of="$DEVICE" bs=4M status=progress 2>&1
sync

echo -e "  ${GREEN}Image written.${NC}"

# Re-read partition table
partprobe "$DEVICE" 2>/dev/null || true
sleep 2

# ============================================================
# Step 4: Verify
# ============================================================
echo -e "${CYAN}[4/4] Verifying...${NC}"

IMG_MD5=$(md5sum "$IMG" | cut -d' ' -f1)
# Read back same number of bytes from device
IMG_BYTES=$(stat -c%s "$IMG")
CARD_MD5=$(dd if="$DEVICE" bs=4M count=$(( (IMG_BYTES + 4194303) / 4194304 )) 2>/dev/null | head -c "$IMG_BYTES" | md5sum | cut -d' ' -f1)

if [ "$IMG_MD5" = "$CARD_MD5" ]; then
    echo -e "  ${GREEN}[OK] Verified — image matches SD card (md5: $IMG_MD5)${NC}"
else
    echo -e "  ${RED}[FAIL] Image mismatch! Try a different SD card.${NC}"
    echo "    Image: $IMG_MD5"
    echo "    Card:  $CARD_MD5"
    exit 1
fi

# ============================================================
# Summary
# ============================================================
echo ""
echo -e "${GREEN}========================================${NC}"
echo " Flash Complete — Verified OK!"
echo -e "${GREEN}========================================${NC}"
echo ""
BOOT_PART=""
if [ -b "${DEVICE}1" ]; then BOOT_PART="${DEVICE}1"; fi
if [ -b "${DEVICE}p1" ]; then BOOT_PART="${DEVICE}p1"; fi
lsblk -o NAME,SIZE,FSTYPE,LABEL "$DEVICE" 2>/dev/null || true
echo ""
echo -e "${YELLOW}Next steps:${NC}"
echo "  1. sudo eject $DEVICE"
echo "  2. Insert SD card into BeagleBone Black"
echo "  3. Hold BOOT button (S2), then power on"
echo "  4. screen /dev/ttyUSB0 115200"
echo ""
echo "Done."
