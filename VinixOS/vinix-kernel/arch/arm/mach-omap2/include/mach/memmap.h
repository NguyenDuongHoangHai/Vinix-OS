/* ============================================================
 * platform/memmap.h
 * ------------------------------------------------------------
 * AM3358 peripheral memory map.
 * ============================================================ */

#ifndef PLATFORM_MEMMAP_H
#define PLATFORM_MEMMAP_H

#include "types.h"

/* AM335x TRM Ch.02 — peripheral bus base addresses. */
#define PLATFORM_PERIPH_L4_WKUP_PA        0x44E00000
#define PLATFORM_PERIPH_L4_WKUP_SECTIONS  1

#define PLATFORM_PERIPH_L4_PER_PA         0x48000000
#define PLATFORM_PERIPH_L4_PER_SECTIONS   4

/* AM335x TRM Ch.02 — L4_FAST: CPSW + MDIO + PRU-ICSS sub-system */
#define PLATFORM_PERIPH_L4_FAST_PA        0x4A000000
#define PLATFORM_PERIPH_L4_FAST_SECTIONS  1

#define PLATFORM_CPSW_PA                  0x4A100000
#define PLATFORM_MDIO_PA                  0x4A101000

struct platform_peripheral_region {
    uint32_t pa;
    uint32_t sections;
    const char *name;
};

extern const struct platform_peripheral_region platform_peripheral_map[];
extern const int platform_peripheral_map_count;

#endif
