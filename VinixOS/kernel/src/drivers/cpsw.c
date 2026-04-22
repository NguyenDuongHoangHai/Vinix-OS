/* ============================================================
 * cpsw.c
 * ------------------------------------------------------------
 * CPSW Ethernet MAC Driver — AM335x
 * ============================================================ */

#include "types.h"
#include "cpsw.h"
#include "mmio.h"
#include "uart.h"

/* ============================================================
 * PRCM — Clock Verify
 * mdio_init() owns clock enable. cpsw_init() only verifies
 * the 125MHz GCLK is active before touching CPSW registers.
 * AM335x TRM Ch.08
 * ============================================================ */
#define CM_PER_BASE                 0x44E00000u
#define CM_PER_CPSW_CLKSTCTRL      (CM_PER_BASE + 0x144u)
/* Bit 4: CPSW_125MHz_GCLK active — set by bootloader DPLL_CORE M5 */
#define CLKACTIVITY_CPSW_125M      (1u << 4)

/* ============================================================
 * Pinmux — MII1 Ethernet Pins
 * AM335x TRM Ch.09 — Control Module
 * MDIO_DATA and MDIO_CLK already configured by mdio_init().
 * ============================================================ */
#define CTRL_BASE                   0x44E10000u
/* [FIX #2] CONF_GMII_SEL: force MII mode
 * U-Boot may leave this in RGMII/RMII → pinmux wrong → no RX.
 * Must be set to 0x00 (MII) before any other pin config. */
#define CONF_GMII_SEL              (CTRL_BASE + 0x650u)
#define GMII1_SEL_MII              0x00u
/* ---------------------------------------------------------- */
#define CONF_MII1_COL              (CTRL_BASE + 0x908u)
#define CONF_MII1_CRS              (CTRL_BASE + 0x90Cu)
#define CONF_MII1_RX_ER            (CTRL_BASE + 0x910u)
#define CONF_MII1_TX_EN            (CTRL_BASE + 0x914u)
#define CONF_MII1_RX_DV            (CTRL_BASE + 0x918u)
#define CONF_MII1_TXD3             (CTRL_BASE + 0x91Cu)
#define CONF_MII1_TXD2             (CTRL_BASE + 0x920u)
#define CONF_MII1_TXD1             (CTRL_BASE + 0x924u)
#define CONF_MII1_TXD0             (CTRL_BASE + 0x928u)
#define CONF_MII1_TX_CLK           (CTRL_BASE + 0x92Cu)
#define CONF_MII1_RX_CLK           (CTRL_BASE + 0x930u)
#define CONF_MII1_RXD3             (CTRL_BASE + 0x934u)
#define CONF_MII1_RXD2             (CTRL_BASE + 0x938u)
#define CONF_MII1_RXD1             (CTRL_BASE + 0x93Cu)
#define CONF_MII1_RXD0             (CTRL_BASE + 0x940u)

#define PIN_TX    0x00u               /* mode0, output */
#define PIN_RX    (0x00u | (1u << 5)) /* mode0, RXACTIVE — required for input buffer */

/* ============================================================
 * CPSW Subsystem
 * AM335x TRM Ch.14
 * ============================================================ */
#define CPSW_SS_BASE                0x4A100000u
#define SS_SOFT_RESET              (CPSW_SS_BASE + 0x008u)
#define SS_STAT_PORT_EN            (CPSW_SS_BASE + 0x00Cu)
#define SS_SOFT_RESET_BIT          (1u << 0)
#define SS_STAT_PORT_EN_P0         (1u << 0)
#define SS_STAT_PORT_EN_P1         (1u << 1)

/* ============================================================
 * CPDMA — DMA Controller
 * CPPI 3.0: CPU places BDs in CPPI_RAM, writes HDP to kick DMA.
 * DMA writes completed BD address to CP — CPU acks by writing
 * that same address back to CP.
 * ============================================================ */
