/* ============================================================
 * drivers/net/ethernet/ti/cpsw.c
 * ------------------------------------------------------------
 * TI AM335x CPSW 3G Ethernet driver — single-port RGMII.
 * ============================================================ */

#include "vinix/init.h"
#include "linux/netdevice.h"
#include "linux/etherdevice.h"
#include "linux/skbuff.h"
#include "platform_device.h"
#include "mmio.h"
#include "irq.h"
#include "uart.h"
#include "mdio.h"
#include "slab.h"
#include "cpsw.h"

/* ── CPSW register block offsets (AM335x TRM Ch.14) ────────────────── */

/* Switch Subsystem (SS) — base + 0x000 */
#define SS_SOFT_RESET       0x008
#define SS_STAT_PORT_EN     0x00C

/* CPGMAC_SL port blocks */
#define SL0_BASE            0x100      /* MAC port 0 (external) */
#define SL_MACCONTROL       0x004
#define SL_SOFT_RESET       0x00C
#define SL_RX_MAXLEN        0x010
#define SL_SA_LO            0x020      /* source MAC bytes 5..2 */
#define SL_SA_HI            0x024      /* source MAC bytes 1..0 */

/* CPSW Wrapper (WR) — base + 0x500 */
#define WR_BASE             0x500
#define WR_C0_RX_EN         0x014      /* core 0 RX interrupt enable */
#define WR_C0_TX_EN         0x018      /* core 0 TX interrupt enable */

/* Host port (P0) — base + 0x600 */
#define P0_BASE             0x600
#define P0_CONTROL          0x000

/* MAC port 1 (P1) — base + 0x700 */
#define P1_BASE             0x700
#define P1_CONTROL          0x000

/* CPDMA — base + 0x800 */
#define CPDMA_BASE          0x800
#define CPDMA_TX_CONTROL    0x004
#define CPDMA_RX_CONTROL    0x014
#define CPDMA_SOFT_RESET    0x01C
#define CPDMA_DMACONTROL    0x020
#define CPDMA_RX_BUF_OFFSET 0x028
#define CPDMA_TX_INTMASK_SET 0x088
#define CPDMA_RX_INTMASK_SET 0x0A8
#define CPDMA_EOI_VECTOR    0x094

/* CPDMA channel head-descriptor and completion pointers */
#define CPDMA_TX0_HDP       0xA00      /* TX channel 0 head desc ptr */
#define CPDMA_RX0_HDP       0xA20      /* RX channel 0 head desc ptr */
#define CPDMA_TX0_CP        0xA40      /* TX channel 0 completion ptr */
#define CPDMA_RX0_CP        0xA60      /* RX channel 0 completion ptr */

/* ALE — base + 0xD00 */
#define ALE_BASE            0xD00
#define ALE_CONTROL         0x008
#define ALE_PORTCTL0        0x040      /* host port */
#define ALE_PORTCTL1        0x044      /* MAC port 1 */
#define ALE_PORTCTL2        0x048      /* MAC port 2 */

/* ALE_CONTROL bits */
#define ALE_EN              (1u << 31)
#define ALE_CLEAR           (1u << 30)
#define ALE_BYPASS          (1u << 4)

/* ALE port state: FORWARD = 0x3 */
#define ALE_PORT_FWD        0x3u

/* CPDMA descriptor flags (CPPI format, AM335x TRM Ch.14) */
#define DESC_FLAG_SOP       (1u << 31)
#define DESC_FLAG_EOP       (1u << 30)
#define DESC_FLAG_OWN       (1u << 29)   /* 1 = DMA owns, 0 = host owns */
#define DESC_FLAG_EOQ       (1u << 28)

/* Control Module — pin mux and MAC fuse (AM335x TRM Ch.9) */
#define CTRL_BASE           0x44E10000u
#define CTRL_GMII_SEL       0x650       /* 0x10 = RGMII mode for port 1 */
#define CTRL_MAC0_LO        0x630
#define CTRL_MAC0_HI        0x634

/* MACCONTROL bits */
#define MACCTRL_GMII_EN     (1u << 5)
#define MACCTRL_FULLDUPLEX  (1u << 0)

/* Kernel VA ↔ PA: kernel DDR = VA 0xC0000000 → PA 0x80000000 */
#define KERN_VA_TO_PA(va)   ((uint32_t)(va) - 0x40000000u)
#define PA_TO_KERN_VA(pa)   ((uint32_t)(pa) + 0x40000000u)

