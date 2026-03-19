/* ============================================================
 * tda19988.c
 * ------------------------------------------------------------
 * NXP TDA19988 HDMI Transmitter Driver
 * Target: BeagleBone Black (AM335x), 1280x720@60Hz
 * I2C interface via I2C0 at 0x44E0B000
 *
 * Init sequence ported from U-Boot tda19988 driver
 * (Liviu Dudau, based on Linux/TI driver).
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
 * ============================================================ */
/* Source: QNX drm_1280x720 struct (hdmi.c line 125-138) */
#define T720P_HTOTAL        1650
#define T720P_VTOTAL        750
#define T720P_HACTIVE       1280
#define T720P_VACTIVE       720
#define T720P_HFP           110     /* hsync_start - hdisplay = 1390 - 1280 */
#define T720P_HSW           40      /* hsync_end - hsync_start = 1430 - 1390 */
#define T720P_HBP           220     /* htotal - hsync_end = 1650 - 1430 */
#define T720P_VFP           5       /* vsync_start - vdisplay = 725 - 720 */
#define T720P_VSW           5       /* vsync_end - vsync_start = 730 - 725 */
#define T720P_VBP           20      /* vtotal - vsync_end = 750 - 730 */
#define T720P_HSKEW         40      /* QNX: 0x28 */

/* TDA timing values calculated using QNX formula (hdmi.c lines 469-505) */
#define TDA_REF_PIX         (3 + T720P_HFP + T720P_HSKEW)              /* 153 */
#define TDA_REF_LINE        (1 + T720P_VFP)                             /* 6   */
#define TDA_DE_PIX_S        (T720P_HTOTAL - T720P_HACTIVE)             /* 370 */
#define TDA_DE_PIX_E        (TDA_DE_PIX_S + T720P_HACTIVE)             /* 1650 */
#define TDA_HS_PIX_S        T720P_HFP                                   /* 110 */
#define TDA_HS_PIX_E        (T720P_HFP + T720P_HSW)                    /* 150 */
#define TDA_VS1_LINE_S      T720P_VFP                                   /* 5   */
#define TDA_VS1_LINE_E      (T720P_VFP + T720P_VSW)                    /* 10  */
#define TDA_VWIN1_LINE_S    (T720P_VTOTAL - T720P_VACTIVE - 1)         /* 29  */
#define TDA_VWIN1_LINE_E    (TDA_VWIN1_LINE_S + T720P_VACTIVE)         /* 749 */

/* ============================================================
 * Internal state
 * ============================================================ */
static uint8_t g_current_page = 0xFF;   /* invalid sentinel */

/* ============================================================
 * Low-level I2C primitives
 * ============================================================ */

static void tda_set_page(uint8_t page)
{
    if (page != g_current_page) {
        if (i2c_write_reg(TDA_HDMI_I2C_ADDR, TDA_CURPAGE_ADDR, page) != 0)
            uart_printf("[TDA] ERR: page switch to 0x%02x failed\n", page);
        g_current_page = page;
    }
}

static int tda_write(uint16_t reg, uint8_t val)
{
    tda_set_page(TDA_PAGE(reg));
    return i2c_write_reg(TDA_HDMI_I2C_ADDR, TDA_ADDR(reg), val);
}

static int tda_read(uint16_t reg, uint8_t *val)
{
    tda_set_page(TDA_PAGE(reg));
    return i2c_read_reg(TDA_HDMI_I2C_ADDR, TDA_ADDR(reg), val);
}

/* Read-modify-write: set bits */
static void tda_set(uint16_t reg, uint8_t bits)
{
    uint8_t val = 0;
    tda_read(reg, &val);
    val |= bits;
    tda_write(reg, val);
}

/* Read-modify-write: clear bits */
static void tda_clear(uint16_t reg, uint8_t bits)
{
    uint8_t val = 0;
    tda_read(reg, &val);
    val &= ~bits;
    tda_write(reg, val);
}

static int tda_cec_write(uint8_t reg, uint8_t val)
{
    return i2c_write_reg(TDA_CEC_I2C_ADDR, reg, val);
}

