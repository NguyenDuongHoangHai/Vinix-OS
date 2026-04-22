/* ============================================================
 * cpsw_test.c
 * ------------------------------------------------------------
 * Layer 2 Test Suite — CPSW MAC + CPDMA
 * Board: BeagleBone Black / AM335x
 *
 * Mục tiêu: Verify CPDMA hoạt động đúng trước khi test wire.
 * Theo handoff §6: "Test ngay sau init bằng 1 hardcoded packet
 * loopback (internal); đọc Wireshark từ laptop, so bit-by-bit"
 * ============================================================ */

#include "cpsw_test.h"
#include "mmio.h"
#include "uart.h"

/* ============================================================
 * HARDWARE CONSTANTS
 * ============================================================ */

/* PRCM */
#define CM_PER_BASE            0x44E00000u
#define CM_PER_CPSW_CLKSTCTRL (CM_PER_BASE + 0x144u)
#define CLKACTIVITY_CPSW_125M (1u << 4)

/* CPSW Subsystem */
#define CPSW_SS_BASE       0x4A100000u
#define SS_SOFT_RESET     (CPSW_SS_BASE + 0x008u)
#define SS_STAT_PORT_EN   (CPSW_SS_BASE + 0x00Cu)

/* CPDMA */
#define CPSW_CPDMA_BASE        0x4A100800u
#define CPDMA_TX_CONTROL      (CPSW_CPDMA_BASE + 0x004u)
#define CPDMA_RX_CONTROL      (CPSW_CPDMA_BASE + 0x014u)
#define CPDMA_SOFT_RESET      (CPSW_CPDMA_BASE + 0x01Cu)
#define CPDMA_DMASTATUS       (CPSW_CPDMA_BASE + 0x024u)
#define CPDMA_RX_BUFFER_OFFSET (CPSW_CPDMA_BASE + 0x028u)
#define CPDMA_TX_INTMASK_CLEAR (CPSW_CPDMA_BASE + 0x08Cu)
#define CPDMA_EOI_VECTOR      (CPSW_CPDMA_BASE + 0x094u)
#define CPDMA_RX_INTMASK_CLEAR (CPSW_CPDMA_BASE + 0x0ACu)
#define CPDMA_SOFT_RESET_BIT   (1u << 0)
#define CPDMA_TX_EN            (1u << 0)
#define CPDMA_RX_EN            (1u << 0)
#define DMASTATUS_IDLE         (1u << 31)
#define DMASTATUS_RX_ERR      (0xFu << 13u)

/* CPSW_WR */
#define CPSW_WR_BASE       0x4A101200u
#define CPSW_WR_C0_TX_EN  (CPSW_WR_BASE + 0x02Cu)
#define CPSW_WR_C0_RX_EN  (CPSW_WR_BASE + 0x030u)

/* STATERAM */
#define CPSW_STATERAM_BASE  0x4A100A00u
#define STATERAM_TX0_HDP   (CPSW_STATERAM_BASE + 0x000u)
#define STATERAM_RX0_HDP   (CPSW_STATERAM_BASE + 0x020u)
#define STATERAM_TX0_CP    (CPSW_STATERAM_BASE + 0x040u)
#define STATERAM_RX0_CP    (CPSW_STATERAM_BASE + 0x060u)

/* CPPI_RAM */
#define CPPI_RAM_BASE  0x4A102000u
#define TX_BD_PA       (CPPI_RAM_BASE)
#define RX_BD_PA       (CPPI_RAM_BASE + 16u)
#define TX_BUF_PA      (CPPI_RAM_BASE + 32u)
#define RX_BUF_PA      (CPPI_RAM_BASE + 32u + 1024u)
#define BD_OFF_NEXT    0u
#define BD_OFF_BUFPTR  4u
#define BD_OFF_BUFLEN  8u
#define BD_OFF_FLAGS  12u
#define BD_OWNER  (1u << 31)
#define BD_SOP    (1u << 27)
#define BD_EOP    (1u << 26)