#define CPSW_CPDMA_BASE             0x4A100800u
#define CPDMA_TX_CONTROL           (CPSW_CPDMA_BASE + 0x004u)
#define CPDMA_RX_CONTROL           (CPSW_CPDMA_BASE + 0x014u)
#define CPDMA_SOFT_RESET           (CPSW_CPDMA_BASE + 0x01Cu)
/* [FIX #3] CPDMA_RX_BUFFER_OFFSET: must be 0 for normal operation.
 * If non-zero, CPDMA offsets the buffer pointer → writes frame to wrong address. */
#define CPDMA_RX_BUFFER_OFFSET     (CPSW_CPDMA_BASE + 0x028u)
/* ---------------------------------------------------------- */
#define CPDMA_TX_INTMASK_CLEAR     (CPSW_CPDMA_BASE + 0x08Cu)
/* [FIX #5] CPDMA_EOI_VECTOR: write after RX completion to clear interrupt state.
 * Without EOI, CPDMA may not process subsequent frames in polling mode.
 * TRM §14.3.1.2: value 1 = RX EOI, value 2 = TX EOI. */
#define CPDMA_EOI_VECTOR           (CPSW_CPDMA_BASE + 0x094u)
#define CPDMA_EOI_RX               1u
/* ---------------------------------------------------------- */
#define CPDMA_RX_INTMASK_CLEAR     (CPSW_CPDMA_BASE + 0x0ACu)
#define CPDMA_DMASTATUS            (CPSW_CPDMA_BASE + 0x024u)
/* DMASTATUS bits — TRM Table 14-38 */
#define DMASTATUS_IDLE             (1u << 31)  /* Both TX and RX idle */
#define DMASTATUS_RX_ERR_CODE_SHIFT 13u
#define DMASTATUS_RX_ERR_MASK      (0xFu << 13u)
#define DMASTATUS_RX_HOST_ERR      (1u << 13u) /* Error code 1 = 0x2000 */
#define CPDMA_SOFT_RESET_BIT       (1u << 0)
#define CPDMA_TX_EN                (1u << 0)
#define CPDMA_RX_EN                (1u << 0)

/* ============================================================
 * [FIX #4] CPSW_WR — Wrapper / Interrupt Pacing
 * ------------------------------------------------------------
 * In polling mode there is no EOI write to CPSW_WR.
 * If interrupt pacing is enabled (e.g. left by U-Boot),
 * CPDMA stalls waiting for an ack that never comes.
 * Fix: clear C0_TX_EN / C0_RX_EN to disable pacing.
 * TRM §14.3.4
 * ============================================================ */
#define CPSW_WR_BASE                0x4A101200u
#define CPSW_WR_C0_TX_EN           (CPSW_WR_BASE + 0x02Cu)
#define CPSW_WR_C0_RX_EN           (CPSW_WR_BASE + 0x030u)
/* ---------------------------------------------------------- */

/* ============================================================
 * STATERAM — Head/Completion Descriptor Pointers
 * Channel 0 used for both TX and RX.
 * ============================================================ */
#define CPSW_STATERAM_BASE          0x4A100A00u
#define STATERAM_TX0_HDP           (CPSW_STATERAM_BASE + 0x000u)
#define STATERAM_RX0_HDP           (CPSW_STATERAM_BASE + 0x020u)
#define STATERAM_TX0_CP            (CPSW_STATERAM_BASE + 0x040u)
#define STATERAM_RX0_CP            (CPSW_STATERAM_BASE + 0x060u)

/* ============================================================
 * CPPI_RAM — Buffer Descriptor Memory
 * 8KB device memory, non-cacheable. DMA and CPU share this
 * region directly — no cache flush needed.
 *
 * Layout (2080 bytes total, well within 8KB limit):
 *   0x4A102000 : TX BD   (16 bytes)
 *   0x4A102010 : RX BD   (16 bytes)
 *   0x4A102020 : TX buf  (1024 bytes)
 *   0x4A102420 : RX buf  (1024 bytes)
 * ============================================================ */