static void tda_mdelay(volatile uint32_t ms)
{
    /* Empirically calibrated from UART log timing:
     * tda_mdelay(100) with 50000 multiplier took ~20-30 seconds real time.
     * That means 1 iteration ≈ 4-6 µs → CPU effective rate ~200 KHz equiv.
     * Using 200 iters/ms to get ~1ms actual wall time.
     * This accounts for volatile access overhead on slow peripheral bus. */
    volatile uint32_t count = ms * 200;
    while (count--);
}

static void tda_write16(uint16_t reg_msb, uint16_t val16)
{
    tda_write(reg_msb,     (uint8_t)((val16 >> 8) & 0xFF));
    tda_write(reg_msb + 1, (uint8_t)(val16 & 0xFF));
}

/* ============================================================
 * Probe: CEC enable, soft reset, PLL, version
 * Matches U-Boot tda19988_probe() order exactly.
 * ============================================================ */

static void tda_probe(void)
{
    uint16_t version;

    /* Wake up device */
    tda_cec_write(TDA_CEC_ENAMODS, ENAMODS_RXSENS | ENAMODS_HDMI);
    uart_printf("[TDA] CEC modules enabled\n");

    /* Reset audio and I2C master (DDC). */
    uart_printf("[TDA] soft reset: writing SOFTRESET...\n");
    tda_write(REG_SOFTRESET, SOFTRESET_AUDIO | SOFTRESET_I2C);
    uart_printf("[TDA] soft reset: delay 1...\n");
    tda_mdelay(100);
    uart_printf("[TDA] soft reset: clearing SOFTRESET...\n");
    tda_write(REG_SOFTRESET, 0);
    uart_printf("[TDA] soft reset: delay 2...\n");
    tda_mdelay(100);
    uart_printf("[TDA] soft reset: done\n");

    /* Transmitter core reset — all production drivers (Linux, U-Boot, QNX) do this.
     * Previous testing showed SCG registers (audio PLL) fail after SR, but those
     * are audio-only. Video PLL (SERIAL_2, ANA_GENERAL) still works fine. */
    tda_set(REG_MAIN_CNTRL0, MAIN_CNTRL0_SR);
    tda_clear(REG_MAIN_CNTRL0, MAIN_CNTRL0_SR);

    /* Invalidate page cache after reset */
    g_current_page = 0xFF;

    uart_printf("[TDA] HDMI core reset complete (soft + SR)\n");

    /* Read version (page 0x00 — always accessible) */
    version = 0;
    {
        uint8_t lo = 0, hi = 0;
        tda_read(REG_VERSION, &lo);
        tda_read(REG_VERSION_MSB, &hi);
        version = ((uint16_t)hi << 8) | lo;
    }
    uart_printf("[TDA] Chip version: 0x%04x (expected 0x%04x)%s\n",
                version, TDA19988_VERSION,
                (version == TDA19988_VERSION) ? " OK" : " MISMATCH!");

    /* Enable DDC */
    tda_write(REG_DDC_DISABLE, 0x00);

    /* Enable CEC clock — needed for internal state machine */
    tda_write(REG_CCLK_ON, 0x01);

    /* Disable multi-master on I2C */
    tda_set(REG_I2C_MASTER, I2C_MASTER_DIS_MM);

    /* Explicit power state: only SPDIF powered down, video path ON.
     * Default after reset may have video bits powered down.
     * Using direct write (not tda_set) to control ALL bits. */
    tda_write(REG_FEAT_POWERDOWN, FEAT_POWERDOWN_SPDIF);

    /* Default MUX register */
    tda_write(REG_MUX_VP_VIP_OUT, 0x24);

    /* FRO clock config */
    tda_cec_write(TDA_CEC_FRO_IM_CLK_CTRL,
                  FRO_IM_CLK_CTRL_GHOST_DIS | FRO_IM_CLK_CTRL_IMCLK_SEL);

    uart_printf("[TDA] Probe complete (PLL deferred — needs pixel clock)\n");
}

/* ============================================================
 * Enable: video path configuration + output enable
 * Matches U-Boot tda19988_enable() order exactly.
 * ============================================================ */