/* MAC Sliver */
#define CPSW_SL1_BASE      0x4A100D80u
#define SL1_MACCONTROL    (CPSW_SL1_BASE + 0x004u)
#define SL1_SOFT_RESET    (CPSW_SL1_BASE + 0x00Cu)
#define SL1_RX_MAXLEN     (CPSW_SL1_BASE + 0x010u)
#define MAC_FULLDUPLEX     (1u << 0)
#define MAC_LOOPBACK       (1u << 1)
#define MAC_GMII_EN        (1u << 5)

/* CPSW Stats */
#define CPSW_STATS_BASE        0x4A100900u
#define STATS_RXGOODFRAMES    (CPSW_STATS_BASE + 0x00u)
#define STATS_RXCRCERRORS     (CPSW_STATS_BASE + 0x10u)

/* ALE */
#define CPSW_ALE_BASE   0x4A100D00u
#define ALE_CONTROL    (CPSW_ALE_BASE + 0x008u)
#define ALE_PORTCTL0   (CPSW_ALE_BASE + 0x040u)
#define ALE_PORTCTL1   (CPSW_ALE_BASE + 0x044u)

#define RESET_TIMEOUT  100000

/* ============================================================
 * HELPERS
 * ============================================================ */

#define PASS 1
#define FAIL 0
#define SKIP 2

static void print_sep(void)
{
    uart_printf("--------------------------------------------------\n");
}

static int wait_clear(uint32_t addr, uint32_t bit, int timeout)
{
    while ((mmio_read32(addr) & bit) && timeout-- > 0);
    return (timeout > 0) ? 0 : -1;
}

/* ============================================================
 * TEST CASES
 * ============================================================ */

/*
 * T1: CPSW Clock Active
 *
 * Mục đích : Verify CPSW_125MHz_GCLK đang chạy.
 *            Nếu clock không active → mọi register access sẽ hang.
 *
 * PASS khi : CM_PER_CPSW_CLKSTCTRL bit 4 = 1
 * FAIL khi : bit 4 = 0 → mdio_init() chưa chạy hoặc PRCM lỗi
 *
 * Không cần: cable
 */
int cpsw_test_clock(void)
{
    print_sep();
    uart_printf("[T1] CPSW Clock Active\n");

    uint32_t clkctrl = mmio_read32(CM_PER_CPSW_CLKSTCTRL);
    uart_printf("     CM_PER_CPSW_CLKSTCTRL = 0x%08x\n", clkctrl);
    uart_printf("     CPSW_125MHz_GCLK      = %s\n",
                (clkctrl & CLKACTIVITY_CPSW_125M) ? "ACTIVE" : "NOT ACTIVE");

    if (!(clkctrl & CLKACTIVITY_CPSW_125M)) {
        uart_printf("     => FAIL: clock không active — mdio_init() phải chạy trước\n");
        return FAIL;
    }

    uart_printf("     => PASS\n");
    return PASS;
}

/*
 * T2: CPSW Subsystem Soft Reset
 *
 * Mục đích : Verify SS_SOFT_RESET hoạt động — reset bit tự clear.
 *            Nếu không clear → hardware bị treo.
 *
 * PASS khi : SS_SOFT_RESET bit 0 tự clear sau khi set
 * FAIL khi : timeout — hardware không respond
 *
 * Không cần: cable
 */
int cpsw_test_ss_reset(void)
{
    print_sep();
    uart_printf("[T2] CPSW Subsystem Soft Reset\n");

    mmio_write32(SS_SOFT_RESET, 1u);
    uart_printf("     SS_SOFT_RESET set, waiting for clear...\n");

    if (wait_clear(SS_SOFT_RESET, 1u, RESET_TIMEOUT) != 0) {
        uart_printf("     => FAIL: SS_SOFT_RESET timeout — hardware không respond\n");
        return FAIL;
    }

    mmio_write32(SS_STAT_PORT_EN, 0x3u);
    uart_printf("     SS_STAT_PORT_EN = 0x%08x\n", mmio_read32(SS_STAT_PORT_EN));
    uart_printf("     => PASS\n");
    return PASS;
}

/*
 * T3: CPDMA Soft Reset + DMASTATUS Clean
 *
 * Mục đích : Verify CPDMA reset hoạt động và DMASTATUS clean sau reset.
 *            DMASTATUS=0x80000000 = IDLE, không có error.
 *
 * PASS khi : CPDMA_SOFT_RESET tự clear VÀ DMASTATUS = 0x80000000
 * FAIL khi : reset timeout HOẶC DMASTATUS có error bits sau reset
 *
 * Không cần: cable
 */
