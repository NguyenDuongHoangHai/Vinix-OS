/* ============================================================
 * tda19988.c
 * ------------------------------------------------------------
 * NXP TDA19988 HDMI Transmitter Driver
 * Target: BeagleBone Black (AM335x), 1280x720@60Hz
 * I2C interface via I2C0 at 0x44E0B000
 * ============================================================ */

#include "tda19988.h"
#include "i2c.h"
#include "uart.h"

/* ============================================================
 * 720p@60Hz timing (CEA-861 VIC=4)
 *
 * H: active=1280, FP=110, SW=40, BP=220, total=1650
 * V: active=720,  FP=5,   SW=5,  BP=20,  total=750
 * Pixel clock: 74.25 MHz (positive HS, positive VS)
 *
 * TDA19988 coordinate origin: start of HSYNC / start of VSYNC.
 * REFPIX = SW + BP = 260  (pixel where active video starts)
 * REFLINE = SW + BP = 25  (line where active video starts)
 * ============================================================ */
#define T720P_HTOTAL        1650
#define T720P_VTOTAL        750
#define T720P_HACTIVE       1280
#define T720P_VACTIVE       720
#define T720P_HSW           40
#define T720P_HBP           220
#define T720P_VSW           5
#define T720P_VBP           20

#define T720P_REFPIX        (T720P_HSW + T720P_HBP)        /* 260 */
#define T720P_REFLINE       (T720P_VSW + T720P_VBP)        /* 25  */

/* ============================================================
 * Internal state
 * ============================================================ */
static uint8_t g_current_page = 0xFF;   /* invalid sentinel */

/* ============================================================
 * Low-level I2C primitives
 * ============================================================ */

/* Switch TDA HDMI core to the given register page.
 * Uses software cache to avoid redundant page switches —
 * matches U-Boot tda19988 driver behavior. */
static void tda_set_page(uint8_t page)
{
    if (page != g_current_page) {
        if (i2c_write_reg(TDA_HDMI_I2C_ADDR, TDA_CURPAGE_ADDR, page) != 0)
            uart_printf("[TDA] ERR: page switch to 0x%02x failed\n", page);
        g_current_page = page;
    }
}

/* Write one byte to a TDA HDMI core register (page:addr encoded) */
static int tda_write(uint16_t reg, uint8_t val)
{
    tda_set_page(TDA_PAGE(reg));
    return i2c_write_reg(TDA_HDMI_I2C_ADDR, TDA_ADDR(reg), val);
}

/* Read one byte from a TDA HDMI core register */
static int tda_read(uint16_t reg, uint8_t *val)
{
    tda_set_page(TDA_PAGE(reg));
    return i2c_read_reg(TDA_HDMI_I2C_ADDR, TDA_ADDR(reg), val);
}

/* Write one byte to TDA CEC core (no page mechanism) */
static int tda_cec_write(uint8_t reg, uint8_t val)
{
    return i2c_write_reg(TDA_CEC_I2C_ADDR, reg, val);
}

/* Spin-loop delay (calibrated for ~100ns per iteration at 500MHz) */
static void tda_udelay(volatile uint32_t us)
{
    /* ~500 iterations ≈ 1µs at Cortex-A8 500MHz; conservative factor */
    volatile uint32_t count = us * 100;
    while (count--);
}

/* Write 16-bit value to a pair of consecutive MSB/LSB registers */
static void tda_write16(uint16_t reg_msb, uint16_t val16)
{
    tda_write(reg_msb,     (uint8_t)((val16 >> 8) & 0xFF));
    tda_write(reg_msb + 1, (uint8_t)(val16 & 0xFF));
}

/* ============================================================
 * Init sequence steps
 * ============================================================ */

static void tda_cec_enable(void)
{
    /* Wake up CEC module and enable HDMI core.
     * HDMI I2C slave needs time to initialize after CEC enable. */
    tda_cec_write(TDA_CEC_ENAMODS, ENAMODS_RXSENS | ENAMODS_HDMI);
    tda_udelay(50000);  /* 50ms settle for HDMI core I2C interface */
}

