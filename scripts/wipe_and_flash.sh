#!/bin/bash
# ============================================================
# wipe_and_flash.sh
# ------------------------------------------------------------
# Build VinixOS and flash directly to SD card.
#
# Disk layout:
#   Sector 0          MBR (partition table + 0xAA55 signature)
#   Sector 256        MLO (128KB offset, RAW mode)
#   Sector 512        MLO redundant copy 1
#   Sector 768        MLO redundant copy 2
#   Sector 2048       kernel.bin (1MB offset)
#   Sector 8192+      FAT32 partition (4MB offset) — rootfs
#
# Usage: sudo ./wipe_and_flash.sh [OPTIONS] /dev/sdX
#
# Options:
#   --skip-build    Skip building, use existing MLO + kernel.bin
#   --help          Show this help message
#
# WARNING: This will DESTROY ALL DATA on the target device!
# ============================================================

set -e

# ============================================================
# Color output
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
INITFS_DIR="$TOPDIR/VinixOS/initfs"

# ============================================================
# Parse arguments
# ============================================================
SKIP_BUILD=0
DEVICE=""

for arg in "$@"; do
    case "$arg" in
        --skip-build) SKIP_BUILD=1 ;;
        --help|-h)
            echo "Usage: sudo $0 [--skip-build] /dev/sdX"
            exit 0 ;;
        -*)
            echo -e "${RED}Error: Unknown option: $arg${NC}"; exit 1 ;;
        *)
            DEVICE="$arg" ;;
    esac
done

if [ -z "$DEVICE" ]; then
    echo "Usage: sudo $0 [--skip-build] /dev/sdX"
    exit 1
fi

if [ "$(id -u)" -ne 0 ]; then
    echo -e "${RED}Error: Must run as root (sudo).${NC}"
    exit 1
fi

# ============================================================
# Validate device
# ============================================================
if [ ! -b "$DEVICE" ]; then
    echo -e "${RED}Error: $DEVICE is not a block device.${NC}"
    exit 1
fi

SYSTEM_MOUNTS=$(lsblk -no MOUNTPOINT "$DEVICE" 2>/dev/null | grep -E "^/$|^/home$|^/boot$|^/boot/efi$" || true)
if [ -n "$SYSTEM_MOUNTS" ]; then
    echo -e "${RED}Error: $DEVICE contains system mount points!${NC}"
    exit 1
fi

REMOVABLE=$(cat "/sys/block/$(basename "$DEVICE")/removable" 2>/dev/null || echo "")
if [ "$REMOVABLE" != "1" ]; then
    echo -e "${YELLOW}Warning: $DEVICE is not marked as removable.${NC}"
    read -p "Are you sure this is an SD card? (y/N): " RMCHECK
    if [ "$RMCHECK" != "y" ] && [ "$RMCHECK" != "Y" ]; then
        echo "Aborted."; exit 0
    fi
fi

# ============================================================
# Header
# ============================================================
echo -e "${CYAN}========================================${NC}"
echo " VinixOS SD Card Wipe & Flash"
echo -e "${CYAN}========================================${NC}"
echo "Device: $DEVICE"
echo ""

# ============================================================
# Step 1: Build
# ============================================================
if [ "$SKIP_BUILD" -eq 0 ]; then
    echo -e "${CYAN}[1/3] Building...${NC}"
    make -C "$TOPDIR/VinixOS" bootloader || { echo -e "${RED}Bootloader build failed!${NC}"; exit 1; }
    make -C "$TOPDIR/VinixOS" userspace  || { echo -e "${RED}Userspace build failed!${NC}"; exit 1; }
    make -C "$TOPDIR/VinixOS" kernel     || { echo -e "${RED}Kernel build failed!${NC}"; exit 1; }
    echo -e "${GREEN}[1/3] Build complete.${NC}"
    echo ""
else
    echo -e "${YELLOW}[1/3] Build skipped.${NC}"
    echo ""
fi

if [ ! -f "$MLO" ]; then
    echo -e "${RED}Error: MLO not found at $MLO${NC}"; exit 1
fi
if [ ! -f "$KERNEL" ]; then
    echo -e "${RED}Error: kernel.bin not found at $KERNEL${NC}"; exit 1
fi

echo "MLO:    $(stat -c%s "$MLO") bytes"
echo "Kernel: $(stat -c%s "$KERNEL") bytes"
echo ""

# ============================================================
# Confirm
# ============================================================
echo -e "${YELLOW}Current layout:${NC}"
lsblk -o NAME,SIZE,FSTYPE,LABEL,MOUNTPOINT "$DEVICE" 2>/dev/null || true
echo ""
echo -e "${RED}WARNING: ALL DATA on $DEVICE will be DESTROYED!${NC}"
read -p "Type 'YES' to confirm: " CONFIRM
if [ "$CONFIRM" != "YES" ]; then
    echo "Aborted."; exit 0
