/* ============================================================
 * lcdc.h
 * ------------------------------------------------------------
 * LCD Controller Driver Interface
 * Target: AM335x LCDC in Raster mode (TFT 24bpp)
 * ============================================================ */

#ifndef LCDC_H
#define LCDC_H

#include "types.h"
#include "mmu.h"

/* ============================================================
 * LCDC Configuration
 * ============================================================ */

/* LCDC base address (L4_PER domain) */
#define LCDC_BASE           0x4830E000

/* FB_PA_BASE is defined in mmu.h as part of the platform memory map */

/* ============================================================
 * Public API
 * ============================================================ */

/**
 * Initialize LCDC
 *
 * - Enables LCDC clock and Display PLL
 * - Configures raster mode with 720p @ 60Hz timing
 * - Sets up DMA to read framebuffer from DDR3
 * - Enables raster output
 *
 * CONTRACT:
 * - Must be called after mmu_init() and i2c_init()
 * - Framebuffer PA must be mapped in page table before calling
 */
void lcdc_init(void);

/**
 * Get pointer to framebuffer pixel data (after palette header)
 * @return Kernel VA pointer to first pixel (RGB565, uint16_t per pixel)
 */
uint16_t *lcdc_get_framebuffer(void);

/**
 * Get display width in pixels
 */
uint32_t lcdc_get_width(void);

/**
 * Get display height in pixels
 */
uint32_t lcdc_get_height(void);

/**
 * Get pitch (bytes per row)
 */
uint32_t lcdc_get_pitch(void);

#endif /* LCDC_H */