static void tda_soft_reset(void)
{
    uint8_t val = 0;

    /* Phase 1: Reset I2C master (DDC) and audio interfaces.
     * NXP BSL uses 50ms assert / 50ms de-assert. */
    tda_read(REG_SOFTRESET, &val);
    val |= SOFTRESET_I2C | SOFTRESET_AUDIO;
    tda_write(REG_SOFTRESET, val);
    tda_udelay(50000);  /* 50ms assert (NXP BSL requirement) */

    val &= ~(SOFTRESET_I2C | SOFTRESET_AUDIO);
    tda_write(REG_SOFTRESET, val);
    tda_udelay(50000);  /* 50ms recovery */

    /* Phase 2: Main core soft reset via MAIN_CNTRL0.
     * Uses read-modify-write to match U-Boot exactly. */
    {
        uint8_t mc0 = 0;
        tda_read(REG_MAIN_CNTRL0, &mc0);
        mc0 |= MAIN_CNTRL0_SR;
        tda_write(REG_MAIN_CNTRL0, mc0);
        mc0 &= ~MAIN_CNTRL0_SR;
        tda_write(REG_MAIN_CNTRL0, mc0);
    }

    /* Invalidate page cache after reset — chip may reset internal page */
    g_current_page = 0xFF;
}

static uint16_t tda_read_version(void)
{
    uint8_t lo = 0, hi = 0;
    tda_read(REG_VERSION,     &lo);
    tda_read(REG_VERSION_MSB, &hi);
    return (uint16_t)(((uint16_t)hi << 8) | lo);
}

static void tda_powerup(void)
{
    /* Enable all sub-modules */
    tda_write(REG_DDC_DISABLE,    0x00);  /* Enable DDC interface (Linux driver requirement) */
    tda_write(REG_CCLK_ON,        0x01);  /* CEC clock on */
    tda_write(REG_I2C_MASTER,     I2C_MASTER_DIS_MM | I2C_MASTER_DIS_FILT);
    tda_write(REG_FEAT_POWERDOWN, FEAT_POWERDOWN_SPDIF);  /* SPDIF off, video on */
}

static void tda_enable_video_ports(void)
{
    /* BBB uses 16-bit LCD interface: only VP0 and VP1 connected.
     * VP2 (upper 8 bits) tied to GND on board — disable it. */
    tda_write(REG_ENA_VP_0, 0xFF);  /* VPA[7:0] = lcd_data[7:0] */
    tda_write(REG_ENA_VP_1, 0xFF);  /* VPB[7:0] = lcd_data[15:8] */
    tda_write(REG_ENA_VP_2, 0x00);  /* VPC not connected on BBB */
    tda_write(REG_ENA_AP,   0x00);  /* Audio ports off */
}

static void tda_config_video_input(void)
{
    /*
     * 16-bit RGB565 external synchronization (rising edge)
     *
     * BBB hardware: lcd_data[15:0] → TDA19988 VP[15:0]
     *   VP0 (VPA[7:0]) = lcd_data[7:0]  = {G[2:0], B[4:0]}
     *   VP1 (VPB[7:0]) = lcd_data[15:8] = {R[4:0], G[5:3]}
     *   VP2 (VPC[7:0]) = GND (not connected)
     *
     * SWAP fields select VP nibbles (4-bit each) for internal routing:
     *   SWAP_A/B in VIP_CNTRL_0, SWAP_C/D in VIP_CNTRL_1
     *   Nibble index: 0=VP[3:0], 1=VP[7:4], 2=VP[11:8], 3=VP[15:12]
     *
     * Route VP[15:0] to internal bus positions [15:0] (identity):
     *   SWAP_A=3 (VP[15:12]), SWAP_B=2 (VP[11:8])  → upper byte
     *   SWAP_C=1 (VP[7:4]),   SWAP_D=0 (VP[3:0])   → lower byte
     */
    tda_write(REG_VIP_CNTRL_0, 0x23);  /* SWAP_A=3, SWAP_B=2 */
    tda_write(REG_VIP_CNTRL_1, 0x01);  /* SWAP_C=1, SWAP_D=0 */
    tda_write(REG_VIP_CNTRL_2, 0x01);  /* DE enabled from external pin */
    tda_write(REG_VIP_CNTRL_3, 0x00);  /* Rising edge, external sync */
    tda_write(REG_VIP_CNTRL_4, VIP_CNTRL_4_BLC(0) | VIP_CNTRL_4_BLANKIT(0));
    tda_write(REG_VIP_CNTRL_5, 0x00);

    /* Route VP output to HDMI encoder unchanged (no pixel remap) */
    tda_write(REG_MUX_VP_VIP_OUT, 0x24);
}

