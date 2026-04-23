/* ============================================================
 * cpsw.c
 * ------------------------------------------------------------
 * CPSW Ethernet MAC Driver — AM335x
 *
 * RX model: Interrupt-driven (like Linux cpsw driver)
 * ============================================================ */

#include "types.h"
#include "cpsw.h"
#include "mmio.h"
#include "uart.h"
#include "irq.h"
#include "intc.h"

/* ============================================================
 * PRCM — Clock Verify
 * ============================================================ */
#define CM_PER_BASE                 0x44E00000u
#define CM_PER_CPSW_CLKSTCTRL      (CM_PER_BASE + 0x144u)
#define CLKACTIVITY_CPSW_125M      (1u << 4)

/* ============================================================
 * Pinmux — MII1 Ethernet Pins
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
 * CPSW Subsystem
 * ============================================================ */
#define CPSW_SS_BASE                0x4A100000u
#define SS_SOFT_RESET              (CPSW_SS_BASE + 0x008u)
#define SS_STAT_PORT_EN            (CPSW_SS_BASE + 0x00Cu)
#define SS_SOFT_RESET_BIT          (1u << 0)
#define SS_STAT_PORT_EN_P0         (1u << 0)
#define SS_STAT_PORT_EN_P1         (1u << 1)

/* ============================================================
 * CPDMA — DMA Controller
 * ============================================================ */
#define CPSW_CPDMA_BASE             0x4A100800u
#define CPDMA_TX_CONTROL           (CPSW_CPDMA_BASE + 0x004u)
#define CPDMA_RX_CONTROL           (CPSW_CPDMA_BASE + 0x014u)
#define CPDMA_SOFT_RESET           (CPSW_CPDMA_BASE + 0x01Cu)
#define CPDMA_DMASTATUS            (CPSW_CPDMA_BASE + 0x024u)
#define CPDMA_RX_BUFFER_OFFSET     (CPSW_CPDMA_BASE + 0x028u)
#define CPDMA_TX_INTMASK_CLEAR     (CPSW_CPDMA_BASE + 0x08Cu)
#define CPDMA_EOI_VECTOR           (CPSW_CPDMA_BASE + 0x094u)
#define CPDMA_RX_INTMASK_SET       (CPSW_CPDMA_BASE + 0x0A8u)
#define CPDMA_RX_INTMASK_CLEAR     (CPSW_CPDMA_BASE + 0x0ACu)
#define CPDMA_EOI_RX               1u
#define CPDMA_SOFT_RESET_BIT       (1u << 0)
#define CPDMA_TX_EN                (1u << 0)
#define CPDMA_RX_EN                (1u << 0)

/* ============================================================
 * CPSW_WR — Wrapper / Interrupt Pacing
 * TRM §14.3.4
 * ============================================================ */
#define CPSW_WR_BASE                0x4A101200u
#define CPSW_WR_C0_TX_EN           (CPSW_WR_BASE + 0x02Cu)
#define CPSW_WR_C0_RX_EN           (CPSW_WR_BASE + 0x030u)

/* ============================================================
 * STATERAM
 * ============================================================ */
#define CPSW_STATERAM_BASE          0x4A100A00u
#define STATERAM_TX0_HDP           (CPSW_STATERAM_BASE + 0x000u)
#define STATERAM_RX0_HDP           (CPSW_STATERAM_BASE + 0x020u)
#define STATERAM_TX0_CP            (CPSW_STATERAM_BASE + 0x040u)
#define STATERAM_RX0_CP            (CPSW_STATERAM_BASE + 0x060u)

/* ============================================================
 * CPPI_RAM — Buffer Descriptor Memory
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

/* BD flag bits — verified against Linux davinci_cpdma.c
 * Linux: SOP=BIT(31), EOP=BIT(30), OWNER=BIT(29), EOQ=BIT(28)
 * Previous VinixOS had OWNER=bit31, SOP=bit27, EOP=bit26 — WRONG!
 * That caused CPDMA to see OWNER=0 at bit29 → DMASTATUS=0x2000
 * "Ownership bit not set" error on every RX enable. */
#define BD_SOP    (1u << 31) /* Start of packet */
#define BD_EOP    (1u << 30) /* End of packet */
#define BD_OWNER  (1u << 29) /* 1 = owned by DMA, 0 = owned by CPU */
#define BD_EOQ    (1u << 28) /* DMA sets when next=0 and chain stalls */

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
 * ALE
 * ============================================================ */
#define CPSW_ALE_BASE               0x4A100D00u
#define ALE_CONTROL                (CPSW_ALE_BASE + 0x008u)
#define ALE_PORTCTL0               (CPSW_ALE_BASE + 0x040u)
#define ALE_PORTCTL1               (CPSW_ALE_BASE + 0x044u)
#define ALE_ENABLE                 (1u << 31)
#define ALE_CLEAR_TABLE            (1u << 30)
#define ALE_BYPASS                 (1u << 4)
#define ALE_PORT_FORWARD            0x3u

