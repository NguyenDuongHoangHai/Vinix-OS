/* ============================================================
 * mdio.h
 * ------------------------------------------------------------
 * MDIO controller public API — PHY management bus.
 * ============================================================ */

#ifndef MDIO_H
#define MDIO_H

#include "types.h"

int  mdio_phy_read(uint8_t phy_addr, uint8_t reg);
void mdio_phy_write(uint8_t phy_addr, uint8_t reg, uint16_t val);
int  mdio_link_up(void);

#endif /* MDIO_H */