static void tda_config_color_matrix(void)
{
    /* Bypass color matrix: input RGB passes through unchanged to output RGB */
    tda_write(REG_MAT_CONTRL, MAT_CONTRL_MAT_BP | MAT_CONTRL_MAT_SC(3));
}

static void tda_config_720p_timing(void)
{
    /*
     * 720p@60Hz CEA timing parameters.
     * All pixel/line coordinates are relative to HSYNC/VSYNC start
     * (origin = blanking boundary, not active video start).
     *
     * HS occupies pixels [HBP : HBP+HSW] = [220 : 260] from pixel 0 of active
     * which in blanking-relative coords = pixels [0 : HSW] = [0 : 40].
     * But TDA counts from the start of the full line:
     *   hs_pix_strt = HTOTAL - (HSW+HBP) = HTOTAL - REFPIX = 1650-260 = 1390
     * ... or equivalently the complement:
     *   hs_pix_strt = HBP (= 220 from active start, wrapping = REFPIX - HSW = 220)
     *   hs_pix_stop = REFPIX (= 260)
     * (consistent with the Linux tda998x driver computation)
     */

    /* Disable output during timing setup to prevent garbage */
    tda_write(REG_TBG_CNTRL_0, TBG_CNTRL_0_FRAME_DIS | TBG_CNTRL_0_SYNC_ONCE);

    tda_write(REG_VIDFORMAT, 0x00);   /* Progressive, no repetition */

    /* Reference point: start of active video */
    tda_write16(REG_REFPIX_MSB,  T720P_REFPIX);    /* 260 */
    tda_write16(REG_REFLINE_MSB, T720P_REFLINE);   /* 25  */

    /* Total dimensions */
    tda_write16(REG_NPIX_MSB,  T720P_HTOTAL);      /* 1650 */
    tda_write16(REG_NLINE_MSB, T720P_VTOTAL);      /* 750  */

    /*
     * VSYNC timing (field 1 for progressive = entire frame)
     * VS starts at line 0, pixel REFPIX (= 260)
     * VS ends  at line VSW (= 5), pixel REFPIX (= 260)
     */
    tda_write16(REG_VS_LINE_STRT_1_MSB, 0);
    tda_write16(REG_VS_PIX_STRT_1_MSB,  T720P_REFPIX);
    tda_write16(REG_VS_LINE_END_1_MSB,  T720P_VSW);
    tda_write16(REG_VS_PIX_END_1_MSB,   T720P_REFPIX);

    /* Progressive: field 2 = unused (all zeros) */
    tda_write16(REG_VS_LINE_STRT_2_MSB, 0);
    tda_write16(REG_VS_PIX_STRT_2_MSB,  0);
    tda_write16(REG_VS_LINE_END_2_MSB,  0);
    tda_write16(REG_VS_PIX_END_2_MSB,   0);

    /*
     * HSYNC timing
     * hs_pix_strt = HBP = REFPIX - HSW = 220
     * hs_pix_stop = REFPIX = HSW + HBP = 260
     */
    tda_write16(REG_HS_PIX_START_MSB, T720P_HBP);          /* 220 */
    tda_write16(REG_HS_PIX_STOP_MSB,  T720P_REFPIX);       /* 260 */

    /*
     * Active video window
     * vwin starts at REFLINE (= 25), ends at REFLINE + Vactive (= 745)
     */
    tda_write16(REG_VWIN_START_1_MSB, T720P_REFLINE);                  /* 25  */
    tda_write16(REG_VWIN_END_1_MSB,   T720P_REFLINE + T720P_VACTIVE);  /* 745 */
    tda_write16(REG_VWIN_START_2_MSB, 0);
    tda_write16(REG_VWIN_END_2_MSB,   0);

    /*
     * Data Enable (DE) window
     * de_start = REFPIX = 260  (where active pixels begin)
     * de_stop  = REFPIX + Hactive = 1540
     */
    tda_write16(REG_DE_START_MSB, T720P_REFPIX);                       /* 260  */
    tda_write16(REG_DE_STOP_MSB,  T720P_REFPIX + T720P_HACTIVE);       /* 1540 */

    /*
     * Timing Bus Generator:
     * TBG_CNTRL_1: use LCDC external signals for PCLK, HSYNC, VSYNC.
     *   All three EXT bits must be set — without H_EXT/V_EXT the TDA
     *   generates internal sync that mismatches LCDC output.
     *   TGL_EN enables H_TGL/V_TGL polarity control from VIP_CNTRL_3.
     *   720p HSYNC/VSYNC are active-high → no toggle bits needed.
     * TBG_CNTRL_0: enable after all timing is written.
     */
    tda_write(REG_TBG_CNTRL_1,
              TBG_CNTRL_1_TGL_EN |
              TBG_CNTRL_1_X_EXT |
              TBG_CNTRL_1_H_EXT |
              TBG_CNTRL_1_V_EXT);   /* 0x3C */
    tda_write(REG_TBG_CNTRL_0, 0x00);                  /* Enable output */
}