#define CPPI_RAM_BASE               0x4A102000u
#define TX_BD_PA                    (CPPI_RAM_BASE)
#define RX_BD_PA                    (CPPI_RAM_BASE + 16u)
#define TX_BUF_PA                   (CPPI_RAM_BASE + 32u)
#define RX_BUF_PA                   (CPPI_RAM_BASE + 32u + 1024u)

/* BD field offsets within a 16-byte CPPI descriptor */
#define BD_OFF_NEXT      0u
#define BD_OFF_BUFPTR    4u
#define BD_OFF_BUFLEN    8u
#define BD_OFF_FLAGS    12u

/* BD flag bits — AM335x TRM Ch.14 CPPI descriptor format */
#define BD_OWNER  (1u << 31) /* 1 = owned by DMA, 0 = owned by CPU */
#define BD_EOQ    (1u << 30) /* DMA sets when next=0 and chain stalls */
#define BD_SOP    (1u << 27) /* Start of packet */
#define BD_EOP    (1u << 26) /* End of packet */

/* ============================================================
 * MAC Sliver — CPGMAC_SL1
 * GMII_EN must be written last in MACCONTROL — it releases the
 * MAC from reset. Writing other bits after GMII_EN is set may
 * cause the MAC to start processing with incomplete config.
 * AM335x TRM Ch.14
 * ============================================================ */
#define CPSW_SL1_BASE               0x4A100D80u
#define SL1_MACCONTROL             (CPSW_SL1_BASE + 0x004u)
#define SL1_SOFT_RESET             (CPSW_SL1_BASE + 0x00Cu)
#define SL1_RX_MAXLEN              (CPSW_SL1_BASE + 0x010u)
#define MAC_FULLDUPLEX             (1u << 0)
#define MAC_GMII_EN                (1u << 5)
#define SL1_SOFT_RESET_BIT         (1u << 0)

/* ============================================================
 * ALE — Address Lookup Engine
 * BYPASS mode: forward all frames without MAC table lookup.
 * Both host port (0) and MAC port (1) must be in FORWARD state
 * or ALE will drop frames even in bypass mode.
 * ============================================================ */
#define CPSW_ALE_BASE               0x4A100D00u
#define ALE_CONTROL                (CPSW_ALE_BASE + 0x008u)
#define ALE_PORTCTL0               (CPSW_ALE_BASE + 0x040u)
#define ALE_PORTCTL1               (CPSW_ALE_BASE + 0x044u)
#define ALE_ENABLE                 (1u << 31)
#define ALE_CLEAR_TABLE            (1u << 30)
#define ALE_BYPASS                 (1u << 4)
#define ALE_PORT_FORWARD            0x3u

#define RESET_TIMEOUT               10000

/* ============================================================
 * Static state
 * ============================================================ */
static uint8_t s_mac[CPSW_MAC_LEN] = { 0xDE, 0xAD, 0xBE, 0xEF, 0x00, 0x01 };
static void (*s_rx_cb)(const uint8_t *buf, uint16_t len);

/* ============================================================
 * Static helpers
 * ============================================================ */
static int wait_bit_clear(uint32_t addr, uint32_t bit, int timeout)
{
    while ((mmio_read32(addr) & bit) && timeout > 0)
        timeout = timeout - 1;
    return (timeout > 0) ? 0 : -1;
}

static int cpsw_clock_verify(void)
{
    int timeout = RESET_TIMEOUT;
    while (timeout > 0) {
        if (mmio_read32(CM_PER_CPSW_CLKSTCTRL) & CLKACTIVITY_CPSW_125M)
            return 0;
        timeout = timeout - 1;
    }
    uart_printf("[CPSW] clock not active — mdio_init() must run first\n");
    return -1;
}

