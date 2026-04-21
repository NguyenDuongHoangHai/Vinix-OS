# VinixOS Makefile

CROSS_COMPILE = arm-none-eabi-
CC = $(CROSS_COMPILE)gcc
AS = $(CROSS_COMPILE)as
LD = $(CROSS_COMPILE)ld
OBJCOPY = $(CROSS_COMPILE)objcopy

# Directories
SRC = src
LINKER = linker
# Directory containing the userspace payload (shell)
USERSPACE_DIR = ../userspace
BUILD_DIR = build

# Source files
ASM_BOOT_SRC = $(SRC)/arch/arm/entry/reset.S $(SRC)/arch/arm/entry/entry.S
ASM_EXC_SRC = $(SRC)/arch/arm/exceptions/vectors.S $(SRC)/arch/arm/exceptions/exception_entry.S
C_KERNEL_SRC = $(SRC)/kernel/main.c $(SRC)/kernel/exceptions/exception_handlers.c $(SRC)/kernel/irq/irq_core.c \
               $(SRC)/kernel/core/svc_handler.c
C_DRIVER_SRC = $(SRC)/drivers/uart.c $(SRC)/drivers/watchdog.c $(SRC)/drivers/intc.c $(SRC)/drivers/timer.c \
               $(SRC)/drivers/i2c.c $(SRC)/drivers/lcdc.c $(SRC)/drivers/tda19988.c \
               $(SRC)/drivers/mmc.c $(SRC)/drivers/mbr.c
C_FS_SRC = $(SRC)/kernel/fs/vfs.c $(SRC)/kernel/fs/fat32.c
C_LIBC_SRC = libc/src/string.c

# Objects (Flattened in BUILD_DIR)
ASM_OBJ = $(addprefix $(BUILD_DIR)/, vectors.o reset.o entry.o exception_entry.o context_switch.o mmu_enable.o)
C_OBJ = $(addprefix $(BUILD_DIR)/, main.o exception_handlers.o irq_core.o uart.o watchdog.o intc.o timer.o \
               i2c.o lcdc.o tda19988.o fb.o boot_screen.o \
               scheduler.o task.o idle.o svc_handler.o mmu.o payload.o vfs.o string.o \
               mmc.o mbr.o fat32.o \
               page_alloc.o slab.o vmm.o \
               atomic.o spinlock.o wait_queue.o sleep.o sync_selftest.o \
               fork.o wait.o exec.o \
               char_dev.o devfs.o procfs.o \
               selftest.o \
               block.o buffer_cache.o \
               driver_device.o driver_platform.o \
               format.o \
               platform_memmap.o platform_devices.o)
OBJS = $(ASM_OBJ) $(C_OBJ)

C_OBJ += $(BUILD_DIR)/mdio.o
C_OBJ += $(BUILD_DIR)/cpsw.o

# Flags
CFLAGS = -Wall -nostdlib -nostartfiles -ffreestanding \
         -march=armv7-a -mtune=cortex-a8 \
         -mfloat-abi=soft \
         -mno-unaligned-access \
         -fno-builtin \
         -fno-stack-protector \
         -Iinclude \
         -Ilibc/include \
         -I$(SRC)/arch/arm/include \
         -I../platform/bbb/include \
         -I$(USERSPACE_DIR)/include \
         -O2 -g

# ==================================================
# Hai Nguyen: add MDIO/PHY driver (Layer 1)
# Phải đặt SAU khi CFLAGS được define để += không bị ghi đè
TESTCASE_L1 = ../testcase/layer1
ifneq ($(wildcard $(TESTCASE_L1)/mdio_test.c),)
    CFLAGS += -DENABLE_LAYER1_TEST -I$(TESTCASE_L1)
    C_OBJ  += $(BUILD_DIR)/mdio_test.o
endif
# end Hai Nguyen
# ==================================================
ASFLAGS = -march=armv7-a -mfloat-abi=soft
LDFLAGS = -T $(LINKER)/kernel.ld -nostdlib -Wl,-Map=$(BUILD_DIR)/kernel.map

# Targets
all: $(BUILD_DIR)/kernel.bin

# Create build directory
$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

# Userspace Dependencies - Build shell
PAYLOAD_BIN = $(USERSPACE_DIR)/build/apps/shell/shell.bin
$(PAYLOAD_BIN):
	$(MAKE) -C $(USERSPACE_DIR)

# Payload Embed Target
$(BUILD_DIR)/payload.o: $(SRC)/kernel/payload.S $(PAYLOAD_BIN) | $(BUILD_DIR)
	$(CC) $(ASFLAGS) -c $< -o $@


# Assembly files - Boot
$(BUILD_DIR)/reset.o: $(SRC)/arch/arm/entry/reset.S | $(BUILD_DIR)
	$(CC) $(ASFLAGS) -c $< -o $@

$(BUILD_DIR)/entry.o: $(SRC)/arch/arm/entry/entry.S | $(BUILD_DIR)
	$(CC) $(ASFLAGS) -c $< -o $@

# Assembly files - Exceptions
$(BUILD_DIR)/vectors.o: $(SRC)/arch/arm/exceptions/vectors.S | $(BUILD_DIR)
	$(CC) $(ASFLAGS) -c $< -o $@

