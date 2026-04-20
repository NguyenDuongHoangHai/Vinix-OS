/* ============================================================
 * mdio.c
 * ------------------------------------------------------------
 * AM335x MDIO bus + DP83848I PHY bringup.
 * ============================================================ */

#include "types.h"
#include "mmio.h"
#include "uart.h"
#include "syscalls.h"
#include "mdio.h"

/* ============================================================
 * PHẦN 1 — PRCM: Clock Enable
 * AM335x TRM Ch.08
 * ============================================================ */

#define CM_PER_BASE                 0x44E00000
#define CM_PER_CPGMAC0_CLKCTRL     (CM_PER_BASE + 0x014)
/* ================================================== */
/* Fix Bug 01: MDIO idle timeout — Hai Nguyen
 * Thiếu CM_PER_CPSW_CLKSTCTRL SW_WKUP khiến 125MHz functional
 * clock của MDIO state machine bị gated dù CPGMAC0 báo FUNC.
 * TRM Ch.08 — CPSW clock domain phải wake trước khi enable module. */
#define CM_PER_CPSW_CLKSTCTRL      (CM_PER_BASE + 0x144)
#define CLKTRCTRL_SW_WKUP          0x2
#define CLKACT_CPSW_125M           (1U << 4)
/* end Fix Bug 01                                      */
/* ================================================== */
#define MODULEMODE_ENABLE           0x2
#define MODULEMODE_MASK             0x3
#define IDLEST_MASK                 (0x3 << 16)
#define IDLEST_FUNC                 0x0

/* ============================================================
 * PHẦN 2 — PinMux: Control Module
 * AM335x TRM Ch.09
 * ============================================================ */

#define CTRL_BASE                   0x44E10000
#define CONF_MDIO_DATA              (CTRL_BASE + 0x948)
#define CONF_MDIO_CLK               (CTRL_BASE + 0x94C)
#define CONF_MDIO_DATA_VAL          0x30    /* mode0, input-enable, pullup */
#define CONF_MDIO_CLK_VAL           0x00    /* mode0, output only          */

/* ============================================================
 * PHẦN 3 — MDIO Controller Registers
 * AM335x TRM Ch.14, base 0x4A101000
 * ============================================================ */

#define MDIO_BASE                   0x4A101000
#define MDIO_CONTROL                (MDIO_BASE + 0x004)
#define MDIO_ALIVE                  (MDIO_BASE + 0x008)
#define MDIO_USERACCESS0            (MDIO_BASE + 0x080)

#define MDIO_CTRL_ENABLE            (1U << 30)
#define MDIO_CTRL_IDLE              (1U << 31)
/* CLKDIV=49 → MDC = ref_clk / (2*(49+1)) ≤ 2.5 MHz per IEEE 802.3 */
#define MDIO_CTRL_CLKDIV            49U

#define USERACCESS_GO               (1U << 31)
#define USERACCESS_WRITE            (1U << 30)
#define USERACCESS_ACK              (1U << 29)
#define USERACCESS_REGADR_SHIFT     21
#define USERACCESS_PHYADR_SHIFT     16
#define USERACCESS_DATA_MASK        0xFFFF

/* ============================================================
 * PHẦN 4 — IEEE 802.3 MII Register Map
 * ============================================================ */

#define MII_BMCR     0
#define MII_BMSR     1
#define MII_PHYSID1  2
#define MII_PHYSID2  3
#define MII_ANAR     4
#define MII_ANLPAR   5

#define BMCR_ANENABLE    (1 << 12)
#define BMCR_ANRESTART   (1 << 9)
#define BMSR_LINK_UP     (1 << 2)
#define BMSR_ANEG_DONE   (1 << 5)
#define ANAR_ALL         0x01E1

/* ============================================================
 * PHẦN 5 — Static Helpers
 * ============================================================ */

static int mdio_wait_go(void)
{
    uint32_t n = 100000;
    while (mmio_read32(MDIO_USERACCESS0) & USERACCESS_GO) {
        if (--n == 0)
            return E_FAIL;
    }
    return E_OK;
}

static int mdio_wait_idle(void)
{
    uint32_t n = 100000;
    while (!(mmio_read32(MDIO_CONTROL) & MDIO_CTRL_IDLE)) {
        if (--n == 0)
            return E_FAIL;
    }
    return E_OK;
}

/* ============================================================
 * PHẦN 6 — Public Functions
 * ============================================================ */

