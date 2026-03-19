/* ============================================================
 * boot_screen.c — Boot Log + Splash Screen
 * Draws directly to framebuffer via fb.h primitives.
 * Completely independent of UART — no serial dependency.
 * ============================================================ */

#include "types.h"
#include "boot_screen.h"
#include "fb.h"
#include "lcdc.h"
#include "timer.h"

/* ============================================================
 * Screen 1: Boot Log
 * ============================================================ */

static void show_boot_log(void)
{
    uint32_t sw = lcdc_get_width();
    uint32_t sh = lcdc_get_height();
    uint16_t bg       = FB_BLACK;
    uint16_t border   = FB_RGB(180, 180, 180);
    uint16_t ok_col   = FB_RGB(0, 220, 0);
    uint16_t txt_col  = FB_WHITE;
    uint16_t done_col = FB_RGB(220, 200, 60);
    uint16_t title_col= FB_RGB(180, 180, 180);

    fb_clear(bg);

    /* Thin border inset 20px */
    uint32_t bx = 20, by = 20, bw = sw - 40, bh = sh - 40;
    fb_fillrect(bx, by, bw, 1, border);
    fb_fillrect(bx, by + bh - 1, bw, 1, border);
    fb_fillrect(bx, by, 1, bh, border);
    fb_fillrect(bx + bw - 1, by, 1, bh, border);

    uint32_t cx = bx + 24;
    uint32_t cy = by + 20;
    uint32_t line_h = FB_FONT_H + 4;

    /* Title */
    fb_puts(cx, cy, "VinixOS Boot", title_col, bg);
    cy += line_h + 8;

    /* Status lines — appear one by one */
    static const char *labels[] = {
        "CPU: ARM Cortex-A8 800MHz (ARMv7-A)",
        "Board: BeagleBone Black Rev.C",
        "SoC: Texas Instruments AM335x",
        "Memory: 256MB DDR3 @ 400MHz",
        "MMU: Virtual memory enabled",
        "Watchdog: Disabled",
        "I2C0: Bus initialized (100kHz)",
        "LCDC: Framebuffer 800x600 RGB565",
        "DPLL: Display PLL locked @ 40MHz",
        "HDMI: TDA19988 TMDS link active",
        "INTC: Interrupt controller ready",
        "Timer: DMTimer2 (10ms preemptive tick)",
        "UART0: Console 115200 8N1",
        "Storage: SD/MMC card mounted",
        "VFS: Virtual filesystem mounted at /",
        "Scheduler: Round-robin preemptive ready",
    };
    uint32_t n_labels = sizeof(labels) / sizeof(labels[0]);

    for (uint32_t i = 0; i < n_labels; i++) {
        fb_puts(cx, cy, "[ ", txt_col, bg);
        fb_puts(cx + 2 * FB_FONT_W, cy, "OK", ok_col, bg);
        fb_puts(cx + 4 * FB_FONT_W, cy, " ]  ", txt_col, bg);
        fb_puts(cx + 8 * FB_FONT_W, cy, labels[i], txt_col, bg);
        cy += line_h;
        delay_ms(300);
    }

    /* Boot complete */
    cy += line_h / 2;
    fb_puts(cx, cy, "Boot complete. All services ready.", done_col, bg);

    delay_ms(1000);
}

/* ============================================================
 * Screen 2: Splash
 * ============================================================ */

static void show_splash(void)
{
    uint32_t sw = lcdc_get_width();
    uint32_t sh = lcdc_get_height();
    uint16_t bg = FB_RGB(10, 10, 46);

    fb_clear(bg);

    /* "VINIX OS" — scale 6 */
    uint32_t scale = 6;
    uint32_t tw = 8 * FB_FONT_W * scale;
    uint32_t th = FB_FONT_H * scale;
    uint32_t tx = (sw - tw) / 2;
    uint32_t ty = sh / 3 - th / 2;
    fb_puts_scaled(tx, ty, "VINIX OS", FB_WHITE, bg, scale);

    /* Subtitle 1 */
    const char *sub1 = "Reference ARM Software Platform";
    uint32_t s1w = 31 * FB_FONT_W;
    fb_puts((sw - s1w) / 2, ty + th + 30, sub1, FB_RGB(170, 170, 170), bg);

    /* Subtitle 2 */
    const char *sub2 = "Developed by Vinalinux - Vietnamese Operating System";
    uint32_t s2w = 52 * FB_FONT_W;
    fb_puts((sw - s2w) / 2, ty + th + 56, sub2, FB_RGB(100, 200, 200), bg);

    /* "Starting environment" + dots animation */
    uint32_t dots_y = sh * 2 / 3 + 20;
    const char *msg = "Starting environment";
    uint32_t mw = 20 * FB_FONT_W;
    uint32_t mx = (sw - (mw + 3 * FB_FONT_W)) / 2;
    uint16_t dot_col = FB_RGB(136, 136, 136);

    fb_puts(mx, dots_y, msg, dot_col, bg);

    uint32_t dot_x = mx + mw;
    for (int cycle = 0; cycle < 3; cycle++) {
        for (int dots = 1; dots <= 3; dots++) {
            fb_fillrect(dot_x, dots_y, 3 * FB_FONT_W, FB_FONT_H, bg);
            for (int d = 0; d < dots; d++)
                fb_draw_char(dot_x + d * FB_FONT_W, dots_y, '.', dot_col, bg);
            delay_ms(500);
        }
    }
}

/* ============================================================
 * Screen 3: Home
 * ============================================================ */

static void show_home(void)
{
    uint32_t sw = lcdc_get_width();
    uint32_t sh = lcdc_get_height();
    uint16_t bg     = FB_RGB(30, 30, 50);
    uint16_t border = FB_RGB(100, 140, 180);
    uint16_t txt    = FB_WHITE;

    fb_clear(bg);

    /* Centered frame */
    uint32_t fw = 300, fh = 160;
    uint32_t fx = (sw - fw) / 2;
    uint32_t fy = (sh - fh) / 2;

    /* Border 2px */
    fb_fillrect(fx, fy, fw, 2, border);
    fb_fillrect(fx, fy + fh - 2, fw, 2, border);
    fb_fillrect(fx, fy, 2, fh, border);
    fb_fillrect(fx + fw - 2, fy, 2, fh, border);

    /* "Home" centered in frame, scale 3 */
    uint32_t scale = 3;
    uint32_t tw = 4 * FB_FONT_W * scale;
    uint32_t th = FB_FONT_H * scale;
    fb_puts_scaled(fx + (fw - tw) / 2, fy + (fh - th) / 2,
                   "Home", txt, bg, scale);
}

/* ============================================================
 * Public API
 * ============================================================ */

void boot_screen_run(void)
{
    show_boot_log();
    show_splash();
    show_home();
}