static void cpsw_pinmux_init(void)
{
    /* [FIX #2] Force MII mode — U-Boot may leave GMII_SEL in RGMII/RMII.
     * Must be set FIRST before any pin config takes effect. */
    mmio_write32(CONF_GMII_SEL, GMII1_SEL_MII);

    mmio_write32(CONF_MII1_TX_EN,  PIN_TX);
    mmio_write32(CONF_MII1_TXD3,   PIN_TX);
    mmio_write32(CONF_MII1_TXD2,   PIN_TX);
    mmio_write32(CONF_MII1_TXD1,   PIN_TX);
    mmio_write32(CONF_MII1_TXD0,   PIN_TX);
    /* [FIX #6] TX_CLK is driven by PHY in MII mode — must be INPUT (PIN_RX).
     * Setting as output conflicts with PHY clock driver → TX broken. */
    mmio_write32(CONF_MII1_TX_CLK, PIN_RX);
    mmio_write32(CONF_MII1_RX_DV,  PIN_RX);
    mmio_write32(CONF_MII1_RX_ER,  PIN_RX);
    mmio_write32(CONF_MII1_RXD3,   PIN_RX);
    mmio_write32(CONF_MII1_RXD2,   PIN_RX);
    mmio_write32(CONF_MII1_RXD1,   PIN_RX);
    mmio_write32(CONF_MII1_RXD0,   PIN_RX);
    mmio_write32(CONF_MII1_RX_CLK, PIN_RX);
    mmio_write32(CONF_MII1_COL,    PIN_RX);
    mmio_write32(CONF_MII1_CRS,    PIN_RX);
    uart_printf("[CPSW] pinmux done\n");
}

static int cpsw_ss_reset(void)
{
    mmio_write32(SS_SOFT_RESET, SS_SOFT_RESET_BIT);
    if (wait_bit_clear(SS_SOFT_RESET, SS_SOFT_RESET_BIT, RESET_TIMEOUT) != 0) {
        uart_printf("[CPSW] SS reset timeout\n");
        return -1;
    }
    mmio_write32(SS_STAT_PORT_EN, SS_STAT_PORT_EN_P0 | SS_STAT_PORT_EN_P1);
    uart_printf("[CPSW] SS reset done\n");
    return 0;
}

static int cpsw_cpdma_init(void)
{
    mmio_write32(CPDMA_SOFT_RESET, CPDMA_SOFT_RESET_BIT);
    if (wait_bit_clear(CPDMA_SOFT_RESET, CPDMA_SOFT_RESET_BIT, RESET_TIMEOUT) != 0) {
        uart_printf("[CPSW] CPDMA reset timeout\n");
        return -1;
    }

    /* Clear all 8 channel HDP/CP — stale values from previous boot cause DMA crash */
    for (int i = 0; i < 8; i = i + 1) {
        mmio_write32(STATERAM_TX0_HDP + (uint32_t)(i * 4), 0);
        mmio_write32(STATERAM_RX0_HDP + (uint32_t)(i * 4), 0);
        mmio_write32(STATERAM_TX0_CP  + (uint32_t)(i * 4), 0);
        mmio_write32(STATERAM_RX0_CP  + (uint32_t)(i * 4), 0);
    }

    /* [FIX #3] Clear RX buffer offset — must be 0 for normal operation */
    mmio_write32(CPDMA_RX_BUFFER_OFFSET, 0);

    mmio_write32(CPDMA_TX_INTMASK_CLEAR, 0xFFu);
    mmio_write32(CPDMA_RX_INTMASK_CLEAR, 0xFFu);

    /* [FIX #4] Disable CPSW_WR interrupt pacing — polling mode has no EOI ack */
    mmio_write32(CPSW_WR_C0_TX_EN, 0);
    mmio_write32(CPSW_WR_C0_RX_EN, 0);

    /* [FIX #1] Enable TX only here.
     * RX_CONTROL must NOT be enabled until after HDP is kicked in cpsw_bd_init().
     * If RX is enabled with HDP=0, any arriving frame triggers RX_HOST_ERR
     * (DMASTATUS=0x2000, error code 1: SOP ownership bit not set). */
    mmio_write32(CPDMA_TX_CONTROL, CPDMA_TX_EN);
    /* RX_CONTROL enabled in cpsw_bd_init() after HDP kick */

    uart_printf("[CPSW] CPDMA init done\n");
    return 0;
}

