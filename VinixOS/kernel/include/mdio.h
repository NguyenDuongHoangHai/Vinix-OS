/* ============================================================
 * mdio.h
 * ------------------------------------------------------------
 * AM335x MDIO bus + DP83848I PHY driver interface.
 * ============================================================ */

#ifndef MDIO_H
#define MDIO_H

#include "types.h"
#include "syscalls.h"

typedef struct {
    uint8_t  addr;
    uint32_t id;
    uint8_t  link_up;
    uint16_t speed;
    uint8_t  duplex;
} phy_info_t;

int mdio_init(void);
int mdio_read(uint8_t phy_addr, uint8_t reg);
int mdio_write(uint8_t phy_addr, uint8_t reg, uint16_t val);
int phy_wait_link(uint8_t phy_addr, phy_info_t *info);

#endif /* MDIO_H */