static void tda_config_pll(void)
{
    /*
     * PLL configuration for 720p@60Hz.
     * Register order matches U-Boot tda19988_probe() exactly.
     * Must be called immediately after soft reset, before any
     * video port / timing configuration touches page 0x00.
     */

    /* Common PLL config */
    tda_write(REG_PLL_SERIAL_1, 0x00);
    tda_write(REG_PLL_SERIAL_2, PLL_SERIAL_2_SRL_NOSC(1));  /* 0x01 */
    tda_write(REG_PLL_SERIAL_3, 0x00);
    tda_write(REG_SERIALIZER,   0x00);
    tda_write(REG_BUFFER_OUT,   0x00);
    tda_write(REG_PLL_SCG1,     0x00);
    tda_write(REG_AUDIO_DIV,    AUDIO_DIV_SERCLK_8);        /* 0x03 */
    tda_write(REG_SEL_CLK,      0x09);  /* ENA_SC_CLK + SEL_CLK1 */

    /* Format-specific dividers (720p non-interlaced) */
    tda_write(REG_PLL_SCGN1, 0xFA);
    tda_write(REG_PLL_SCGN2, 0x00);
    tda_write(REG_PLL_SCGR1, 0x5B);
    tda_write(REG_PLL_SCGR2, 0x00);
    tda_write(REG_PLL_SCG2,  0x10);    /* selpllclkin=1, bypass=0 */

    /* Analog output enable */
    tda_write(REG_ANA_GENERAL, 0x09);
}

static void tda_config_output(void)
{
    /* HVF (horizontal/vertical filter) — bypass for progressive output */
    tda_write(REG_HVF_CNTRL_0, HVF_CNTRL_0_INTPOL(0) | HVF_CNTRL_0_PREFIL(0));
    tda_write(REG_HVF_CNTRL_1, HVF_CNTRL_1_VQR(0));

    /* No pixel repetition for 720p */
    tda_write(REG_RPT_CNTRL, 0x00);
}

/* ============================================================
 * Public API
 * ============================================================ */