/* RX ring depth — must be power of 2 */
#define RX_RING_SIZE    4
#define RX_BUF_SIZE     2048

/* CPPI buffer descriptor */
struct cppi_desc {
    volatile uint32_t next;      /* PA of next descriptor, 0 = end */
    volatile uint32_t buf_pa;    /* PA of packet buffer */
    volatile uint32_t buf_len;   /* buffer capacity */
    volatile uint32_t flags;     /* SOP/EOP/OWN/EOQ + pkt length */
} __attribute__((packed, aligned(16)));

/* Per-device private state */
struct cpsw_priv {
    uint32_t base;
    int      rx_irq;

    struct cppi_desc  rx_desc[RX_RING_SIZE] __attribute__((aligned(16)));
    uint8_t           rx_buf[RX_RING_SIZE][RX_BUF_SIZE];

    struct cppi_desc  tx_desc __attribute__((aligned(16)));

    struct net_device *ndev;
};

/* ── Register helpers ───────────────────────────────────────────────── */

static inline void reg_wr(uint32_t base, uint32_t off, uint32_t val)
{
    mmio_write32(base + off, val);
}

static inline uint32_t reg_rd(uint32_t base, uint32_t off)
{
    return mmio_read32(base + off);
}

static void wait_reset(uint32_t base, uint32_t off)
{
    int i = 200000;
    while ((reg_rd(base, off) & 1u) && --i)
        ;
}

/* ── MAC address ────────────────────────────────────────────────────── */

static void read_mac_from_fuse(uint8_t mac[6])
{
    /* AM335x TRM Ch.9 — Control Module MACID0 fuse registers */
    uint32_t lo = mmio_read32(CTRL_BASE + CTRL_MAC0_LO);
    uint32_t hi = mmio_read32(CTRL_BASE + CTRL_MAC0_HI);

    mac[0] = (hi >>  0) & 0xFF;
    mac[1] = (hi >>  8) & 0xFF;
    mac[2] = (lo >> 24) & 0xFF;
    mac[3] = (lo >> 16) & 0xFF;
    mac[4] = (lo >>  8) & 0xFF;
    mac[5] = (lo >>  0) & 0xFF;
}

/* ── CPDMA ring setup ───────────────────────────────────────────────── */

static void rx_ring_init(struct cpsw_priv *priv)
{
    for (int i = 0; i < RX_RING_SIZE; i++) {
        struct cppi_desc *d = &priv->rx_desc[i];
        int next = (i + 1) % RX_RING_SIZE;

        d->next   = KERN_VA_TO_PA(&priv->rx_desc[next]);
        d->buf_pa = KERN_VA_TO_PA(priv->rx_buf[i]);
        d->buf_len = RX_BUF_SIZE;
        d->flags   = DESC_FLAG_OWN;   /* DMA owns all descriptors */
    }
    /* Last descriptor links back → circular ring */

    /* Hand ring to CPDMA */
    reg_wr(priv->base, CPDMA_RX0_HDP, KERN_VA_TO_PA(&priv->rx_desc[0]));
}

/* ── Hardware init sequence ─────────────────────────────────────────── */

