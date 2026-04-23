/* ============================================================
 * main.c
 * ------------------------------------------------------------
 * Kernel Entry Point
 * ============================================================ */

#include "uart.h"
#include "watchdog.h"
#include "scheduler.h"
#include "idle.h"
#include "timer.h"
#include "irq.h"
#include "intc.h"
#include "mmu.h"
#include "platform_device.h"
#include "platform_drivers.h"
#include "page_alloc.h"
#include "slab.h"
#include "vmm.h"
#include "atomic.h"
#include "spinlock.h"
#include "cpu.h"

extern void sync_selftest(void);
#include "vfs.h"
#include "devfs.h"
#include "procfs.h"
#include "mmc.h"
#include "mbr.h"
#include "fat32.h"
#include "buffer_cache.h"
#include "selftest.h"
#include "syscalls.h"
#include "types.h"
#include "i2c.h"
#include "lcdc.h"
#include "tda19988.h"
#include "fb.h"
#include "boot_screen.h"

/* ================================================== */
/* Hai Nguyen: add MDIO/PHY + CPSW + Ethernet layer  */
#include "mdio.h"
#include "cpsw.h"
#include "ether.h"
#include "netcore.h"
#include "arp.h"
/* end Hai Nguyen                                      */
/* ================================================== */

/* ============================================================ */
/* User Space Payload (Defined in payload.S) */
/* ============================================================ */
extern uint8_t _shell_payload_start;
extern uint8_t _shell_payload_end;

/* ============================================================
 * User App Memory Definitions
 * ============================================================ */
static struct task_struct shell_task;

/* We use the end of the 1MB User Space (0x40000000 -> 0x40100000)
 * as the stack base for the User Task. Let's reserve the top 4KB. */
#define USER_STACK_BASE (USER_SPACE_VA + (USER_SPACE_MB * 1024 * 1024))
#define USER_STACK_SIZE 4096

/* ============================================================
 * Network Protocol Tests
 * ============================================================ */
#include "network_test_runner.h"

/* ============================================================
 * Kernel Main
 * ============================================================ */
