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
#define CLKACTIVITY_CPSW_125M      (1u << 4)

/* ============================================================
 * Pinmux — MII1 Ethernet Pins
 * AM335x TRM Ch.09 — Control Module
 * MDIO_DATA and MDIO_CLK already configured by mdio_init().
 * ============================================================ */
#define CTRL_BASE                   0x44E10000u
#define CONF_GMII_SEL              (CTRL_BASE + 0x650u)
#define GMII1_SEL_MII              0x00u
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

#define PIN_TX    0x00u
#define PIN_RX    (0x00u | (1u << 5))

/* ============================================================
 * CPSW Subsystem — AM335x TRM Ch.14
 * ============================================================ */
#define CPSW_SS_BASE                0x4A100000u
#define SS_SOFT_RESET              (CPSW_SS_BASE + 0x008u)
#define SS_STAT_PORT_EN            (CPSW_SS_BASE + 0x00Cu)
#define SS_SOFT_RESET_BIT          (1u << 0)
#define SS_STAT_PORT_EN_P0         (1u << 0)
#define SS_STAT_PORT_EN_P1         (1u << 1)

/* ============================================================
 * CPSW_STATS — Network Statistics — AM335x TRM Ch.14 §14.5.4
 * Base: 0x4A100900
 * ============================================================ */
#define CPSW_STATS_BASE             0x4A100900u
#define STATS_RXGOODFRAMES         (CPSW_STATS_BASE + 0x00u)
#define STATS_RXBROADCASTFRAMES    (CPSW_STATS_BASE + 0x04u)
#define STATS_RXMULTICASTFRAMES    (CPSW_STATS_BASE + 0x08u)
#define STATS_RXCRCERRORS          (CPSW_STATS_BASE + 0x10u)
#define STATS_RXOVERSIZEDFRAMES    (CPSW_STATS_BASE + 0x18u)
#define STATS_RXUNDERSIZEDFRAMES   (CPSW_STATS_BASE + 0x20u)

/* ============================================================
 * CPDMA — DMA Controller — AM335x TRM Ch.14
 * ============================================================ */
#define CPSW_CPDMA_BASE             0x4A100800u
#define CPDMA_TX_CONTROL           (CPSW_CPDMA_BASE + 0x004u)
#define CPDMA_RX_CONTROL           (CPSW_CPDMA_BASE + 0x014u)
#define CPDMA_SOFT_RESET           (CPSW_CPDMA_BASE + 0x01Cu)
#define CPDMA_DMASTATUS            (CPSW_CPDMA_BASE + 0x024u)
#define CPDMA_RX_BUFFER_OFFSET     (CPSW_CPDMA_BASE + 0x028u)
#define CPDMA_TX_INTMASK_CLEAR     (CPSW_CPDMA_BASE + 0x08Cu)
/* ----------------------------------------------------------
 * [FIX] CPDMA_EOI_VECTOR — End of Interrupt
 * ----------------------------------------------------------
 * TRM Ch.14 §14.3.1.2: sau khi xử lý RX packet, CPU phải
 * ghi 1h vào CPDMA_EOI_VECTOR để clear interrupt state.
 * Không ghi EOI → CPDMA không clear → stuck sau frame đầu.
 * ---------------------------------------------------------- */
#define CPDMA_EOI_VECTOR           (CPSW_CPDMA_BASE + 0x094u)
#define CPDMA_EOI_RX               1u
#define CPDMA_EOI_TX               2u
/* ---------------------------------------------------------- */
#define CPDMA_RX_INTMASK_CLEAR     (CPSW_CPDMA_BASE + 0x0ACu)
#define CPDMA_SOFT_RESET_BIT       (1u << 0)
#define CPDMA_TX_EN                (1u << 0)
#define CPDMA_RX_EN                (1u << 0)

/* ============================================================
 * [FIX] CPSW_WR — Wrapper / Interrupt Pacing
 * ------------------------------------------------------------
 * Vấn đề: CPDMA TX stall ở polling mode — không có EOI write
 *   → CPDMA chờ interrupt ack không bao giờ đến.
 * Fix: clear C0_TX_EN / C0_RX_EN trước khi enable CPDMA.
 * Ref: AM335x TRM Ch.14 §14.3.4
 * ============================================================ */
#define CPSW_WR_BASE                0x4A101200u
#define CPSW_WR_C0_TX_EN           (CPSW_WR_BASE + 0x02Cu)
#define CPSW_WR_C0_RX_EN           (CPSW_WR_BASE + 0x030u)
/* ------------------------------------------------------------ */