int mdio_init(void)
{
    /* ================================================== */
    /* Fix Bug 01: wake CPSW clock domain trước enable module — Hai Nguyen */
    mmio_write32(CM_PER_CPSW_CLKSTCTRL, CLKTRCTRL_SW_WKUP);
    uint32_t n = 50000;
    while (!(mmio_read32(CM_PER_CPSW_CLKSTCTRL) & CLKACT_CPSW_125M)) {
        if (--n == 0) {
            uart_printf("[MDIO] CPSW 125MHz clock timeout\n");
            return E_FAIL;
        }
    }
    /* end Fix Bug 01                                      */
    /* ================================================== */

    uint32_t reg = mmio_read32(CM_PER_CPGMAC0_CLKCTRL);
    reg = (reg & ~MODULEMODE_MASK) | MODULEMODE_ENABLE;
    mmio_write32(CM_PER_CPGMAC0_CLKCTRL, reg);

    n = 50000;
    while ((mmio_read32(CM_PER_CPGMAC0_CLKCTRL) & IDLEST_MASK) != IDLEST_FUNC) {
        if (--n == 0) {
            uart_printf("[MDIO] clock enable timeout\n");
            return E_FAIL;
        }
    }

    mmio_write32(CONF_MDIO_DATA, CONF_MDIO_DATA_VAL);
    mmio_write32(CONF_MDIO_CLK,  CONF_MDIO_CLK_VAL);

    mmio_write32(MDIO_CONTROL, MDIO_CTRL_ENABLE | MDIO_CTRL_CLKDIV);

    if (mdio_wait_idle() != E_OK) {
        uart_printf("[MDIO] idle timeout\n");
        return E_FAIL;
    }

    uart_printf("[MDIO] init OK, ALIVE=0x%08x\n", mmio_read32(MDIO_ALIVE));
    return E_OK;
}

int mdio_read(uint8_t phy_addr, uint8_t reg)
{
    if (mdio_wait_go() != E_OK)
        return E_FAIL;

    mmio_write32(MDIO_USERACCESS0,
        USERACCESS_GO |
        (((uint32_t)reg      & 0x1F) << USERACCESS_REGADR_SHIFT) |
        (((uint32_t)phy_addr & 0x1F) << USERACCESS_PHYADR_SHIFT));

    if (mdio_wait_go() != E_OK)
        return E_FAIL;

    uint32_t val = mmio_read32(MDIO_USERACCESS0);
    if (!(val & USERACCESS_ACK))
        return E_FAIL;

    return (int)(val & USERACCESS_DATA_MASK);
}

int mdio_write(uint8_t phy_addr, uint8_t reg, uint16_t data)
{
    if (mdio_wait_go() != E_OK)
        return E_FAIL;

    mmio_write32(MDIO_USERACCESS0,
        USERACCESS_GO    |
        USERACCESS_WRITE |
        (((uint32_t)reg      & 0x1F) << USERACCESS_REGADR_SHIFT) |
        (((uint32_t)phy_addr & 0x1F) << USERACCESS_PHYADR_SHIFT) |
        data);

    return mdio_wait_go();
}

int phy_wait_link(uint8_t phy_addr, phy_info_t *info)
{
    mdio_write(phy_addr, MII_ANAR, ANAR_ALL);
    /* ANRESTART required alongside ANENABLE — ANENABLE alone does not restart */
    mdio_write(phy_addr, MII_BMCR, BMCR_ANENABLE | BMCR_ANRESTART);

    uint32_t n = 10000000;
    while (--n) {
        /* BMSR is latched-low — read twice to get current state */
        mdio_read(phy_addr, MII_BMSR);
        int bmsr = mdio_read(phy_addr, MII_BMSR);
        if (bmsr < 0)
            continue;
        if ((bmsr & BMSR_LINK_UP) && (bmsr & BMSR_ANEG_DONE))
            break;
    }
    if (n == 0)
        return E_FAIL;

    int id1  = mdio_read(phy_addr, MII_PHYSID1);
    int id2  = mdio_read(phy_addr, MII_PHYSID2);
    int anlp = mdio_read(phy_addr, MII_ANLPAR);

    info->addr    = phy_addr;
    info->id      = ((uint32_t)id1 << 16) | (uint32_t)id2;
    info->link_up = 1;
    info->speed   = (anlp & 0x0180) ? 100 : 10;
    info->duplex  = (anlp & 0x0140) ? 1   : 0;

    uart_printf("[MDIO] link UP %d Mbps %s-duplex\n",
                info->speed, info->duplex ? "full" : "half");
    return E_OK;
}