$(BUILD_DIR)/exception_entry.o: $(SRC)/arch/arm/exceptions/exception_entry.S | $(BUILD_DIR)
	$(CC) $(ASFLAGS) -c $< -o $@

# Assembly files - Scheduler
$(BUILD_DIR)/context_switch.o: $(SRC)/arch/arm/scheduler/context_switch.S | $(BUILD_DIR)
	$(CC) $(ASFLAGS) -c $< -o $@

# C files - Kernel
$(BUILD_DIR)/main.o: $(SRC)/kernel/main.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/exception_handlers.o: $(SRC)/kernel/exceptions/exception_handlers.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/irq_core.o: $(SRC)/kernel/irq/irq_core.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@


$(BUILD_DIR)/scheduler.o: $(SRC)/kernel/scheduler/scheduler.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/task.o: $(SRC)/kernel/scheduler/task.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/idle.o: $(SRC)/kernel/scheduler/idle.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/svc_handler.o: $(SRC)/kernel/core/svc_handler.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

# C files - Drivers
$(BUILD_DIR)/uart.o: $(SRC)/drivers/uart.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/watchdog.o: $(SRC)/drivers/watchdog.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/intc.o: $(SRC)/drivers/intc.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/timer.o: $(SRC)/drivers/timer.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/i2c.o: $(SRC)/drivers/i2c.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/lcdc.o: $(SRC)/drivers/lcdc.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/tda19988.o: $(SRC)/drivers/tda19988.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/mmc.o: $(SRC)/drivers/mmc.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/mbr.o: $(SRC)/drivers/mbr.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

# ==================================================
# Hai Nguyen: add MDIO/PHY driver build rule (Layer 1)
$(BUILD_DIR)/mdio.o: $(SRC)/drivers/mdio.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/mdio_test.o: $(TESTCASE_L1)/mdio_test.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/cpsw.o: $(SRC)/drivers/cpsw.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@
# end Hai Nguyen
# ==================================================

$(BUILD_DIR)/fb.o: $(SRC)/drivers/fb.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/boot_screen.o: $(SRC)/ui/boot_screen.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

# Assembly files - MMU
$(BUILD_DIR)/mmu_enable.o: $(SRC)/arch/arm/mmu/mmu_enable.S | $(BUILD_DIR)
	$(CC) $(ASFLAGS) -c $< -o $@

# C files - MMU
$(BUILD_DIR)/mmu.o: $(SRC)/kernel/mmu/mmu.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

# C files - Memory Manager
$(BUILD_DIR)/page_alloc.o: $(SRC)/kernel/mm/page_alloc.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/slab.o: $(SRC)/kernel/mm/slab.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/vmm.o: $(SRC)/kernel/mm/vmm.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

# C files - Synchronization primitives
$(BUILD_DIR)/atomic.o: $(SRC)/kernel/sync/atomic.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/spinlock.o: $(SRC)/kernel/sync/spinlock.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/wait_queue.o: $(SRC)/kernel/sync/wait_queue.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/sleep.o: $(SRC)/kernel/sync/sleep.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/sync_selftest.o: $(SRC)/kernel/sync/sync_selftest.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

# C files - Process model
$(BUILD_DIR)/fork.o: $(SRC)/kernel/proc/fork.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/wait.o: $(SRC)/kernel/proc/wait.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/exec.o: $(SRC)/kernel/proc/exec.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

# C files - Platform (BBB)
$(BUILD_DIR)/platform_memmap.o: ../platform/bbb/memmap.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/platform_devices.o: ../platform/bbb/devices.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

# C files - Driver model
$(BUILD_DIR)/driver_device.o: $(SRC)/kernel/driver/device.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/driver_platform.o: $(SRC)/kernel/driver/platform.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

# C files - Block layer
$(BUILD_DIR)/block.o: $(SRC)/kernel/block/block.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/buffer_cache.o: $(SRC)/kernel/block/buffer_cache.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

# C files - Filesystem
$(BUILD_DIR)/vfs.o: $(SRC)/kernel/fs/vfs.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/fat32.o: $(SRC)/kernel/fs/fat32.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/devfs.o: $(SRC)/kernel/fs/devfs.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/procfs.o: $(SRC)/kernel/fs/procfs.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/selftest.o: $(SRC)/kernel/test/selftest.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/char_dev.o: $(SRC)/kernel/dev/char_dev.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

# C files - libc
$(BUILD_DIR)/string.o: libc/src/string.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/format.o: libc/src/format.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@


# Link
$(BUILD_DIR)/kernel.elf: $(OBJS)
	$(CC) $(LDFLAGS) -o $@ $^ -lgcc
	@echo "=== Kernel sections ==="
	@$(CROSS_COMPILE)size $@

# Create binary
$(BUILD_DIR)/kernel.bin: $(BUILD_DIR)/kernel.elf
	$(OBJCOPY) -O binary $< $@
	@echo ""
	@echo "Build complete: $@"
	@ls -lh $@

clean:
	@echo "  CLEAN   kernel headers, objects and elf"
	@rm -rf $(BUILD_DIR) *.o *.elf *.bin *.map uImage
	@echo "  CLEAN   userspace payload"
	@$(MAKE) -C $(USERSPACE_DIR) clean

.PHONY: all clean