static void cpsw_hw_init(struct cpsw_priv *priv, const uint8_t mac[6])
{
    uint32_t b = priv->base;

    /* RGMII mode for port 1 — AM335x TRM Ch.9 GMII_SEL */
    mmio_write32(CTRL_BASE + CTRL_GMII_SEL, 0x10u);   /* port1 = RGMII */

    /* Soft reset: CPDMA first, then CPSW SS */
    reg_wr(b, CPDMA_BASE + CPDMA_SOFT_RESET, 1u);
    wait_reset(b, CPDMA_BASE + CPDMA_SOFT_RESET);

    reg_wr(b, SS_SOFT_RESET, 1u);
    wait_reset(b, SS_SOFT_RESET);

    reg_wr(b, SL0_BASE + SL_SOFT_RESET, 1u);
    wait_reset(b, SL0_BASE + SL_SOFT_RESET);

    /* ALE: clear table + enable + bypass (forward all to host) */
    reg_wr(b, ALE_BASE + ALE_CONTROL, ALE_CLEAR);
    reg_wr(b, ALE_BASE + ALE_CONTROL, ALE_EN | ALE_BYPASS);
    reg_wr(b, ALE_BASE + ALE_PORTCTL0, ALE_PORT_FWD);
    reg_wr(b, ALE_BASE + ALE_PORTCTL1, ALE_PORT_FWD);

    /* Enable statistics on all ports */
    reg_wr(b, SS_STAT_PORT_EN, 0x7u);

    /* MAC port 1 config */
    reg_wr(b, SL0_BASE + SL_RX_MAXLEN, 1518u);
    reg_wr(b, SL0_BASE + SL_MACCONTROL, MACCTRL_GMII_EN | MACCTRL_FULLDUPLEX);

    /* Write MAC address into port 1 SL registers */
    reg_wr(b, SL0_BASE + SL_SA_HI,
           ((uint32_t)mac[1] << 8) | mac[0]);
    reg_wr(b, SL0_BASE + SL_SA_LO,
           ((uint32_t)mac[2] << 24) | ((uint32_t)mac[3] << 16) |
           ((uint32_t)mac[4] <<  8) |  (uint32_t)mac[5]);

    /* Host port enable */
    reg_wr(b, P0_BASE + P0_CONTROL, 0x3u);
    reg_wr(b, P1_BASE + P1_CONTROL, 0x3u);

    /* CPDMA: zero all channel HDP/CP before starting */
    for (int ch = 0; ch < 8; ch++) {
        reg_wr(b, CPDMA_TX0_HDP + ch * 4, 0u);
        reg_wr(b, CPDMA_RX0_HDP + ch * 4, 0u);
        reg_wr(b, CPDMA_TX0_CP  + ch * 4, 0u);
        reg_wr(b, CPDMA_RX0_CP  + ch * 4, 0u);
    }

    reg_wr(b, CPDMA_BASE + CPDMA_RX_BUF_OFFSET, 0u);
    reg_wr(b, CPDMA_BASE + CPDMA_TX_CONTROL, 0x1u);   /* ch 0 */
    reg_wr(b, CPDMA_BASE + CPDMA_RX_CONTROL, 0x1u);
    reg_wr(b, CPDMA_BASE + CPDMA_DMACONTROL, 0x3u);   /* TX_EN | RX_EN */

    /* Enable RX interrupt for channel 0 on core 0 */
    reg_wr(b, WR_BASE + WR_C0_RX_EN, 0x1u);
    reg_wr(b, CPDMA_BASE + CPDMA_RX_INTMASK_SET, 0x1u);

    rx_ring_init(priv);
}

/* ── ISR ────────────────────────────────────────────────────────────── */

static void cpsw_rx_isr(void *data)
{
    struct net_device *ndev = (struct net_device *)data;
    struct cpsw_priv  *priv = (struct cpsw_priv *)netdev_priv(ndev);
    uint32_t b = priv->base;

    /* Process all descriptors the DMA has completed (OWN cleared) */
    for (int i = 0; i < RX_RING_SIZE; i++) {
        struct cppi_desc *d = &priv->rx_desc[i];

        if (d->flags & DESC_FLAG_OWN)
            continue;   /* DMA still owns this one */

        uint32_t pkt_len = d->flags & 0x7FFu;
        if (pkt_len == 0 || pkt_len > RX_BUF_SIZE)
            goto replenish;

        struct sk_buff *skb = alloc_skb(pkt_len);
        if (skb) {
            uint8_t *src = (uint8_t *)PA_TO_KERN_VA(d->buf_pa);
            unsigned char *dst = skb_put(skb, pkt_len);
            for (uint32_t j = 0; j < pkt_len; j++)
                dst[j] = src[j];
            skb->dev = ndev;
            netif_rx(skb);
        }

replenish:
        /* Return descriptor to DMA */
        d->buf_pa  = KERN_VA_TO_PA(priv->rx_buf[i]);
        d->buf_len = RX_BUF_SIZE;
        d->flags   = DESC_FLAG_OWN;

        /* Acknowledge completion to CPDMA */
        reg_wr(b, CPDMA_RX0_CP, KERN_VA_TO_PA(d));
    }

    /* RX EOI — allows next interrupt */
    reg_wr(b, CPDMA_BASE + CPDMA_EOI_VECTOR, 1u);
}

/* ── net_device_ops ─────────────────────────────────────────────────── */