static void tda_enable_720p(void)
{
    /* QNX init order:
     * 1. encoder_dpms(): enable ports + VIP mux
     * 2. encoder_mode_set(): mute audio, config timing, TBG, enable HDMI */

    uart_printf("[TDA] enable_720p: step 1 — enable ports + VIP mux (QNX dpms)\n");

    /* QNX tda998x_encoder_dpms (hdmi.c line 443-453) */
    tda_write(REG_ENA_AP, 0x03);    /* Audio ports (QNX enables these) */
    tda_write(REG_ENA_VP_0, 0xFF);
    tda_write(REG_ENA_VP_1, 0xFF);
    tda_write(REG_ENA_VP_2, 0xFF);
    /* VIP muxing for 16-bit RGB565 (QNX values) */
    tda_write(REG_VIP_CNTRL_0, 0x23);  /* SWAP_A=2, SWAP_B=3 */
    tda_write(REG_VIP_CNTRL_1, 0x01);  /* SWAP_C=0, SWAP_D=1 */
    tda_write(REG_VIP_CNTRL_2, 0x45);  /* SWAP_E=4, SWAP_F=5 */

    uart_printf("[TDA] enable_720p: step 2 — mode set start\n");

    /* QNX tda998x_encoder_mode_set (hdmi.c line 514-608) */
    /* Mute audio FIFO */
    tda_set(REG_AIP_CNTRL_0, AIP_CNTRL_0_RST_FIFO);

    /* Disable HDMI during config */
    tda_set(REG_TBG_CNTRL_1, TBG_CNTRL_1_DWIN_DIS);
    tda_clear(REG_TX33, TX33_HDMI);
    tda_write(REG_ENC_CNTRL, ENC_CNTRL_CTL_CODE(0));

    uart_printf("[TDA] enable_720p: step 3 — video path config\n");

    /* No pre-filter or interpolator */
    tda_write(REG_HVF_CNTRL_0, HVF_CNTRL_0_PREFIL(0) | HVF_CNTRL_0_INTPOL(0));

    /* Power: only SPDIF + pre-filter + CSC off. Direct write — NOT tda_set(),
     * because default after reset may have video output powered down. */
    tda_write(REG_FEAT_POWERDOWN,
              FEAT_POWERDOWN_SPDIF | FEAT_POWERDOWN_PREFILT | FEAT_POWERDOWN_CSC);

    tda_write(REG_VIP_CNTRL_5, VIP_CNTRL_5_SP_CNT(0));
    tda_write(REG_VIP_CNTRL_4, VIP_CNTRL_4_BLANKIT(0) | VIP_CNTRL_4_BLC(0));

    uart_printf("[TDA] enable_720p: step 3 — PLL + serializer config (page 0x02)\n");

    /* Serializer PLL: direct writes, no read-modify-write.
     * PLL_SERIAL_2 is confirmed writable. SEL_CLK bit 0 may not stick
     * but we write it anyway — serializer may work with just ENA_SC_CLK. */
    tda_write(REG_PLL_SERIAL_1, 0x00);
    tda_write(REG_PLL_SERIAL_3, 0x00);
    tda_write(REG_SERIALIZER, 0);
    tda_write(REG_HVF_CNTRL_1, HVF_CNTRL_1_VQR(0));
    tda_write(REG_RPT_CNTRL, 0);
    tda_write(REG_SEL_CLK, SEL_CLK_SEL_VRF_CLK(0) |
              SEL_CLK_SEL_CLK1 | SEL_CLK_ENA_SC_CLK);
    tda_write(REG_PLL_SERIAL_2, PLL_SERIAL_2_SRL_NOSC(1) |
              PLL_SERIAL_2_SRL_PR(0));

    /* Color matrix: bypass */
    tda_write(REG_MAT_CONTRL, MAT_CONTRL_MAT_BP | MAT_CONTRL_MAT_SC(1));

    /* Analog output — critical for TMDS signal generation */
    tda_write(REG_ANA_GENERAL, 0x09);

    /* Verify SEL_CLK and ANA_GENERAL */
    {
        uint8_t sc = 0, ag = 0;
        tda_read(REG_SEL_CLK, &sc);
        tda_read(REG_ANA_GENERAL, &ag);
        uart_printf("[TDA]   SEL_CLK=0x%02x ANA_GENERAL=0x%02x\n", sc, ag);
    }

    uart_printf("[TDA] enable_720p: step 4 — analog + color config\n");

    uart_printf("[TDA] enable_720p: step 5 — timing registers (QNX formula)\n");

    /* All timing values calculated from QNX hdmi.c tda998x_encoder_mode_set()
     * using drm_1280x720 struct. See defines at top of file. */

    tda_write(REG_VIDFORMAT, 0x00);

    tda_write16(REG_REFPIX_MSB,  TDA_REF_PIX);                   /* 153  */
    tda_write16(REG_REFLINE_MSB, TDA_REF_LINE);                  /* 6    */

    tda_write16(REG_NPIX_MSB,  T720P_HTOTAL);                    /* 1650 */
    tda_write16(REG_NLINE_MSB, T720P_VTOTAL);                    /* 750  */

    /* VSYNC (progressive) */
    tda_write16(REG_VS_LINE_STRT_1_MSB, TDA_VS1_LINE_S);         /* 5   */
    tda_write16(REG_VS_PIX_STRT_1_MSB,  TDA_HS_PIX_S);           /* 110 */
    tda_write16(REG_VS_LINE_END_1_MSB,  TDA_VS1_LINE_E);         /* 10  */
    tda_write16(REG_VS_PIX_END_1_MSB,   TDA_HS_PIX_S);           /* 110 */

    /* Progressive: field 2 unused */
    tda_write16(REG_VS_LINE_STRT_2_MSB, 0);
    tda_write16(REG_VS_PIX_STRT_2_MSB,  0);
    tda_write16(REG_VS_LINE_END_2_MSB,  0);
    tda_write16(REG_VS_PIX_END_2_MSB,   0);

    /* HSYNC */
    tda_write16(REG_HS_PIX_START_MSB, TDA_HS_PIX_S);             /* 110 */
    tda_write16(REG_HS_PIX_STOP_MSB,  TDA_HS_PIX_E);             /* 150 */

    /* VWIN: active video window (QNX formula: vtotal - vactive - 1) */
    tda_write16(REG_VWIN_START_1_MSB, TDA_VWIN1_LINE_S);         /* 29  */
    tda_write16(REG_VWIN_END_1_MSB,   TDA_VWIN1_LINE_E);         /* 749 */
    tda_write16(REG_VWIN_START_2_MSB, 0);
    tda_write16(REG_VWIN_END_2_MSB,   0);

    /* DE: data enable (QNX formula: htotal - hactive to htotal) */
    tda_write16(REG_DE_START_MSB, TDA_DE_PIX_S);                 /* 370  */
    tda_write16(REG_DE_STOP_MSB,  TDA_DE_PIX_E);                 /* 1650 */

    /* TDA19988: enable active space fill (QNX uses 0x01, we used 0x00) */
    tda_write(REG_ENABLE_SPACE, 0x01);

    /*
     * TBG_CNTRL_1: QNX uses ONLY TGL_EN — NO X_EXT/H_EXT/V_EXT.
     * 720p has positive HSYNC/VSYNC → no H_TGL/V_TGL needed.
     * TBG_CNTRL_0: clear SYNC_MTHD first, then clear SYNC_ONCE last.
     */
    tda_clear(REG_TBG_CNTRL_0, TBG_CNTRL_0_SYNC_MTHD);

    /* VIP_CNTRL_3: sync on HSYNC, 720p positive sync → no toggle.
     * QNX: write 0 first, then set SYNC_HS. */
    tda_write(REG_VIP_CNTRL_3, 0);
    tda_set(REG_VIP_CNTRL_3, VIP_CNTRL_3_SYNC_HS);

    /* TBG: only TGL_EN, no external sync bits (QNX line 571) */
    tda_write(REG_TBG_CNTRL_1, TBG_CNTRL_1_TGL_EN);

    /* Enable HDMI + encoder */
    tda_clear(REG_TBG_CNTRL_1, TBG_CNTRL_1_DWIN_DIS);
    tda_write(REG_ENC_CNTRL, ENC_CNTRL_CTL_CODE(1));

    /* MUST BE LAST register set (QNX line 607) */
    tda_clear(REG_TBG_CNTRL_0, TBG_CNTRL_0_SYNC_ONCE);

    uart_printf("[TDA] enable_720p: step 6 — TBG + timing done\n");
    uart_printf("[TDA] Timing: REFPIX=%d REFLINE=%d DE=%d-%d VWIN=%d-%d\n",
                TDA_REF_PIX, TDA_REF_LINE,
                TDA_DE_PIX_S, TDA_DE_PIX_E,
                TDA_VWIN1_LINE_S, TDA_VWIN1_LINE_E);

    uart_printf("[TDA] enable_720p: done\n");
}