/* IRQ number — AM335x TRM Table 6-1 */
#define IRQ_CPSW_RX                 41u

#define RESET_TIMEOUT               10000

/* ============================================================
 * Static state
 * ============================================================ */
static uint8_t s_mac[CPSW_MAC_LEN] = { 0xDE, 0xAD, 0xBE, 0xEF, 0x00, 0x01 };
static void (*s_rx_cb)(const uint8_t *buf, uint16_t len);

/* Flag set by ISR, consumed by cpsw_rx_poll() */
static volatile uint8_t s_rx_pending = 0;

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
    mmio_write32(CONF_GMII_SEL, GMII1_SEL_MII);
    mmio_write32(CONF_MII1_TX_EN,  PIN_TX);
    mmio_write32(CONF_MII1_TXD3,   PIN_TX);
    mmio_write32(CONF_MII1_TXD2,   PIN_TX);
    mmio_write32(CONF_MII1_TXD1,   PIN_TX);
    mmio_write32(CONF_MII1_TXD0,   PIN_TX);
    mmio_write32(CONF_MII1_TX_CLK, PIN_RX);  /* PHY drives TX_CLK in MII */
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

    for (int i = 0; i < 8; i = i + 1) {
        mmio_write32(STATERAM_TX0_HDP + (uint32_t)(i * 4), 0);
        mmio_write32(STATERAM_RX0_HDP + (uint32_t)(i * 4), 0);
        mmio_write32(STATERAM_TX0_CP  + (uint32_t)(i * 4), 0);
        mmio_write32(STATERAM_RX0_CP  + (uint32_t)(i * 4), 0);
    }

    mmio_write32(CPDMA_RX_BUFFER_OFFSET, 0);
    mmio_write32(CPDMA_TX_INTMASK_CLEAR, 0xFFu);
    mmio_write32(CPDMA_RX_INTMASK_CLEAR, 0xFFu);

    /* Disable TX interrupt pacing — TX still polling */
    mmio_write32(CPSW_WR_C0_TX_EN, 0);

    /* Enable TX channel */
    mmio_write32(CPDMA_TX_CONTROL, CPDMA_TX_EN);

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

    /* RX BD: arm — OWNER=1 means DMA owns this BD */
    mmio_write32(RX_BD_PA + BD_OFF_NEXT,   0);
    mmio_write32(RX_BD_PA + BD_OFF_BUFPTR, RX_BUF_PA);
    mmio_write32(RX_BD_PA + BD_OFF_BUFLEN, (uint32_t)CPSW_FRAME_MAXLEN);
    mmio_write32(RX_BD_PA + BD_OFF_FLAGS,
                 BD_OWNER | BD_SOP | BD_EOP | (uint32_t)CPSW_FRAME_MAXLEN);

    /* Kick HDP — tell CPDMA where the first BD is.
     * DO NOT enable RX_CONTROL here.
     * RX_CONTROL is enabled in cpsw_rx_irq_enable() AFTER the IRQ
     * handler is registered. This prevents the race condition where
     * CPDMA reads the BD before the write has propagated through the
     * L3/L4 interconnect to CPPI_RAM (root cause of DMASTATUS=0x2000).
     * The IRQ handler writes EOI immediately, so CPDMA never stalls. */
    mmio_write32(STATERAM_RX0_HDP, RX_BD_PA);

    uart_printf("[CPSW] BD init done, HDP kicked, waiting for IRQ enable\n");
}

/* ============================================================
 * RX Interrupt Handler — mirrors Linux cpsw_rx_interrupt()
 * ------------------------------------------------------------
 * Linux sequence (cpsw_priv.c):
 *   1. writel(0, rx_en)           — disable interrupt pacing
 *   2. cpdma_ctlr_eoi(EOI_RX)    — write EOI to CPDMA
 *   3. napi_schedule()            — defer frame processing
 *
 * VinixOS equivalent:
 *   1. Disable CPSW_WR RX interrupt pacing
 *   2. Write EOI to CPDMA_EOI_VECTOR  ← KEY: ngay trong ISR
 *   3. Set s_rx_pending flag          ← defer to cpsw_rx_poll()
 *
 * Writing EOI ngay trong ISR là điểm mấu chốt:
 * CPDMA nhận EOI → clear error state → không bị stuck.
 * ============================================================ */
static void cpsw_rx_isr(void *data)
{
    (void)data;
    /* NO uart_printf in ISR — must be short */
    mmio_write32(CPSW_WR_C0_RX_EN, 0);
    mmio_write32(CPDMA_EOI_VECTOR, CPDMA_EOI_RX);
    s_rx_pending = 1;
    
    /* Debug: Set flag to indicate ISR was called */
    static volatile uint32_t rx_isr_count = 0;
    rx_isr_count++;
}