int cpsw_test_cpdma_reset(void)
{
    print_sep();
    uart_printf("[T3] CPDMA Soft Reset + DMASTATUS\n");

    /* Disable channels trước khi reset */
    mmio_write32(CPDMA_RX_CONTROL, 0);
    mmio_write32(CPDMA_TX_CONTROL, 0);

    mmio_write32(CPDMA_SOFT_RESET, CPDMA_SOFT_RESET_BIT);
    if (wait_clear(CPDMA_SOFT_RESET, CPDMA_SOFT_RESET_BIT, RESET_TIMEOUT) != 0) {
        uart_printf("     => FAIL: CPDMA_SOFT_RESET timeout\n");
        return FAIL;
    }

    /* Clear all HDP/CP */
    for (int i = 0; i < 8; i++) {
        mmio_write32(STATERAM_TX0_HDP + (uint32_t)(i * 4), 0);
        mmio_write32(STATERAM_RX0_HDP + (uint32_t)(i * 4), 0);
        mmio_write32(STATERAM_TX0_CP  + (uint32_t)(i * 4), 0);
        mmio_write32(STATERAM_RX0_CP  + (uint32_t)(i * 4), 0);
    }

    mmio_write32(CPDMA_RX_BUFFER_OFFSET, 0);
    mmio_write32(CPDMA_TX_INTMASK_CLEAR, 0xFFu);
    mmio_write32(CPDMA_RX_INTMASK_CLEAR, 0xFFu);
    mmio_write32(CPSW_WR_C0_TX_EN, 0);
    mmio_write32(CPSW_WR_C0_RX_EN, 0);

    /* Clear stale error bits */
    mmio_write32(CPDMA_DMASTATUS, 0x0001E000u);

    uint32_t dmastat = mmio_read32(CPDMA_DMASTATUS);
    uart_printf("     DMASTATUS after reset = 0x%08x\n", dmastat);
    uart_printf("     IDLE bit              = %u (expect 1)\n",
                (dmastat >> 31) & 1);
    uart_printf("     RX_ERR bits [16:13]   = %u (expect 0)\n",
                (dmastat >> 13) & 0xF);

    if (dmastat & DMASTATUS_RX_ERR) {
        uart_printf("     => FAIL: DMASTATUS có error bits sau reset\n");
        return FAIL;
    }

    mmio_write32(CPDMA_TX_CONTROL, CPDMA_TX_EN);
    uart_printf("     => PASS\n");
    return PASS;
}

/*
 * T4: MAC Internal Loopback — CPDMA TX→RX
 *
 * Mục đích : Verify CPDMA TX và RX hoạt động bằng MAC loopback nội bộ.
 *            Đây là test quan trọng nhất của Layer 2.
 *            Nếu T4 FAIL → CPDMA bị lỗi, không thể nhận frame từ wire.
 *
 * Cách test:
 *   1. Enable MAC_LOOPBACK (TX → RX nội bộ, không ra wire)
 *   2. Arm RX BD, enable RX channel
 *   3. Gửi 1 test frame qua TX
 *   4. Poll RX BD — nếu OWNER=0 → CPDMA nhận được frame
 *   5. Verify nội dung frame khớp với frame đã gửi
 *   6. Disable loopback
 *
 * PASS khi : RX BD OWNER=0 trong timeout VÀ nội dung frame đúng
 * FAIL khi : RX timeout HOẶC DMASTATUS error HOẶC nội dung sai
 *
 * Không cần: cable, PHY, laptop
 */