/* ============================================================
 * Public API
 * ============================================================ */

void tda19988_init(void)
{
    uart_printf("[TDA] Initializing TDA19988 HDMI transmitter\n");
    tda_probe();
    uart_printf("[TDA] TDA19988 probe complete\n");
}

void tda19988_post_lcdc_init(void)
{
    uint8_t v = 0;

    uart_printf("[TDA] Post-LCDC: pixel clock now active, configuring PLL...\n");

    /* === Step 1: Verify page 0x02 is now accessible (pixel clock present) === */
    g_current_page = 0xFF;
    tda_write(REG_PLL_SCG2, 0x10);
    tda_read(REG_PLL_SCG2, &v);
    uart_printf("[TDA] Page 0x02 test: wrote 0x10, read 0x%02x%s\n",
                v, (v == 0x10) ? " OK" : " FAIL");

    if (v != 0x10) {
        uart_printf("[TDA] ERROR: Page 0x02 still locked even with pixel clock!\n");
        uart_printf("[TDA] Continuing anyway — output may not work.\n");
    }

    /* === Step 2: Write PLL configuration (page 0x02) === */
    uart_printf("[TDA] Writing PLL registers...\n");
    tda_write(REG_PLL_SERIAL_1, 0x00);
    tda_write(REG_PLL_SERIAL_2, PLL_SERIAL_2_SRL_NOSC(1));
    tda_write(REG_PLL_SERIAL_3, 0x00);
    tda_write(REG_SERIALIZER,   0x00);
    tda_write(REG_BUFFER_OUT,   0x00);
    tda_write(REG_PLL_SCG1,     0x00);
    tda_write(REG_AUDIO_DIV,    AUDIO_DIV_SERCLK_8);
    tda_write(REG_SEL_CLK,      SEL_CLK_SEL_CLK1 | SEL_CLK_ENA_SC_CLK);
    tda_write(REG_PLL_SCGN1, 0xFA);
    tda_write(REG_PLL_SCGN2, 0x00);
    tda_write(REG_PLL_SCGR1, 0x5B);
    tda_write(REG_PLL_SCGR2, 0x00);
    tda_write(REG_PLL_SCG2,  0x10);

    /* Verify PLL writes */
    tda_read(REG_PLL_SCGN1, &v);
    uart_printf("[TDA] PLL verify: SCGN1=0x%02x (expect 0xFA) SEL_CLK=", v);
    tda_read(REG_SEL_CLK, &v);
    uart_printf("0x%02x (expect 0x09)\n", v);

    /* === Step 3: HPD check === */
    if (i2c_read_reg(TDA_CEC_I2C_ADDR, TDA_CEC_RXSHPDLEV, &v) == 0) {
        uart_printf("[TDA] HPD=%d  RXSENS=%d\n", v & 1, (v >> 1) & 1);
    }

    /* === Step 4: Full video path enable === */
    tda_enable_720p();
    uart_printf("[TDA] Video output enabled (1280x720@60Hz)\n");

    /* === Step 5: Post-enable diagnostic === */
    {
        uint8_t v0 = 0, v1 = 0;
        tda_read(REG_PLL_SERIAL_2, &v0);
        tda_read(REG_SEL_CLK, &v1);
        uart_printf("[TDA] PLL final: SERIAL_2=0x%02x  SEL_CLK=0x%02x\n", v0, v1);

        tda_read(REG_PLL_SCGN1, &v0);
        tda_read(REG_PLL_SCG2, &v1);
        uart_printf("[TDA] PLL final: SCGN1=0x%02x  SCG2=0x%02x\n", v0, v1);

        if (i2c_read_reg(TDA_CEC_I2C_ADDR, TDA_CEC_RXSHPDLEV, &v0) == 0) {
            uart_printf("[TDA] Final: HPD=%d  RXSENS=%d\n", v0 & 1, (v0 >> 1) & 1);
        }
    }
}