fi
echo ""

# ============================================================
# Step 2: Unmount + write
# ============================================================
echo -e "${CYAN}[2/3] Flashing...${NC}"

umount "${DEVICE}"* 2>/dev/null || true
umount "${DEVICE}p"* 2>/dev/null || true

# Wipe first 9MB (covers MBR + MLO + kernel + start of FAT32 partition)
dd if=/dev/zero of="$DEVICE" bs=1M count=9 status=none

# Create MBR partition table: one FAT32 LBA partition starting at sector 8192
# Size: 131072 sectors (64MB) — enough for rootfs, fits on any SD card
sfdisk "$DEVICE" >/dev/null 2>&1 <<EOF
label: dos
8192,131072,c
EOF
partprobe "$DEVICE" 2>/dev/null || true
sleep 1

# Determine partition device node (/dev/sdX1 or /dev/mmcblkXp1)
if [ -b "${DEVICE}1" ]; then
    PART="${DEVICE}1"
elif [ -b "${DEVICE}p1" ]; then
    PART="${DEVICE}p1"
else
    echo -e "${RED}Error: Partition device not found after sfdisk${NC}"
    exit 1
fi

# Format FAT32 partition
mkfs.fat -F 32 -n VINIX "$PART" >/dev/null

# MLO at RAW offsets (TRM 26.1.8.5.5) — written AFTER mkfs.fat since
# mkfs touches sector 0 (partition table region), but MLO sectors
# 256/512/768 are in the unpartitioned gap and untouched.
dd if="$MLO" of="$DEVICE" bs=512 seek=256 conv=notrunc status=none
dd if="$MLO" of="$DEVICE" bs=512 seek=512 conv=notrunc status=none
dd if="$MLO" of="$DEVICE" bs=512 seek=768 conv=notrunc status=none

# Kernel at sector 2048 (also in unpartitioned gap)
dd if="$KERNEL" of="$DEVICE" bs=512 seek=2048 conv=notrunc status=none

# Mount FAT32 partition and copy initfs files (rootfs contents)
MNT=$(mktemp -d)
mount "$PART" "$MNT"
if [ -d "$INITFS_DIR" ]; then
    cp "$INITFS_DIR"/* "$MNT/" 2>/dev/null || true
    echo "  Copied $(ls "$INITFS_DIR" | wc -l) file(s) from initfs/ to rootfs"
fi
sync
umount "$MNT"
rmdir "$MNT"

sync
echo -e "${GREEN}[2/3] Flash complete.${NC}"
echo ""

# ============================================================
# Step 3: Verify
# ============================================================
echo -e "${CYAN}[3/3] Verifying...${NC}"

# Verify MLO at sector 256
MLO_MD5=$(md5sum "$MLO" | cut -d' ' -f1)
MLO_BYTES=$(stat -c%s "$MLO")
CARD_MLO_MD5=$(dd if="$DEVICE" bs=512 skip=256 count=$(( (MLO_BYTES + 511) / 512 )) 2>/dev/null | head -c "$MLO_BYTES" | md5sum | cut -d' ' -f1)

if [ "$MLO_MD5" = "$CARD_MLO_MD5" ]; then
    echo -e "  ${GREEN}[OK] MLO verified${NC}"
else
    echo -e "  ${RED}[FAIL] MLO mismatch!${NC}"; exit 1
fi

# Verify kernel at sector 2048
KERN_MD5=$(md5sum "$KERNEL" | cut -d' ' -f1)
KERN_BYTES=$(stat -c%s "$KERNEL")
CARD_KERN_MD5=$(dd if="$DEVICE" bs=512 skip=2048 count=$(( (KERN_BYTES + 511) / 512 )) 2>/dev/null | head -c "$KERN_BYTES" | md5sum | cut -d' ' -f1)

if [ "$KERN_MD5" = "$CARD_KERN_MD5" ]; then
    echo -e "  ${GREEN}[OK] Kernel verified${NC}"
else
    echo -e "  ${RED}[FAIL] Kernel mismatch!${NC}"; exit 1
fi

# ============================================================
# Done
# ============================================================
echo ""
echo -e "${GREEN}========================================${NC}"
echo " Flash Complete — Verified OK!"
echo -e "${GREEN}========================================${NC}"
echo ""
echo -e "${YELLOW}Next steps:${NC}"
echo "  1. sudo eject $DEVICE"
echo "  2. Insert SD card into BeagleBone Black"
echo "  3. Hold BOOT button (S2), then power on"
echo "  4. screen /dev/ttyUSB0 115200"
echo ""
echo "Done."