void tda19988_init(void)
{
    uint16_t version;

    uart_printf("[TDA] Initializing TDA19988 HDMI transmitter\n");

    /* ---- Phase 1: Wake up & Reset (matches U-Boot tda19988_probe) ---- */

    /* CEC enable — no FRO here, U-Boot does FRO last */
    tda_cec_write(TDA_CEC_ENAMODS, ENAMODS_RXSENS | ENAMODS_HDMI);
    uart_printf("[TDA] CEC modules enabled\n");

    /* Soft reset: I2C master + audio (50ms assert / 50ms recover) */
    tda_soft_reset();
    uart_printf("[TDA] HDMI core reset complete\n");

    /* ---- Phase 2: PLL config IMMEDIATELY after reset (U-Boot order) ---- */
    tda_config_pll();

    /* MUX default (U-Boot writes this right after PLL) */
    tda_write(REG_MUX_VP_VIP_OUT, 0x24);

    /* Version check */
    version = tda_read_version();
    uart_printf("[TDA] Chip version: 0x%04x (expected 0x%04x)\n",
                version, TDA19988_VERSION);

    /* DDC enable + clock (U-Boot: after version, before FRO) */
    tda_write(REG_DDC_DISABLE, 0x00);

    /* FRO clock — U-Boot does this AFTER PLL and DDC */
    tda_cec_write(TDA_CEC_FRO_IM_CLK_CTRL,
                  FRO_IM_CLK_CTRL_GHOST_DIS | FRO_IM_CLK_CTRL_IMCLK_SEL);

    /* ---- Phase 3: Video path config ---- */
    tda_powerup();
    tda_enable_video_ports();
    tda_config_video_input();
    tda_config_color_matrix();
    tda_config_720p_timing();
    tda_config_output();

    /* ---- Diagnostic: PLL readback ---- */
    {
        uint8_t v = 0;

        uart_printf("[TDA] Page 0x02 readback:\n");
        tda_read(REG_PLL_SERIAL_2, &v);
        uart_printf("  PLL_SERIAL_2 (02:01) = 0x%02x (expect 0x01)\n", v);
        tda_read(REG_PLL_SCG2, &v);
        uart_printf("  PLL_SCG2     (02:06) = 0x%02x (expect 0x10)\n", v);
        tda_read(REG_PLL_SCGN1, &v);
        uart_printf("  PLL_SCGN1    (02:07) = 0x%02x (expect 0xFA)\n", v);
        tda_read(REG_PLL_SCGR1, &v);
        uart_printf("  PLL_SCGR1    (02:09) = 0x%02x (expect 0x5B)\n", v);
        tda_read(REG_SEL_CLK, &v);
        uart_printf("  SEL_CLK      (02:11) = 0x%02x (expect 0x09)\n", v);
        tda_read(REG_ANA_GENERAL, &v);
        uart_printf("  ANA_GENERAL  (02:12) = 0x%02x (expect 0x09)\n", v);
    }

    uart_printf("[TDA] TDA19988 configured for 1280x720@60Hz RGB\n");
}