static void cpsw_rx_irq_init(void)
{
    /* Stop MAC from receiving frames during CPDMA re-init.
     * This prevents frames arriving while CPDMA is being reset,
     * which would cause RX_HOST_ERR immediately after enable. */
    mmio_write32(SL1_MACCONTROL, MAC_FULLDUPLEX);  /* Clear GMII_EN */

    /* Full CPDMA reset */
    mmio_write32(CPDMA_RX_CONTROL, 0);
    mmio_write32(CPDMA_TX_CONTROL, 0);

    mmio_write32(CPDMA_SOFT_RESET, CPDMA_SOFT_RESET_BIT);
    {
        int t = RESET_TIMEOUT;
        while ((mmio_read32(CPDMA_SOFT_RESET) & CPDMA_SOFT_RESET_BIT) && t > 0)
            t--;
    }

    /* Clear all HDP/CP */
    {
        int i;
        for (i = 0; i < 8; i++) {
            mmio_write32(STATERAM_TX0_HDP + (uint32_t)(i * 4), 0);
            mmio_write32(STATERAM_RX0_HDP + (uint32_t)(i * 4), 0);
            mmio_write32(STATERAM_TX0_CP  + (uint32_t)(i * 4), 0);
            mmio_write32(STATERAM_RX0_CP  + (uint32_t)(i * 4), 0);
        }
    }

    /* Re-apply config */
    mmio_write32(CPDMA_RX_BUFFER_OFFSET, 0);
    mmio_write32(CPDMA_TX_INTMASK_CLEAR, 0xFFu);
    mmio_write32(CPDMA_RX_INTMASK_CLEAR, 0xFFu);
    mmio_write32(CPSW_WR_C0_TX_EN, 0);
    mmio_write32(CPDMA_TX_CONTROL, CPDMA_TX_EN);

    /* Clear DMASTATUS error bits — these persist across soft reset!
     * TRM §14.3.1.4: write 1 to clear RX_HOST_ERR bits [16:13] */
    mmio_write32(CPDMA_DMASTATUS, 0x0001E000u);
    uart_printf("[CPSW] DMASTATUS after clear = 0x%08x\n",
                mmio_read32(CPDMA_DMASTATUS));

    /* Re-arm RX BD */
    mmio_write32(RX_BD_PA + BD_OFF_NEXT,   0);
    mmio_write32(RX_BD_PA + BD_OFF_BUFPTR, RX_BUF_PA);
    mmio_write32(RX_BD_PA + BD_OFF_BUFLEN, (uint32_t)CPSW_FRAME_MAXLEN);
    mmio_write32(RX_BD_PA + BD_OFF_FLAGS,
                 BD_OWNER | BD_SOP | BD_EOP | (uint32_t)CPSW_FRAME_MAXLEN);

    /* Force write buffer flush by reading back — ensures CPPI_RAM
     * has the actual value before CPDMA reads it.
     * On AM335x, Strongly Ordered does NOT prevent CPU write buffer.
     * A read-back forces the write to complete before the read returns. */
    {
        volatile uint32_t dummy;
        dummy = mmio_read32(RX_BD_PA + BD_OFF_FLAGS);
        dummy = mmio_read32(RX_BD_PA + BD_OFF_BUFPTR);
        dummy = mmio_read32(RX_BD_PA + BD_OFF_BUFLEN);
        dummy = mmio_read32(RX_BD_PA + BD_OFF_NEXT);
        (void)dummy;
    }
    __asm__ volatile("dsb" ::: "memory");
    __asm__ volatile("isb" ::: "memory");

    uart_printf("[CPSW] BD verified: FLAGS=0x%08x BUFPTR=0x%08x\n",
                mmio_read32(RX_BD_PA + BD_OFF_FLAGS),
                mmio_read32(RX_BD_PA + BD_OFF_BUFPTR));

    /* Kick HDP */
    mmio_write32(STATERAM_RX0_HDP, RX_BD_PA);

    /* Enable CPDMA RX interrupt */
    mmio_write32(CPDMA_RX_INTMASK_SET, 0x1u);

    /* Enable RX channel */
    mmio_write32(CPDMA_RX_CONTROL, CPDMA_RX_EN);

    /* Enable CPSW_WR interrupt pacing */
    mmio_write32(CPSW_WR_C0_RX_EN, 0x1u);

    /* Re-enable MAC — now CPDMA is ready to receive */
    mmio_write32(SL1_MACCONTROL, MAC_FULLDUPLEX | MAC_GMII_EN);

    /* Log state */
    uart_printf("[CPSW] DMASTATUS after RX enable = 0x%08x\n",
                mmio_read32(CPDMA_DMASTATUS));
    uart_printf("[CPSW] RX0_HDP = 0x%08x  BD_FLAGS = 0x%08x\n",
                mmio_read32(STATERAM_RX0_HDP),
                mmio_read32(RX_BD_PA + BD_OFF_FLAGS));

    /* Register ISR */
    irq_register_handler(IRQ_CPSW_RX, cpsw_rx_isr, 0);
    intc_enable_irq(IRQ_CPSW_RX);

    uart_printf("[CPSW] RX interrupt enabled (IRQ %u)\n", IRQ_CPSW_RX);
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
    uart_printf("[CPSW] init complete\n");
    return 0;
}