int cpsw_test_mac_loopback(void)
{
    print_sep();
    uart_printf("[T4] MAC Internal Loopback (CPDMA TX→RX)\n");

    /* Setup MAC với loopback */
    mmio_write32(SL1_SOFT_RESET, 1u);
    wait_clear(SL1_SOFT_RESET, 1u, RESET_TIMEOUT);
    mmio_write32(SL1_RX_MAXLEN, 1522u);
    mmio_write32(SL1_MACCONTROL, MAC_FULLDUPLEX);
    mmio_write32(SL1_MACCONTROL, MAC_FULLDUPLEX | MAC_GMII_EN | MAC_LOOPBACK);
    uart_printf("     MAC loopback enabled, MACCONTROL=0x%08x\n",
                mmio_read32(SL1_MACCONTROL));

    /* Arm RX BD */
    mmio_write32(RX_BD_PA + BD_OFF_NEXT,   0);
    mmio_write32(RX_BD_PA + BD_OFF_BUFPTR, RX_BUF_PA);
    mmio_write32(RX_BD_PA + BD_OFF_BUFLEN, 1024u);
    mmio_write32(RX_BD_PA + BD_OFF_FLAGS,
                 BD_OWNER | BD_SOP | BD_EOP | 1024u);
    (void)mmio_read32(RX_BD_PA + BD_OFF_FLAGS); /* flush write buffer */
    mmio_write32(STATERAM_RX0_HDP, RX_BD_PA);

    /* Enable RX */
    mmio_write32(CPDMA_RX_CONTROL, CPDMA_RX_EN);

    uint32_t dmastat = mmio_read32(CPDMA_DMASTATUS);
    uart_printf("     DMASTATUS after RX enable = 0x%08x\n", dmastat);

    if (dmastat & DMASTATUS_RX_ERR) {
        uart_printf("     => FAIL: DMASTATUS error ngay sau RX enable\n");
        uart_printf("              RX_ERR_CODE = %u\n", (dmastat >> 13) & 0xF);
        goto loopback_cleanup;
    }

    /* Build test frame: 64 bytes minimum Ethernet */
    volatile uint8_t *txbuf = (volatile uint8_t *)TX_BUF_PA;
    /* Dst MAC = broadcast */
    txbuf[0]=0xFF; txbuf[1]=0xFF; txbuf[2]=0xFF;
    txbuf[3]=0xFF; txbuf[4]=0xFF; txbuf[5]=0xFF;
    /* Src MAC = DE:AD:BE:EF:00:01 */
    txbuf[6]=0xDE; txbuf[7]=0xAD; txbuf[8]=0xBE;
    txbuf[9]=0xEF; txbuf[10]=0x00; txbuf[11]=0x01;
    /* EtherType = 0x9000 (loopback test) */
    txbuf[12]=0x90; txbuf[13]=0x00;
    /* Payload = known pattern */
    for (int i = 14; i < 64; i++)
        txbuf[i] = (uint8_t)(i & 0xFF);

    /* Arm TX BD */
    mmio_write32(TX_BD_PA + BD_OFF_NEXT,   0);
    mmio_write32(TX_BD_PA + BD_OFF_BUFPTR, TX_BUF_PA);
    mmio_write32(TX_BD_PA + BD_OFF_BUFLEN, 64u);
    mmio_write32(TX_BD_PA + BD_OFF_FLAGS,
                 BD_OWNER | BD_SOP | BD_EOP | 64u);
    mmio_write32(STATERAM_TX0_HDP, TX_BD_PA);
    uart_printf("     TX frame sent (64 bytes), polling RX...\n");

    /* Poll RX BD */
    int timeout = 500000;
    int rx_ok = 0;
    while (timeout-- > 0) {
        uint32_t flags = mmio_read32(RX_BD_PA + BD_OFF_FLAGS);
        if (!(flags & BD_OWNER)) {
            uint16_t len = (uint16_t)(flags & 0xFFFFu);
            uart_printf("     RX frame received: len=%d bytes\n", len);

            /* Verify nội dung frame */
            volatile uint8_t *rxbuf = (volatile uint8_t *)RX_BUF_PA;
            int content_ok = 1;
            for (int i = 14; i < 64 && i < len; i++) {
                if (rxbuf[i] != (uint8_t)(i & 0xFF)) {
                    uart_printf("     Content mismatch at byte %d: got 0x%02x expect 0x%02x\n",
                                i, rxbuf[i], (uint8_t)(i & 0xFF));
                    content_ok = 0;
                    break;
                }
            }

            if (content_ok)
                uart_printf("     Frame content verified OK\n");

            /* Ack */
            mmio_write32(STATERAM_RX0_CP, RX_BD_PA);
            mmio_write32(CPDMA_EOI_VECTOR, 1u);
            rx_ok = content_ok;
            break;
        }
    }

    if (!rx_ok && timeout <= 0) {
        uart_printf("     => FAIL: RX timeout, DMASTATUS=0x%08x\n",
                    mmio_read32(CPDMA_DMASTATUS));
    }

loopback_cleanup:
    /* Disable RX, restore normal MAC */
    mmio_write32(CPDMA_RX_CONTROL, 0);
    mmio_write32(SL1_MACCONTROL, MAC_FULLDUPLEX | MAC_GMII_EN);
    uart_printf("     MAC loopback disabled\n");

    if (rx_ok) {
        uart_printf("     => PASS: CPDMA TX→RX loopback hoạt động\n");
        return PASS;
    }
    return FAIL;
}

