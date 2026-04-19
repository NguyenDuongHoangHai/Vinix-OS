/* ============================================================
 * platform_drivers.h
 * ------------------------------------------------------------
 * Entry points that register each BBB platform driver with the
 * platform bus. Call order in main.c decides probe sequencing.
 * ============================================================ */

#ifndef PLATFORM_DRIVERS_H
#define PLATFORM_DRIVERS_H

int omap_uart_driver_register(void);
int omap_intc_driver_register(void);
int omap_hsmmc_driver_register(void);
int omap_dmtimer_driver_register(void);

#endif
