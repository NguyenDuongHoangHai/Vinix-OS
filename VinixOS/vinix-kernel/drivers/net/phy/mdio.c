/* ============================================================
 * drivers/net/phy/mdio.c
 * ------------------------------------------------------------
 * AM335x MDIO controller — PHY management at 2.5 MHz.
 * ============================================================ */

#include "vinix/init.h"
#include "platform_device.h"
#include "mmio.h"
#include "uart.h"
#include "mdio.h"

/* MDIO register offsets from MDIO base (AM335x TRM Ch.14) */
#define MDIO_CONTROL        0x04
#define MDIO_ALIVE          0x08
#define MDIO_LINK           0x0C
#define MDIO_USERACCESS0    0x80
#define MDIO_USERPHYSEL0    0x84

/* MDIO_CONTROL bits */
#define MDIO_CTRL_CLKDIV(n) ((n) << 16)   /* bits 25:16 */
#define MDIO_CTRL_ENABLE    (1u << 30)     /* enable MDIO polling */

/* Clock divider: MDIO_CLK = SYSCLK / (2*(CLKDIV+1)).
 * 125 MHz / (2 * (24+1)) = 2.5 MHz — AM335x TRM Ch.14 */
#define MDIO_CLKDIV_2_5MHZ  24

/* MDIO_USERACCESS0 bits */
#define MDIO_ACC_GO         (1u << 31)           /* start transaction */
#define MDIO_ACC_WRITE      (1u << 30)           /* 1=write 0=read */
#define MDIO_ACC_ACK        (1u << 29)           /* PHY acknowledged */
#define MDIO_ACC_REGADR(r)  (((r) & 0x1F) << 21)
#define MDIO_ACC_PHYADR(p)  (((p) & 0x1F) << 16)
#define MDIO_ACC_DATA(d)    ((d) & 0xFFFF)

/* IEEE 802.3 PHY registers */
#define PHY_REG_BMCR        0   /* Basic Mode Control */
#define PHY_REG_BMSR        1   /* Basic Mode Status */
#define PHY_BMCR_RESET      (1u << 15)
#define PHY_BMSR_LINK_UP    (1u << 2)
#define PHY_BMSR_ANCOMP     (1u << 5)

/* PRCM — enable CPSW/MDIO clock (AM335x TRM Ch.8) */
#define CM_PER_BASE              0x44E00000u
#define CM_PER_CPGMAC0_CLKCTRL  0x14
#define CM_PER_CPSW_CLKSTCTRL   0x144
#define CLKCTRL_MODULEMODE_EN   0x2u
#define CLKSTCTRL_SW_WKUP       0x2u
#define CLKCTRL_IDLEST_MASK     (0x3u << 16)

static uint32_t s_base;
static int      s_phy_addr = -1;

static void mdio_wait_go(void)
{
    int i = 100000;
    while ((mmio_read32(s_base + MDIO_USERACCESS0) & MDIO_ACC_GO) && --i)
        ;
}

int mdio_phy_read(uint8_t phy_addr, uint8_t reg)
{
    if (s_base == 0)
        return -1;

    mdio_wait_go();
    mmio_write32(s_base + MDIO_USERACCESS0,
                 MDIO_ACC_GO |
                 MDIO_ACC_REGADR(reg) |
                 MDIO_ACC_PHYADR(phy_addr));
    mdio_wait_go();

    uint32_t val = mmio_read32(s_base + MDIO_USERACCESS0);
    if (!(val & MDIO_ACC_ACK))
        return -1;
    return (int)(val & 0xFFFF);
}

void mdio_phy_write(uint8_t phy_addr, uint8_t reg, uint16_t data)
{
    if (s_base == 0)
        return;

    mdio_wait_go();
    mmio_write32(s_base + MDIO_USERACCESS0,
                 MDIO_ACC_GO |
                 MDIO_ACC_WRITE |
                 MDIO_ACC_REGADR(reg) |
                 MDIO_ACC_PHYADR(phy_addr) |
                 MDIO_ACC_DATA(data));
    mdio_wait_go();
}

int mdio_link_up(void)
{
    if (s_phy_addr < 0)
        return 0;
    int bmsr = mdio_phy_read((uint8_t)s_phy_addr, PHY_REG_BMSR);
    if (bmsr < 0)
        return 0;
    return (bmsr & PHY_BMSR_LINK_UP) ? 1 : 0;
}

static void clock_enable(void)
{
    mmio_write32(CM_PER_BASE + CM_PER_CPSW_CLKSTCTRL, CLKSTCTRL_SW_WKUP);
    mmio_write32(CM_PER_BASE + CM_PER_CPGMAC0_CLKCTRL, CLKCTRL_MODULEMODE_EN);

    int i = 100000;
    while ((mmio_read32(CM_PER_BASE + CM_PER_CPGMAC0_CLKCTRL) & CLKCTRL_IDLEST_MASK)
           && --i)
        ;
}

static int mdio_probe(struct platform_device *pdev)
{
    struct resource *mem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
    if (!mem) {
        pr_err("[MDIO] no memory resource\n");
        return -1;
    }

    clock_enable();
    s_base = mem->start;

    /* Enable MDIO at 2.5 MHz — AM335x TRM Ch.14 */
    mmio_write32(s_base + MDIO_CONTROL,
                 MDIO_CTRL_ENABLE | MDIO_CTRL_CLKDIV(MDIO_CLKDIV_2_5MHZ));

    /* Wait for MDIO to scan and find a live PHY */
    int wait = 100000;
    while (!mmio_read32(s_base + MDIO_ALIVE) && --wait)
        ;

    uint32_t alive = mmio_read32(s_base + MDIO_ALIVE);
    if (!alive) {
        pr_err("[MDIO] no PHY found\n");
        return -1;
    }

    /* Use lowest-numbered alive PHY */
    for (int a = 0; a < 32; a++) {
        if (alive & (1u << a)) {
            s_phy_addr = a;
            break;
        }
    }

    pr_info("[MDIO] PHY found at addr %d\n", s_phy_addr);

    /* Reset PHY and wait for autoneg */
    mdio_phy_write((uint8_t)s_phy_addr, PHY_REG_BMCR, PHY_BMCR_RESET);
    int t = 200000;
    while ((mdio_phy_read((uint8_t)s_phy_addr, PHY_REG_BMCR) & PHY_BMCR_RESET)
           && --t)
        ;

    /* Wait for autoneg completion */
    t = 2000000;
    while (!(mdio_phy_read((uint8_t)s_phy_addr, PHY_REG_BMSR) & PHY_BMSR_ANCOMP)
           && --t)
        ;

    if (mdio_link_up())
        pr_info("[MDIO] link up\n");
    else
        pr_info("[MDIO] link down (cable?)\n");

    return 0;
}

static int mdio_remove(struct platform_device *pdev)
{
    (void)pdev;
    s_base = 0;
    return 0;
}

static struct platform_driver mdio_driver = {
    .drv   = { .name = "omap-mdio" },
    .probe  = mdio_probe,
    .remove = mdio_remove,
};
module_platform_driver(mdio_driver);