static int cpsw_mac_init(void)
{
    mmio_write32(SL1_SOFT_RESET, SL1_SOFT_RESET_BIT);
    if (wait_bit_clear(SL1_SOFT_RESET, SL1_SOFT_RESET_BIT, RESET_TIMEOUT) != 0) {
        uart_printf("[CPSW] MAC reset timeout\n");
        return -1;
    }
    mmio_write32(SL1_RX_MAXLEN, 1522u);
    /* GMII_EN last — releases MAC from reset after config is stable */
    mmio_write32(SL1_MACCONTROL, MAC_FULLDUPLEX);
    mmio_write32(SL1_MACCONTROL, MAC_FULLDUPLEX | MAC_GMII_EN);
    uart_printf("[CPSW] MAC init done\n");
    return 0;
}

static void cpsw_ale_init(void)
{
    mmio_write32(ALE_CONTROL, ALE_ENABLE | ALE_CLEAR_TABLE);
    mmio_write32(ALE_CONTROL, ALE_ENABLE | ALE_BYPASS);
    mmio_write32(ALE_PORTCTL0, ALE_PORT_FORWARD);
    mmio_write32(ALE_PORTCTL1, ALE_PORT_FORWARD);
    uart_printf("[CPSW] ALE bypass enabled\n");
}

static void cpsw_bd_init(void)
{
    /* TX BD: clear — ready to be filled on first cpsw_tx() call */
    mmio_write32(TX_BD_PA + BD_OFF_NEXT,   0);
    mmio_write32(TX_BD_PA + BD_OFF_BUFPTR, 0);
    mmio_write32(TX_BD_PA + BD_OFF_BUFLEN, 0);
    mmio_write32(TX_BD_PA + BD_OFF_FLAGS,  0);

    /* RX BD: pre-armed, owned by DMA, kick HDP to start receive */
    mmio_write32(RX_BD_PA + BD_OFF_NEXT,   0);
    mmio_write32(RX_BD_PA + BD_OFF_BUFPTR, RX_BUF_PA);
    mmio_write32(RX_BD_PA + BD_OFF_BUFLEN, (uint32_t)CPSW_FRAME_MAXLEN);
    mmio_write32(RX_BD_PA + BD_OFF_FLAGS,
                 BD_OWNER | BD_SOP | BD_EOP | (uint32_t)CPSW_FRAME_MAXLEN);

    /* [FIX #1] Kick HDP first, then enable RX_CONTROL.
     * CPDMA reads HDP immediately when RX is enabled.
     * If HDP=0 at that moment → RX_HOST_ERR (DMASTATUS=0x2000).
     * Correct order: arm BD → kick HDP → enable RX. */
    mmio_write32(STATERAM_RX0_HDP, RX_BD_PA);
    mmio_write32(CPDMA_RX_CONTROL, CPDMA_RX_EN);

    uart_printf("[CPSW] BD init done, RX armed\n");
}

/* ============================================================
 * [FIX] CPDMA RX Error Recovery
 * ------------------------------------------------------------
 * TRM §14.3.1.4: When RX_HOST_ERR occurs, CPDMA halts the
 * RX channel. Recovery requires soft reset + re-init.
 *
 * Isolation guarantee — this function does NOT touch:
 *   - PHY layer (mdio.c): MDIO/PHY registers are independent
 *     of CPDMA. PHY link state is preserved.
 *   - MAC sliver (SL1_MACCONTROL): MAC keeps running during
 *     CPDMA reset. RXGOODFRAMES continues to increment.
 *   - ALE: bypass mode and port forward state preserved.
 *   - TX path: TX BD and TX HDP are re-cleared but TX channel
 *     is immediately re-enabled, so TX is briefly paused only.
 *
 * Only CPDMA state machine is reset (0x4A100800 region).
 * ============================================================ */