static int cpsw_ndo_open(struct net_device *dev)
{
    struct cpsw_priv *priv = netdev_priv(dev);

    if (mdio_link_up()) {
        netif_carrier_on(dev);
        pr_info("[CPSW] %s link up\n", dev->name);
    } else {
        netif_carrier_off(dev);
        pr_info("[CPSW] %s no link\n", dev->name);
    }

    request_irq(priv->rx_irq, cpsw_rx_isr, 0, "cpsw-rx", dev);
    return 0;
}

static int cpsw_ndo_stop(struct net_device *dev)
{
    struct cpsw_priv *priv = netdev_priv(dev);
    free_irq(priv->rx_irq, dev);
    netif_carrier_off(dev);
    return 0;
}

static int cpsw_ndo_xmit(struct sk_buff *skb, struct net_device *dev)
{
    struct cpsw_priv *priv = netdev_priv(dev);
    uint32_t b = priv->base;

    if (!skb || skb->len == 0) {
        kfree_skb(skb);
        return 0;
    }

    /* Wait for previous TX to finish */
    int wait = 100000;
    while ((priv->tx_desc.flags & DESC_FLAG_OWN) && --wait)
        ;

    /* Copy frame into static TX buffer (avoids DMA lifetime issues) */
    uint8_t *txbuf = (uint8_t *)PA_TO_KERN_VA(priv->tx_desc.buf_pa);
    for (uint32_t i = 0; i < skb->len; i++)
        txbuf[i] = skb->data[i];

    priv->tx_desc.next    = 0;
    priv->tx_desc.buf_len = skb->len;
    priv->tx_desc.flags   = DESC_FLAG_SOP | DESC_FLAG_EOP |
                             DESC_FLAG_OWN | (skb->len & 0x7FFu);

    reg_wr(b, CPDMA_TX0_HDP, KERN_VA_TO_PA(&priv->tx_desc));

    kfree_skb(skb);
    return 0;
}

static const struct net_device_ops cpsw_netdev_ops = {
    .ndo_open            = cpsw_ndo_open,
    .ndo_stop            = cpsw_ndo_stop,
    .ndo_start_xmit      = cpsw_ndo_xmit,
    .ndo_get_stats       = 0,
    .ndo_set_mac_address = 0,
};

/* ── Platform driver probe ──────────────────────────────────────────── */

static int cpsw_probe(struct platform_device *pdev)
{
    struct resource *mem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
    if (!mem) {
        pr_err("[CPSW] no memory resource\n");
        return -1;
    }

    struct net_device *ndev = alloc_etherdev(sizeof(struct cpsw_priv));
    if (!ndev) {
        pr_err("[CPSW] alloc_etherdev failed\n");
        return -1;
    }

    struct cpsw_priv *priv = netdev_priv(ndev);
    priv->base = mem->start;
    priv->ndev = ndev;
    priv->rx_irq = platform_get_irq(pdev, 0);

    /* Allocate a static TX packet buffer (1 page = 4KB) */
    priv->tx_desc.buf_pa  = KERN_VA_TO_PA(priv->rx_buf[0]); /* reuse, overwrite before TX */
    priv->tx_desc.buf_len = 0;
    priv->tx_desc.flags   = 0;
    priv->tx_desc.next    = 0;

    read_mac_from_fuse(ndev->dev_addr);
    pr_info("[CPSW] MAC %02x:%02x:%02x:%02x:%02x:%02x\n",
            ndev->dev_addr[0], ndev->dev_addr[1], ndev->dev_addr[2],
            ndev->dev_addr[3], ndev->dev_addr[4], ndev->dev_addr[5]);

    cpsw_hw_init(priv, ndev->dev_addr);

    /* Give TX its own buffer page — not rx_buf[0] */
    uint8_t *txbuf = kmalloc(2048, GFP_KERNEL);
    if (txbuf)
        priv->tx_desc.buf_pa = KERN_VA_TO_PA(txbuf);

    ndev->netdev_ops = &cpsw_netdev_ops;

    if (register_netdev(ndev) != 0) {
        pr_err("[CPSW] register_netdev failed\n");
        free_netdev(ndev);
        return -1;
    }

    pr_info("[CPSW] probe complete\n");
    return 0;
}

static int cpsw_remove(struct platform_device *pdev)
{
    (void)pdev;
    return 0;
}

static struct platform_driver cpsw_driver = {
    .drv    = { .name = "ti-cpsw" },
    .probe  = cpsw_probe,
    .remove = cpsw_remove,
};
module_platform_driver(cpsw_driver);

int ti_cpsw_driver_register(void)
{
    return platform_driver_register(&cpsw_driver);
}