/* ============================================================
 * STATERAM — Head/Completion Descriptor Pointers
 * ============================================================ */
#define CPSW_STATERAM_BASE          0x4A100A00u
#define STATERAM_TX0_HDP           (CPSW_STATERAM_BASE + 0x000u)
#define STATERAM_RX0_HDP           (CPSW_STATERAM_BASE + 0x020u)
#define STATERAM_TX0_CP            (CPSW_STATERAM_BASE + 0x040u)
#define STATERAM_RX0_CP            (CPSW_STATERAM_BASE + 0x060u)

/* ============================================================
 * CPPI_RAM — Buffer Descriptor Memory (8KB device RAM)
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

#define BD_OFF_NEXT      0u
#define BD_OFF_BUFPTR    4u
#define BD_OFF_BUFLEN    8u
#define BD_OFF_FLAGS    12u

#define BD_OWNER  (1u << 31)
#define BD_EOQ    (1u << 30)
#define BD_SOP    (1u << 27)
#define BD_EOP    (1u << 26)

/* ============================================================
 * MAC Sliver — CPGMAC_SL1
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
    /* Force MII mode — U-Boot may leave GMII_SEL in RMII/RGMII */
    mmio_write32(CONF_GMII_SEL, GMII1_SEL_MII);

    mmio_write32(CONF_MII1_TX_EN,  PIN_TX);
    mmio_write32(CONF_MII1_TXD3,   PIN_TX);
    mmio_write32(CONF_MII1_TXD2,   PIN_TX);
    mmio_write32(CONF_MII1_TXD1,   PIN_TX);
    mmio_write32(CONF_MII1_TXD0,   PIN_TX);
    /* TX_CLK is driven by PHY in MII mode — must be input */
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

    /* Clear all 8 channel HDP/CP — stale values from previous boot */
    for (int i = 0; i < 8; i = i + 1) {
        mmio_write32(STATERAM_TX0_HDP + (uint32_t)(i * 4), 0);
        mmio_write32(STATERAM_RX0_HDP + (uint32_t)(i * 4), 0);
        mmio_write32(STATERAM_TX0_CP  + (uint32_t)(i * 4), 0);
        mmio_write32(STATERAM_RX0_CP  + (uint32_t)(i * 4), 0);
    }

    mmio_write32(CPDMA_RX_BUFFER_OFFSET, 0);
    mmio_write32(CPDMA_TX_INTMASK_CLEAR, 0xFFu);
    mmio_write32(CPDMA_RX_INTMASK_CLEAR, 0xFFu);

    /* [FIX] Disable CPSW_WR interrupt pacing — polling mode has no EOI */
    mmio_write32(CPSW_WR_C0_TX_EN, 0);
    mmio_write32(CPSW_WR_C0_RX_EN, 0);

    /* ----------------------------------------------------------
     * [FIX] Enable TX only — delay RX enable until after HDP kick
     * ----------------------------------------------------------
     * TRM §14.4.2: "The host enables packet reception on a given
     * channel by writing the address of the first buffer descriptor
     * to the channel's head descriptor pointer"
     * 
     * Nếu enable RX_CONTROL trước khi kick HDP, CPDMA sẽ cố đọc
     * BD từ HDP=0 khi có frame đến → RX_HOST_ERR.
     * 
     * Thứ tự đúng: kick HDP → enable RX_CONTROL (trong cpsw_bd_init)
     * ---------------------------------------------------------- */
    mmio_write32(CPDMA_TX_CONTROL, CPDMA_TX_EN);
    /* RX_CONTROL sẽ được enable trong cpsw_bd_init() sau khi kick HDP */
    /* ---------------------------------------------------------- */

    uart_printf("[CPSW] CPDMA TX_CTRL=0x%08x  RX_CTRL=0x%08x  DMASTATUS=0x%08x\n",
                mmio_read32(CPDMA_TX_CONTROL),
                mmio_read32(CPDMA_RX_CONTROL),
                mmio_read32(CPDMA_DMASTATUS));
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
    /* GMII_EN last — releases MAC from reset */
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
    /* TX BD: clear */
    mmio_write32(TX_BD_PA + BD_OFF_NEXT,   0);
    mmio_write32(TX_BD_PA + BD_OFF_BUFPTR, 0);
    mmio_write32(TX_BD_PA + BD_OFF_BUFLEN, 0);
    mmio_write32(TX_BD_PA + BD_OFF_FLAGS,  0);

    /* RX BD: arm */
    mmio_write32(RX_BD_PA + BD_OFF_NEXT,   0);
    mmio_write32(RX_BD_PA + BD_OFF_BUFPTR, RX_BUF_PA);
    mmio_write32(RX_BD_PA + BD_OFF_BUFLEN, (uint32_t)CPSW_FRAME_MAXLEN);
    mmio_write32(RX_BD_PA + BD_OFF_FLAGS,
                 BD_OWNER | BD_SOP | BD_EOP | (uint32_t)CPSW_FRAME_MAXLEN);

    /* ----------------------------------------------------------
     * [FIX] Enable Statistics Port 1 — TRM §14.4.6
     * ----------------------------------------------------------
     * Per TRM: "Configure the Statistics Port Enable register"
     * Phải enable statistics port trước khi đọc STATS registers.
     * Nếu không enable → statistics không được update.
     * ---------------------------------------------------------- */
    mmio_write32(SS_STAT_PORT_EN, SS_STAT_PORT_EN_P0 | SS_STAT_PORT_EN_P1);
    /* ---------------------------------------------------------- */

    /* ----------------------------------------------------------
     * [FIX] Kick HDP trước, enable RX_CONTROL sau
     * ----------------------------------------------------------
     * TRM §14.4.2: "The host enables packet reception on a given
     * channel by writing the address of the first buffer descriptor
     * to the channel's head descriptor pointer"
     * 
     * Thứ tự đúng:
     * 1. Arm BD (set OWNER=1)
     * 2. Kick HDP (write BD address)
     * 3. Enable RX_CONTROL
     * 
     * Nếu enable RX_CONTROL trước khi kick HDP → CPDMA đọc HDP=0
     * khi có frame đến → RX_HOST_ERR.
     * ---------------------------------------------------------- */
    mmio_write32(STATERAM_RX0_HDP, RX_BD_PA);
    mmio_write32(CPDMA_RX_CONTROL, CPDMA_RX_EN);
    /* ---------------------------------------------------------- */

    uart_printf("[CPSW] BD init done, RX armed\n");
    uart_printf("[CPSW] DMASTATUS=0x%08x  RX0_HDP=0x%08x\n",
                mmio_read32(CPDMA_DMASTATUS),
                mmio_read32(STATERAM_RX0_HDP));
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

    uart_printf("[CPSW] MACCTRL=0x%08x  ALE_CTRL=0x%08x\n",
                mmio_read32(SL1_MACCONTROL), mmio_read32(ALE_CONTROL));
    uart_printf("[CPSW] ALE_P0=0x%08x   ALE_P1=0x%08x\n",
                mmio_read32(ALE_PORTCTL0), mmio_read32(ALE_PORTCTL1));
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

    int timeout = RESET_TIMEOUT;
    while ((mmio_read32(TX_BD_PA + BD_OFF_FLAGS) & BD_OWNER) && timeout > 0)
        timeout = timeout - 1;
    if (timeout == 0) {
        uart_printf("[CPSW] TX timeout\n");
        return -1;
    }

    volatile uint8_t *dst = (volatile uint8_t *)TX_BUF_PA;
    uint16_t i;
    for (i = 0; i < len; i = i + 1)
        dst[i] = buf[i];

    mmio_write32(TX_BD_PA + BD_OFF_NEXT,   0);
    mmio_write32(TX_BD_PA + BD_OFF_BUFPTR, TX_BUF_PA);
    mmio_write32(TX_BD_PA + BD_OFF_BUFLEN, (uint32_t)len);
    mmio_write32(TX_BD_PA + BD_OFF_FLAGS,
                 BD_OWNER | BD_SOP | BD_EOP | (uint32_t)len);

    mmio_write32(STATERAM_TX0_HDP, TX_BD_PA);
    return 0;
}