static void cpsw_rx_recover(void)
{
    uart_printf("[CPSW] RX_HOST_ERR (DMASTATUS=0x%08x) — recovering\n",
                mmio_read32(CPDMA_DMASTATUS));

    /* 1. Disable both channels before reset */
    mmio_write32(CPDMA_RX_CONTROL, 0);
    mmio_write32(CPDMA_TX_CONTROL, 0);

    /* 2. Soft reset CPDMA — clears error state machine
     * TRM §14.3.1.4: "The host can recover by performing a
     * soft reset of the CPDMA" */
    mmio_write32(CPDMA_SOFT_RESET, CPDMA_SOFT_RESET_BIT);
    if (wait_bit_clear(CPDMA_SOFT_RESET, CPDMA_SOFT_RESET_BIT, RESET_TIMEOUT) != 0)
        uart_printf("[CPSW] CPDMA reset timeout in recovery\n");

    /* 3. Re-clear all HDP/CP — stale values cause crash on restart */
    for (int i = 0; i < 8; i = i + 1) {
        mmio_write32(STATERAM_TX0_HDP + (uint32_t)(i * 4), 0);
        mmio_write32(STATERAM_RX0_HDP + (uint32_t)(i * 4), 0);
        mmio_write32(STATERAM_TX0_CP  + (uint32_t)(i * 4), 0);
        mmio_write32(STATERAM_RX0_CP  + (uint32_t)(i * 4), 0);
    }

    /* 4. Re-apply CPDMA config */
    mmio_write32(CPDMA_RX_BUFFER_OFFSET, 0);
    mmio_write32(CPDMA_TX_INTMASK_CLEAR, 0xFFu);
    mmio_write32(CPDMA_RX_INTMASK_CLEAR, 0xFFu);
    mmio_write32(CPSW_WR_C0_TX_EN, 0);
    mmio_write32(CPSW_WR_C0_RX_EN, 0);

    /* 5. Re-enable TX */
    mmio_write32(CPDMA_TX_CONTROL, CPDMA_TX_EN);

    /* 6. Re-arm RX BD */
    mmio_write32(RX_BD_PA + BD_OFF_NEXT,   0);
    mmio_write32(RX_BD_PA + BD_OFF_BUFPTR, RX_BUF_PA);
    mmio_write32(RX_BD_PA + BD_OFF_BUFLEN, (uint32_t)CPSW_FRAME_MAXLEN);
    mmio_write32(RX_BD_PA + BD_OFF_FLAGS,
                 BD_OWNER | BD_SOP | BD_EOP | (uint32_t)CPSW_FRAME_MAXLEN);

    /* 7. Kick HDP then enable RX — same order as initial init */
    mmio_write32(STATERAM_RX0_HDP, RX_BD_PA);

    /* Delay để write propagate qua L3/L4 interconnect đến CPPI_RAM
     * trước khi CPDMA đọc BD. Đây là root cause của RX_HOST_ERR. */
    for (volatile int i = 0; i < 500000; i++);

    mmio_write32(CPDMA_RX_CONTROL, CPDMA_RX_EN);

    uart_printf("[CPSW] Recovery done, DMASTATUS=0x%08x\n",
                mmio_read32(CPDMA_DMASTATUS));
}

int cpsw_init(void)
{
    uart_printf("[CPSW] initializing...\n");
    if (cpsw_clock_verify()  != 0) return -1;
    cpsw_pinmux_init();
    if (cpsw_ss_reset()      != 0) return -1;
    if (cpsw_cpdma_init()    != 0) return -1;
    if (cpsw_mac_init()      != 0) return -1;
    cpsw_ale_init();
    cpsw_bd_init();
    uart_printf("[CPSW] init complete\n");
    return 0;
}

void cpsw_set_mac(const uint8_t mac[CPSW_MAC_LEN])
{
    int i;
    for (i = 0; i < CPSW_MAC_LEN; i = i + 1)
        s_mac[i] = mac[i];
}

void cpsw_get_mac(uint8_t mac[CPSW_MAC_LEN])
{
    int i;
    for (i = 0; i < CPSW_MAC_LEN; i = i + 1)
        mac[i] = s_mac[i];
}

