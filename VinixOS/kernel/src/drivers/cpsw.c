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
/* GMII_SEL: selects MII/RMII/RGMII for CPSW ports — AM335x TRM Ch.09 §9.3.1.50 */
#define CONF_GMII_SEL              (CTRL_BASE + 0x650u)
#define GMII1_SEL_MII              0x00u  /* bits[1:0]=00: Port1 = MII */
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
#define CPDMA_TX_INTMASK_CLEAR     (CPSW_CPDMA_BASE + 0x08Cu)
#define CPDMA_RX_INTMASK_CLEAR     (CPSW_CPDMA_BASE + 0x0ACu)
#define CPDMA_SOFT_RESET_BIT       (1u << 0)
#define CPDMA_TX_EN                (1u << 0)
#define CPDMA_RX_EN                (1u << 0)

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
#define MAC_LOOPBACK               (1u << 1) /* TX→RX inside MAC, bypass MII pins */
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
    /* U-Boot may leave GMII_SEL in RMII/RGMII mode — force MII for Port 1 */
    mmio_write32(CONF_GMII_SEL,    GMII1_SEL_MII);
    uart_printf("[CPSW] GMII_SEL=0x%08x (set to MII)\n",
                mmio_read32(CONF_GMII_SEL));

    mmio_write32(CONF_MII1_TX_EN,  PIN_TX);
    mmio_write32(CONF_MII1_TXD3,   PIN_TX);
    mmio_write32(CONF_MII1_TXD2,   PIN_TX);
    mmio_write32(CONF_MII1_TXD1,   PIN_TX);
    mmio_write32(CONF_MII1_TXD0,   PIN_TX);
    /* TX_CLK in MII mode is driven BY the PHY — must be input */
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

    mmio_write32(CPDMA_TX_INTMASK_CLEAR, 0xFFu);
    mmio_write32(CPDMA_RX_INTMASK_CLEAR, 0xFFu);
    mmio_write32(CPDMA_TX_CONTROL, CPDMA_TX_EN);
    mmio_write32(CPDMA_RX_CONTROL, CPDMA_RX_EN);
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

    mmio_write32(STATERAM_RX0_HDP, RX_BD_PA);
    uart_printf("[CPSW] BD init done, RX armed\n");
}

/* ============================================================
 * MAC loopback self-test — connects TX directly to RX inside
 * the MAC Sliver, bypassing MII pins and PHY entirely.
 * PASS → internal CPSW→CPDMA path works; external MII issue.
 * FAIL → CPDMA or CPSW internal path broken.
 * ============================================================ */
static void cpsw_loopback_selftest(void)
{
    static const uint8_t test_pkt[64] = {
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff,  /* dst: broadcast */
        0xde, 0xad, 0xbe, 0xef, 0x00, 0x01,  /* src: our MAC */
        0x90, 0x00,                           /* ethertype: loopback test */
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    };
    int i;

    mmio_write32(SL1_MACCONTROL, MAC_FULLDUPLEX | MAC_GMII_EN | MAC_LOOPBACK);

    /* NOTE: STATERAM_RX0_HDP already set by cpsw_bd_init.
     * CPDMA spec: writing to a non-zero HDP is INVALID — do NOT write again. */

    cpsw_tx(test_pkt, 64);

    /* Phase 1: confirm TX DMA released the TX BD */
    for (i = 0; i < 100000; i = i + 1) {
        if (!(mmio_read32(TX_BD_PA + BD_OFF_FLAGS) & BD_OWNER))
            break;
    }
    uart_printf("[CPSW] Loopback TX: %s  TX_CP=0x%08x  TX_FLAGS=0x%08x\n",
                (i < 100000) ? "SENT" : "TIMEOUT",
                mmio_read32(STATERAM_TX0_CP),
                mmio_read32(TX_BD_PA + BD_OFF_FLAGS));

    /* Phase 2: wait for RX DMA to receive the looped-back frame */
    for (i = 0; i < 200000; i = i + 1) {
        uint32_t flags = mmio_read32(RX_BD_PA + BD_OFF_FLAGS);
        if (!(flags & BD_OWNER)) {
            uart_printf("[CPSW] Loopback PASS — len=%d\n", (int)(flags & 0xFFFFu));
            /* CPDMA cleared HDP to 0 after EOQ — ack and restart */
            mmio_write32(STATERAM_RX0_CP, RX_BD_PA);
            mmio_write32(RX_BD_PA + BD_OFF_FLAGS,
                         BD_OWNER | BD_SOP | BD_EOP | (uint32_t)CPSW_FRAME_MAXLEN);
            mmio_write32(STATERAM_RX0_HDP, RX_BD_PA);
            goto done;
        }
    }
    uart_printf("[CPSW] Loopback FAIL — RX timeout  RX_HDP=0x%08x  RX_FLAGS=0x%08x\n",
                mmio_read32(STATERAM_RX0_HDP),
                mmio_read32(RX_BD_PA + BD_OFF_FLAGS));

done:
    mmio_write32(SL1_MACCONTROL, MAC_FULLDUPLEX | MAC_GMII_EN);
}

/* ============================================================
 * Public functions
 * ============================================================ */

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
    cpsw_loopback_selftest();

    /* Diagnostic: verify key register state after full init */
    uart_printf("[CPSW] MACCTRL=0x%08x  ALE_CTRL=0x%08x\n",
                mmio_read32(SL1_MACCONTROL), mmio_read32(ALE_CONTROL));
    uart_printf("[CPSW] ALE_P0=0x%08x   ALE_P1=0x%08x\n",
                mmio_read32(ALE_PORTCTL0), mmio_read32(ALE_PORTCTL1));
    uart_printf("[CPSW] RX0_HDP=0x%08x  CPDMA_RX_CTRL=0x%08x\n",
                mmio_read32(STATERAM_RX0_HDP), mmio_read32(CPDMA_RX_CONTROL));

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
    static uint32_t poll_count = 0;
    poll_count = poll_count + 1;
    if (poll_count % 500 == 0)
        uart_printf("[CPSW] poll #%u  RX0_HDP=0x%08x  BD_FLAGS=0x%08x\n",
                    poll_count,
                    mmio_read32(STATERAM_RX0_HDP),
                    mmio_read32(RX_BD_PA + BD_OFF_FLAGS));

    uint32_t flags = mmio_read32(RX_BD_PA + BD_OFF_FLAGS);
    if (flags & BD_OWNER)
        return;  /* DMA hasn't filled this BD yet */

    uint16_t len = (uint16_t)(flags & 0xFFFFu);

    uart_printf("[CPSW] RX frame len=%d\n", len);
    if (s_rx_cb && len > 0)
        s_rx_cb((const uint8_t *)RX_BUF_PA, len);

    /* Ack completed BD to CPDMA */
    mmio_write32(STATERAM_RX0_CP, RX_BD_PA);

    /* Re-arm RX BD and restart DMA — single BD model: always restart */
    mmio_write32(RX_BD_PA + BD_OFF_NEXT,   0);
    mmio_write32(RX_BD_PA + BD_OFF_BUFPTR, RX_BUF_PA);
    mmio_write32(RX_BD_PA + BD_OFF_BUFLEN, (uint32_t)CPSW_FRAME_MAXLEN);
    mmio_write32(RX_BD_PA + BD_OFF_FLAGS,
                 BD_OWNER | BD_SOP | BD_EOP | (uint32_t)CPSW_FRAME_MAXLEN);
    mmio_write32(STATERAM_RX0_HDP, RX_BD_PA);
}