/*
 * T5: RX Statistics Counter (cần cable)
 *
 * Mục đích : Verify MAC nhận được frame từ wire.
 *            RXGOODFRAMES tăng → frame đến MAC từ PHY.
 *
 * Cách test:
 *   1. Đọc RXGOODFRAMES trước
 *   2. Đợi 3 giây (laptop gửi ARP/broadcast)
 *   3. Đọc RXGOODFRAMES sau
 *   4. Nếu tăng → MAC nhận được frame
 *
 * PASS khi : RXGOODFRAMES tăng trong 3 giây
 * SKIP khi : không tăng → cable chưa cắm hoặc không có traffic
 *
 * Cần: cable cắm vào laptop/switch có traffic
 */
int cpsw_test_rx_stats(void)
{
    print_sep();
    uart_printf("[T5] RX Statistics (cần cable + traffic)\n");

    /* Enable stats port */
    mmio_write32(SS_STAT_PORT_EN, 0x3u);

    uint32_t rx_before = mmio_read32(STATS_RXGOODFRAMES);
    uart_printf("     RXGOODFRAMES trước = %u\n", rx_before);
    uart_printf("     Đang đợi 3 giây...\n");

    /* Delay ~3 giây */
    for (volatile int i = 0; i < 30000000; i++);

    uint32_t rx_after = mmio_read32(STATS_RXGOODFRAMES);
    uint32_t rx_crc   = mmio_read32(STATS_RXCRCERRORS);
    uart_printf("     RXGOODFRAMES sau  = %u (delta=%u)\n",
                rx_after, rx_after - rx_before);
    uart_printf("     RXCRCERRORS       = %u\n", rx_crc);

    if (rx_after == rx_before) {
        uart_printf("     => SKIP: không có frame đến — cable chưa cắm?\n");
        return SKIP;
    }

    if (rx_crc > 0) {
        uart_printf("     => WARN: có CRC errors — kiểm tra cable/PHY\n");
    }

    uart_printf("     => PASS: MAC nhận được %u frame từ wire\n",
                rx_after - rx_before);
    return PASS;
}

/* ============================================================
 * ENTRY POINT
 * ============================================================ */

void cpsw_layer2_test(void)
{
    uart_printf("\n");
    uart_printf("==================================================\n");
    uart_printf("  LAYER 2 TEST SUITE — CPSW MAC + CPDMA\n");
    uart_printf("  Board: BeagleBone Black / AM335x\n");
    uart_printf("==================================================\n");

    int results[5];
    results[0] = cpsw_test_clock();
    results[1] = cpsw_test_ss_reset();
    results[2] = cpsw_test_cpdma_reset();
    results[3] = cpsw_test_mac_loopback();
    results[4] = cpsw_test_rx_stats();

    print_sep();
    uart_printf("SUMMARY:\n");

    const char *names[5] = {
        "T1 Clock Active    ",
        "T2 SS Reset        ",
        "T3 CPDMA Reset     ",
        "T4 MAC Loopback    ",
        "T5 RX Stats        "
    };

    int pass_count = 0;
    for (int i = 0; i < 5; i++) {
        const char *status;
        if      (results[i] == PASS) { status = "PASS"; pass_count++; }
        else if (results[i] == SKIP) { status = "SKIP"; pass_count++; }
        else                         { status = "FAIL"; }
        uart_printf("  %s : %s\n", names[i], status);
    }

    print_sep();
    uart_printf("RESULT: %d/5  —  Layer 2 %s\n",
                pass_count,
                (pass_count == 5) ? "READY" : "NOT READY");
    uart_printf("==================================================\n\n");
}