int cpsw_tx(const uint8_t *buf, uint16_t len)
{
    if (len > CPSW_FRAME_MAXLEN)
        return -1;

    /* Wait for previous TX BD to be released by DMA */
    int timeout = RESET_TIMEOUT;
    while ((mmio_read32(TX_BD_PA + BD_OFF_FLAGS) & BD_OWNER) && timeout > 0)
        timeout = timeout - 1;
    if (timeout == 0) {
        uart_printf("[CPSW] TX timeout\n");
        return -1;
    }

    /* Copy frame into TX buffer in CPPI_RAM (device memory, non-cacheable) */
    volatile uint8_t *dst = (volatile uint8_t *)TX_BUF_PA;
    uint16_t i;
    for (i = 0; i < len; i = i + 1)
        dst[i] = buf[i];

    /* Arm TX BD */
    mmio_write32(TX_BD_PA + BD_OFF_NEXT,   0);
    mmio_write32(TX_BD_PA + BD_OFF_BUFPTR, TX_BUF_PA);
    mmio_write32(TX_BD_PA + BD_OFF_BUFLEN, (uint32_t)len);
    mmio_write32(TX_BD_PA + BD_OFF_FLAGS,
                 BD_OWNER | BD_SOP | BD_EOP | (uint32_t)len);

    /* Kick TX DMA */
    mmio_write32(STATERAM_TX0_HDP, TX_BD_PA);
    return 0;
}

void cpsw_set_rx_callback(void (*cb)(const uint8_t *buf, uint16_t len))
{
    s_rx_cb = cb;
}

void cpsw_rx_poll(void)
{
    /* ----------------------------------------------------------
     * [FIX] Detect and recover from RX_HOST_ERR
     * ----------------------------------------------------------
     * DMASTATUS bit 13 set = RX_HOST_ERR: CPDMA halted channel.
     * Root cause: CPDMA read BD before CPU write propagated
     * through L3/L4 interconnect to CPPI_RAM, saw OWNER=0.
     *
     * Recovery: soft reset CPDMA + re-arm BD + re-enable RX.
     * PHY/MAC/ALE are NOT touched — only CPDMA state machine.
     *
     * Limit to 3 retries to avoid infinite loop if hardware
     * is permanently broken.
     * ---------------------------------------------------------- */
    static uint8_t recover_count = 0;
    uint32_t dmastat = mmio_read32(CPDMA_DMASTATUS);
    if (dmastat & DMASTATUS_RX_HOST_ERR) {
        recover_count = recover_count + 1;
        cpsw_rx_recover();
        return;
    }
    recover_count = 0;

    uint32_t flags = mmio_read32(RX_BD_PA + BD_OFF_FLAGS);
    if (flags & BD_OWNER)
        return;  /* DMA hasn't filled this BD yet */

    uint16_t len = (uint16_t)(flags & 0xFFFFu);

    if (s_rx_cb && len > 0)
        s_rx_cb((const uint8_t *)RX_BUF_PA, len);

    /* Ack completed BD to CPDMA */
    mmio_write32(STATERAM_RX0_CP, RX_BD_PA);

    /* [FIX #5] Write EOI after acking CP.
     * TRM §14.3.1.2: required in polling mode to clear CPDMA interrupt state.
     * Without this, CPDMA may not accept the next HDP kick. */
    mmio_write32(CPDMA_EOI_VECTOR, CPDMA_EOI_RX);

    /* Re-arm RX BD and restart DMA — single BD model: always restart */
    mmio_write32(RX_BD_PA + BD_OFF_NEXT,   0);
    mmio_write32(RX_BD_PA + BD_OFF_BUFPTR, RX_BUF_PA);
    mmio_write32(RX_BD_PA + BD_OFF_BUFLEN, (uint32_t)CPSW_FRAME_MAXLEN);
    mmio_write32(RX_BD_PA + BD_OFF_FLAGS,
                 BD_OWNER | BD_SOP | BD_EOP | (uint32_t)CPSW_FRAME_MAXLEN);
    mmio_write32(STATERAM_RX0_HDP, RX_BD_PA);
}