void cpsw_set_rx_callback(void (*cb)(const uint8_t *buf, uint16_t len))
{
    s_rx_cb = cb;
}

/* ----------------------------------------------------------
 * [DIAGNOSTIC] Comprehensive RX path diagnostic
 * ----------------------------------------------------------
 * Vấn đề: Wireshark confirm ARP ra wire, BBB không nhận.
 * Cần dump toàn bộ CPSW/CPDMA/ALE state để tìm root cause.
 * ---------------------------------------------------------- */
void cpsw_rx_poll(void)
{
    static uint32_t call_count = 0;
    call_count = call_count + 1;

    /* Dump comprehensive state every 1000 calls — ~10s at 10ms/tick */
    if (call_count % 1000 == 0) {
        uart_printf("[CPSW] === RX DIAGNOSTIC #%u ===\n", call_count);
        uart_printf("[CPSW] BD_FLAGS=0x%08x  DMASTATUS=0x%08x\n",
                    mmio_read32(RX_BD_PA + BD_OFF_FLAGS),
                    mmio_read32(CPDMA_DMASTATUS));
        uart_printf("[CPSW] RX0_HDP=0x%08x  RX0_CP=0x%08x\n",
                    mmio_read32(STATERAM_RX0_HDP),
                    mmio_read32(STATERAM_RX0_CP));
        uart_printf("[CPSW] MACCTRL=0x%08x  ALE_CTRL=0x%08x\n",
                    mmio_read32(SL1_MACCONTROL),
                    mmio_read32(ALE_CONTROL));
        uart_printf("[CPSW] ALE_P0=0x%08x  ALE_P1=0x%08x\n",
                    mmio_read32(ALE_PORTCTL0),
                    mmio_read32(ALE_PORTCTL1));
        uart_printf("[CPSW] RX_CTRL=0x%08x  TX_CTRL=0x%08x\n",
                    mmio_read32(CPDMA_RX_CONTROL),
                    mmio_read32(CPDMA_TX_CONTROL));
        uart_printf("[CPSW] SS_STAT_PORT_EN=0x%08x\n",
                    mmio_read32(SS_STAT_PORT_EN));
        /* ----------------------------------------------------------
         * [DIAGNOSTIC] MAC Statistics — verify PHY → MAC path
         * ----------------------------------------------------------
         * RXGOODFRAMES: số frame MAC nhận được từ PHY (không lỗi)
         * RXCRCERRORS: số frame bị CRC error
         * Nếu RXGOODFRAMES > 0 → MAC nhận được frame từ PHY
         * Nếu RXGOODFRAMES = 0 → frame không đến MAC (PHY/pinmux issue)
         * ---------------------------------------------------------- */
        uart_printf("[CPSW] STATS: RXGOOD=%u  RXCRC=%u  RXOVER=%u  RXUNDER=%u\n",
                    mmio_read32(STATS_RXGOODFRAMES),
                    mmio_read32(STATS_RXCRCERRORS),
                    mmio_read32(STATS_RXOVERSIZEDFRAMES),
                    mmio_read32(STATS_RXUNDERSIZEDFRAMES));
        /* ---------------------------------------------------------- */
    }

    uint32_t flags = mmio_read32(RX_BD_PA + BD_OFF_FLAGS);
    if (flags & BD_OWNER)
        return;

    uint16_t len = (uint16_t)(flags & 0xFFFFu);

    uart_printf("[CPSW] RX frame len=%d\n", len);
    if (s_rx_cb && len > 0)
        s_rx_cb((const uint8_t *)RX_BUF_PA, len);

    /* Ack completed BD */
    mmio_write32(STATERAM_RX0_CP, RX_BD_PA);

    /* ----------------------------------------------------------
     * [FIX] Write EOI to CPDMA_EOI_VECTOR
     * ----------------------------------------------------------
     * TRM Ch.14 §14.3.1.2: sau khi ghi RX_CP, phải ghi 1h
     * vào CPDMA_EOI_VECTOR để clear interrupt state.
     * Thiếu bước này → CPDMA không nhận frame tiếp theo.
     * ---------------------------------------------------------- */
    mmio_write32(CPDMA_EOI_VECTOR, CPDMA_EOI_RX);
    /* ---------------------------------------------------------- */

    /* Re-arm RX BD and restart DMA */
    mmio_write32(RX_BD_PA + BD_OFF_NEXT,   0);
    mmio_write32(RX_BD_PA + BD_OFF_BUFPTR, RX_BUF_PA);
    mmio_write32(RX_BD_PA + BD_OFF_BUFLEN, (uint32_t)CPSW_FRAME_MAXLEN);
    mmio_write32(RX_BD_PA + BD_OFF_FLAGS,
                 BD_OWNER | BD_SOP | BD_EOP | (uint32_t)CPSW_FRAME_MAXLEN);
    mmio_write32(STATERAM_RX0_HDP, RX_BD_PA);
}
/* ---------------------------------------------------------- */