void tda19988_post_lcdc_init(void)
{
    uint8_t v = 0;
    int pll_ok = 1;

    uart_printf("[TDA] Post-LCDC diagnostic (pixel clock now active)\n");

    /* 1. Check HPD and RXSENS — cable/sink detection */
    if (i2c_read_reg(TDA_CEC_I2C_ADDR, TDA_CEC_RXSHPDLEV, &v) == 0) {
        uart_printf("[TDA] RXSHPDLEV=0x%02x  HPD=%d  RXSENS=%d\n",
                    v, v & RXSHPDLEV_HPD, (v >> 1) & 1);
        if (!(v & RXSHPDLEV_HPD))
            uart_printf("[TDA] WARNING: HPD not asserted — check HDMI cable\n");
    } else {
        uart_printf("[TDA] ERROR: Cannot read CEC RXSHPDLEV\n");
    }

    /* 2. Re-read PLL registers now that LCDC pixel clock is running */
    uart_printf("[TDA] PLL readback (with pixel clock):\n");
    tda_read(REG_PLL_SCG2, &v);
    uart_printf("  PLL_SCG2  = 0x%02x (expect 0x10)\n", v);
    if (v != 0x10) pll_ok = 0;

    tda_read(REG_PLL_SCGN1, &v);
    uart_printf("  PLL_SCGN1 = 0x%02x (expect 0xFA)\n", v);
    if (v != 0xFA) pll_ok = 0;

    tda_read(REG_PLL_SCGR1, &v);
    uart_printf("  PLL_SCGR1 = 0x%02x (expect 0x5B)\n", v);
    if (v != 0x5B) pll_ok = 0;

    tda_read(REG_SEL_CLK, &v);
    uart_printf("  SEL_CLK   = 0x%02x (expect 0x09)\n", v);

    /* 3. If PLL registers still 0, pixel clock is now present — re-write */
    if (!pll_ok) {
        uart_printf("[TDA] PLL registers not latched — re-writing with pixel clock active\n");

        /* Force page cache invalidation before retry */
        g_current_page = 0xFF;

        tda_write(REG_PLL_SERIAL_1, 0x00);
        tda_write(REG_PLL_SERIAL_2, PLL_SERIAL_2_SRL_NOSC(1));
        tda_write(REG_PLL_SERIAL_3, 0x00);
        tda_write(REG_SERIALIZER,   0x00);
        tda_write(REG_BUFFER_OUT,   0x00);
        tda_write(REG_PLL_SCG1,     0x00);
        tda_write(REG_AUDIO_DIV,    AUDIO_DIV_SERCLK_8);
        tda_write(REG_SEL_CLK,      0x09);
        tda_write(REG_PLL_SCGN1, 0xFA);
        tda_write(REG_PLL_SCGN2, 0x00);
        tda_write(REG_PLL_SCGR1, 0x5B);
        tda_write(REG_PLL_SCGR2, 0x00);
        tda_write(REG_PLL_SCG2,  0x10);
        tda_write(REG_ANA_GENERAL, 0x09);

        /* Small delay for PLL to lock */
        tda_udelay(100000);

        /* Verify retry */
        tda_read(REG_PLL_SCG2, &v);
        uart_printf("  PLL_SCG2 after retry = 0x%02x\n", v);
        tda_read(REG_PLL_SCGN1, &v);
        uart_printf("  PLL_SCGN1 after retry = 0x%02x\n", v);
        tda_read(REG_PLL_SCGR1, &v);
        uart_printf("  PLL_SCGR1 after retry = 0x%02x\n", v);
        tda_read(REG_SEL_CLK, &v);
        uart_printf("  SEL_CLK after retry = 0x%02x\n", v);
    } else {
        uart_printf("[TDA] PLL registers OK — PLL should be locked\n");
    }

    /* 4. Page switch verification test.
     * Read addr 0x00 on page 0x00 (VERSION) vs page 0x02 (PLL_SERIAL_1).
     * If both return the same value, page switch is broken. */
    {
        uint8_t val_p0 = 0, val_p2 = 0;

        /* Force explicit page switch via raw I2C (bypass cache) */
        g_current_page = 0xFF;
        i2c_write_reg(TDA_HDMI_I2C_ADDR, TDA_CURPAGE_ADDR, 0x00);
        g_current_page = 0x00;
        i2c_read_reg(TDA_HDMI_I2C_ADDR, 0x00, &val_p0);

        g_current_page = 0xFF;
        i2c_write_reg(TDA_HDMI_I2C_ADDR, TDA_CURPAGE_ADDR, 0x02);
        g_current_page = 0x02;
        i2c_read_reg(TDA_HDMI_I2C_ADDR, 0x00, &val_p2);

        uart_printf("[TDA] Page switch test:\n");
        uart_printf("  Page0x00:addr0x00 = 0x%02x (expect VERSION ~0x31)\n", val_p0);
        uart_printf("  Page0x02:addr0x00 = 0x%02x (expect PLL_SERIAL_1, diff from above)\n", val_p2);

        if (val_p0 == val_p2)
            uart_printf("[TDA] WARNING: same value — page switch may be broken!\n");
        else
            uart_printf("[TDA] Page switch OK — different values on different pages\n");

        /* Also try write-read test on page 0x02:
         * Write 0xAA to PLL_SERIAL_1 (addr 0x00), read back */
        i2c_write_reg(TDA_HDMI_I2C_ADDR, 0x00, 0xAA);
        i2c_read_reg(TDA_HDMI_I2C_ADDR, 0x00, &v);
        uart_printf("  Write 0xAA to page02:addr00, readback = 0x%02x\n", v);

        /* Restore PLL_SERIAL_1 to correct value */
        i2c_write_reg(TDA_HDMI_I2C_ADDR, 0x00, 0x00);
    }

    /* 5. Re-enable TBG output */
    g_current_page = 0xFF;
    tda_write(REG_TBG_CNTRL_0, 0x00);
}