void kernel_main(void)
{
    /* 1. Hardware Init */
    watchdog_disable();

    /* Populate the platform bus, then register drivers in the order
     * their hardware needs to come up. Each register() triggers a
     * probe for the matching device. */
    platform_init();
    omap_uart_driver_register();

    uart_printf("\n\n");
    uart_printf("========================================\n");
    uart_printf(" VinixOS: Interactive Shell\n");
    uart_printf("========================================\n\n");

    /* 1.5 MMU Phase B — remove identity map, update VBAR to high VA.
     * MMU was already enabled by entry.S (Phase A trampoline).
     * We are now running at VA 0xC0xxxxxx. */
    mmu_init();

    /* ================================================== */
    /* Hai Nguyen: init MDIO/PHY + CPSW + Ethernet layer */
    mdio_init();
#ifdef ENABLE_LAYER1_TEST
    mdio_layer1_test();
#endif
    cpsw_init();

    /* Layer 3: Ethernet frame driver
     * ARP, IP, ICMP sẽ được thêm sau khi Ethernet layer hoạt động */
    {
        uint8_t mac[6];
        cpsw_get_mac(mac);
        ether_init(mac);
    }

    /* ------------------------------------------------
     * Layer 3 Smoke Test: verify ether_tx() hoạt động
     * ------------------------------------------------
     * Gửi 1 frame broadcast với EtherType=0x9000 (test).
     * Dùng Wireshark trên laptop filter: eth.type == 0x9000
     * Nếu thấy frame → ether_tx() + CPDMA + MAC + PHY OK.
     * ------------------------------------------------ */
    {
        static const uint8_t bcast[6] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};
        /* Payload: "ETHOK" + MAC của BBB */
        uint8_t msg[12];
        msg[0]='E'; msg[1]='T'; msg[2]='H';
        msg[3]='O'; msg[4]='K'; msg[5]=0;
        cpsw_get_mac(msg + 6);  /* append BBB MAC for identification */
        int r = ether_tx(bcast, 0x9000, msg, 12);
        uart_printf("[ETH] smoke test tx: %s\n", (r == 0) ? "OK" : "FAIL");
        uart_printf("[ETH] Wireshark filter: eth.type == 0x9000\n");
    }

    /* ------------------------------------------------
     * Layer 3 Network Stack Initialization
     * ------------------------------------------------
     * Initialize ARP and register real handlers
     * ------------------------------------------------ */
    /* Set my IP and MAC for ARP FIRST */
    {
        uint8_t mac[6];
        cpsw_get_mac(mac);
        arp_set_my_mac(mac);
        arp_set_my_ip(0xc0a80a64);  /* 192.168.10.100 */
    }
    netcore_init();
    /* end Hai Nguyen                                     */
    /* ================================================== */

    page_alloc_init();
    page_alloc_selftest();

    slab_init();
    slab_selftest();

    vmm_init();
    vmm_selftest();

    sync_selftest();

    /* Graphics subsystem: pixel clock must be running before TDA can output TMDS.
     * Unlike QNX (where U-Boot already started LCDC), we're bare-metal —
     * LCDC raster must start first to provide pixel clock on LCD_PCLK pin.
     *
     * Order: I2C → LCDC (config + raster start) → TDA (full init with pixel clock) */
    i2c_init();
    i2c_scan();
    lcdc_init();                /* Configure LCDC + DPLL (raster NOT started yet) */
    lcdc_start_raster();        /* Start pixel clock — TDA needs this for TMDS */
    tda19988_init();            /* Full TDA config with pixel clock present */

    fb_init();

    omap_intc_driver_register();
    irq_init();
    uart_enable_rx_interrupt();
    /* NOTE: cpsw_rx_irq_enable() is called AFTER irq_enable() below
     * to ensure CPU IRQ is enabled before CPDMA can fire IRQ 41 */

    /* 1.6 Initialize VFS and mount FAT32 rootfs from SD card */
    uart_printf("[BOOT] Initializing Virtual File System...\n");
    bcache_init();
    vfs_init();

    if (omap_hsmmc_driver_register() != E_OK) {
        uart_printf("[BOOT] ERROR: MMC driver probe failed\n");
        while (1);
    }

    uint32_t part_lba;
    if (mbr_find_fat32(&part_lba, NULL) != E_OK) {
        uart_printf("[BOOT] ERROR: No FAT32 partition found on SD card\n");
        while (1);
    }

    if (fat32_init(part_lba) != E_OK) {
        uart_printf("[BOOT] ERROR: FAT32 init failed\n");
        while (1);
    }

    if (vfs_mount("/", fat32_get_operations()) != E_OK) {
        uart_printf("[BOOT] ERROR: Failed to mount FAT32 at /\n");
        while (1);
    }

    if (vfs_mount("/dev", devfs_init()) != E_OK) {
        uart_printf("[BOOT] ERROR: Failed to mount devfs at /dev\n");
        while (1);
    }

    if (vfs_mount("/proc", procfs_init()) != E_OK) {
        uart_printf("[BOOT] ERROR: Failed to mount procfs at /proc\n");
        while (1);
    }

    /* 1.7 Load User Payload */
    uint32_t payload_size = (uint32_t)&_shell_payload_end - (uint32_t)&_shell_payload_start;
    uart_printf("[BOOT] Loading User App Payload to 0x%x (Size: %d bytes)\n", USER_SPACE_VA, payload_size);

    uint8_t *src = &_shell_payload_start;
    uint8_t *dst = (uint8_t *)USER_SPACE_VA;
    for (uint32_t i = 0; i < payload_size; i++)
        dst[i] = src[i];
    uart_printf("[BOOT] Payload successfully copied to User Space.\n");

    /* 2. Schedule Initialization */
    scheduler_init();

    struct task_struct *idle_ptr = get_idle_task();
    scheduler_add_task(idle_ptr);

    shell_task.name = "init";
    shell_task.state = TASK_STATE_READY;
    shell_task.id = 0;

    task_stack_init(&shell_task, (void (*)(void))USER_SPACE_VA,
                    (void *)(USER_STACK_BASE - USER_STACK_SIZE),
                    USER_STACK_SIZE);

    if (scheduler_add_task(&shell_task) < 0)
        uart_printf("[BOOT] Failed to add User App Task\n");

    uart_printf("[BOOT] UART init complete. Starting HDMI boot screen...\n");
uart_printf("[BOOT] *** ARP TX DEBUG VERSION 8c16b56 ***\n");

    /* 3. Boot screen on HDMI — uses timer_early_init() for accurate delay.
     *    MUST run BEFORE timer_init() which reconfigures timer to 10ms auto-reload.
     *    delay_ms() needs free-running counter, not auto-reload. */
    timer_early_init();
    boot_screen_run();

    /* 4. Now reconfigure timer for scheduler (10ms auto-reload + IRQ) */
    omap_dmtimer_driver_register();

    /* Final gate: integration selftest (bcache, procfs). Panics on fail. */
    selftest_run_all();

    /* Network Protocol Tests */
    uart_printf("[BOOT] Running Network Protocol Tests...\n");
    extern network_test_results_t network_run_all_tests(void);
    network_run_all_tests();

    uart_printf("[BOOT] Boot complete. Starting scheduler...\n");

    irq_enable();
    cpsw_rx_irq_enable();  /* Enable RX IRQ AFTER CPU IRQ enabled — prevents missed EOI */
    scheduler_start();

    /* Should never reach here */
    while (1)
    {
        uart_printf("PANIC: Scheduler returned!\n");
    }
}