/* ============================================================
 * cpsw_rx_irq_enable — gọi sau irq_init() trong main.c
 * ------------------------------------------------------------
 * Tách ra khỏi cpsw_init() vì IRQ framework (irq_init) phải
 * chạy trước khi register handler.
 * ============================================================ */
void cpsw_rx_irq_enable(void)
{
    cpsw_rx_irq_init();
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

/* ============================================================
 * cpsw_rx_poll — called from idle_task after WFI wakeup
 * ------------------------------------------------------------
 * Mirrors Linux NAPI poll: only process frame when ISR signals.
 * ISR already wrote EOI → CPDMA is ready to accept next frame.
 * ============================================================ */
void cpsw_rx_poll(void)
{
    /* Debug: Check DMA status */
    uint32_t dmastat = mmio_read32(CPDMA_DMASTATUS);
    static uint32_t last_dmastat = 0;
    if (dmastat != last_dmastat) {
        uart_printf("[CPSW] DMASTATUS change: 0x%08x -> 0x%08x\n", last_dmastat, dmastat);
        last_dmastat = dmastat;
    }
    
    /* Primary path: ISR signaled a frame is ready */
    if (s_rx_pending) {
        uart_printf("[CPSW] RX: ISR signaled frame ready\n");
        s_rx_pending = 0;
        goto process_frame;
    }

    /* Fallback path: ISR not fired, check BD directly. */
    {
        if (dmastat & (1u << 13)) {
            uart_printf("[CPSW] RX: RX_HOST_ERR detected\n");
            return;  /* RX_HOST_ERR — silent, no spam */
        }
    }

    {
        uint32_t flags = mmio_read32(RX_BD_PA + BD_OFF_FLAGS);
        if (flags & BD_OWNER) {
            static uint32_t poll_count = 0;
            poll_count++;
            if (poll_count % 1000 == 0) {
                uart_printf("[CPSW] RX: No frame ready (poll %u, flags=0x%08x)\n", poll_count, flags);
            }
            return;  /* No frame ready */
        }

        /* Frame arrived but ISR was not called — process silently */
        uart_printf("[CPSW] RX: Frame arrived without ISR (flags=0x%08x)\n", flags);
        mmio_write32(CPSW_WR_C0_RX_EN, 0);
        mmio_write32(CPDMA_EOI_VECTOR, CPDMA_EOI_RX);
    }

process_frame:
    {
        uint32_t flags = mmio_read32(RX_BD_PA + BD_OFF_FLAGS);
        if (flags & BD_OWNER) {
            uart_printf("[CPSW] RX: BD still owned by CPDMA (flags=0x%08x)\n", flags);
            mmio_write32(CPSW_WR_C0_RX_EN, 0x1u);
            return;
        }

        uint16_t len = (uint16_t)(flags & 0xFFFFu);
        uart_printf("[CPSW] RX: Processing frame, len=%u, flags=0x%08x\n", len, flags);

        if (s_rx_cb && len > 0) {
            uart_printf("[CPSW] RX: Calling callback with %u bytes\n", len);
            s_rx_cb((const uint8_t *)RX_BUF_PA, len);
        } else {
            uart_printf("[CPSW] RX: No callback or len=0\n");
        }

        mmio_write32(STATERAM_RX0_CP, RX_BD_PA);

        mmio_write32(RX_BD_PA + BD_OFF_NEXT,   0);
        mmio_write32(RX_BD_PA + BD_OFF_BUFPTR, RX_BUF_PA);
        mmio_write32(RX_BD_PA + BD_OFF_BUFLEN, (uint32_t)CPSW_FRAME_MAXLEN);
        mmio_write32(RX_BD_PA + BD_OFF_FLAGS,
                     BD_OWNER | BD_SOP | BD_EOP | (uint32_t)CPSW_FRAME_MAXLEN);
        mmio_write32(STATERAM_RX0_HDP, RX_BD_PA);

        mmio_write32(CPSW_WR_C0_RX_EN, 0x1u);
        
        uart_printf("[CPSW] RX: Frame processed, RX re-enabled\n");
    }
}
