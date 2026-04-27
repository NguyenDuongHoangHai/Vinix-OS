/* ============================================================
 * platform_drivers.h
 * ------------------------------------------------------------
 * Platform-driver registration entry points.
 * ============================================================ */

#ifndef PLATFORM_DRIVERS_H
#define PLATFORM_DRIVERS_H

int omap_uart_driver_register(void);
int omap_intc_driver_register(void);
int omap_hsmmc_driver_register(void);

#endif
