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
 * Initialize LCDC (configure only, raster NOT started)
 *
 * - Enables LCDC clock and Display PLL
 * - Configures raster mode with 800x600 @ 60Hz timing
 * - Sets up DMA to read framebuffer from DDR3
 * - Does NOT enable raster output (call lcdc_start_raster() separately)
 *
 * CONTRACT:
 * - Must be called after mmu_init() and i2c_init()
 * - Framebuffer PA must be mapped in page table before calling
 */
void lcdc_init(void);

/**
 * Enable LCDC raster output
 *
 * Starts pixel clock output and DMA-driven raster scan.
 * Must be called AFTER TDA19988 is fully configured, so the
 * HDMI transmitter is ready to receive pixel data immediately.
 */
void lcdc_start_raster(void);

/**
 * Get pointer to framebuffer pixel data (after palette header)
 * @return Kernel VA pointer to first pixel (xRGB8888, uint32_t per pixel)
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
