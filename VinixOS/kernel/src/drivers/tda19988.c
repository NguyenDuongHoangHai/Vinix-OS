/* ============================================================
 * tda19988.c
 * ------------------------------------------------------------
 * NXP TDA19988 HDMI Transmitter Driver
 * Target: BeagleBone Black (AM335x), 640x480@60Hz
 * I2C interface via I2C0 at 0x44E0B000
 * ============================================================ */

#include "tda19988.h"
#include "i2c.h"
#include "uart.h"

/* ============================================================
 * 640x480@60Hz timing (VESA DMT / CEA-861 VIC=1)
 *
 * H: active=640, FP=16, SW=96, BP=48, total=800
 * V: active=480, FP=10, SW=2,  BP=33, total=525
 * Pixel clock: 25.175 MHz (using 25 MHz)
 * Sync: NHSYNC, NVSYNC (both negative)
 * ============================================================ */

#define TVGA_HTOTAL         800
#define TVGA_VTOTAL         525
#define TVGA_HACTIVE        640
#define TVGA_VACTIVE        480
#define TVGA_HFP            16
#define TVGA_HSW            96
#define TVGA_HBP            48
#define TVGA_VFP            10
#define TVGA_VSW            2
#define TVGA_VBP            33
#define TVGA_HSKEW          0       /* No HSKEW for VGA */

/* TDA timing values — QNX formula (hdmi.c lines 469-505) */
#define TDA_REF_PIX         (3 + TVGA_HFP + TVGA_HSKEW)                /* 19  */
#define TDA_REF_LINE        (1 + TVGA_VFP)                              /* 11  */
#define TDA_DE_PIX_S        (TVGA_HTOTAL - TVGA_HACTIVE)                /* 160 */
#define TDA_DE_PIX_E        (TDA_DE_PIX_S + TVGA_HACTIVE)               /* 800 */
#define TDA_HS_PIX_S        TVGA_HFP                                    /* 16  */
#define TDA_HS_PIX_E        (TVGA_HFP + TVGA_HSW)                       /* 112 */
#define TDA_VS1_LINE_S      TVGA_VFP                                    /* 10  */
#define TDA_VS1_LINE_E      (TVGA_VFP + TVGA_VSW)                       /* 12  */
#define TDA_VWIN1_LINE_S    (TVGA_VTOTAL - TVGA_VACTIVE - 1)            /* 44  */
#define TDA_VWIN1_LINE_E    (TDA_VWIN1_LINE_S + TVGA_VACTIVE)           /* 524 */

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

    /* PLL common config — IMMEDIATELY after reset, BEFORE pixel clock.
     * This is the exact order in Linux/U-Boot/QNX production drivers.
     * Previous attempt deferred PLL to post_lcdc_init → registers locked
     * because pixel clock was already running (hardware auto-control). */
    uart_printf("[TDA] Writing PLL config (before LCDC, like production drivers)...\n");
    tda_write(REG_PLL_SERIAL_1, 0x00);
    tda_write(REG_PLL_SERIAL_2, PLL_SERIAL_2_SRL_NOSC(1));
    tda_write(REG_PLL_SERIAL_3, 0x00);
    tda_write(REG_SERIALIZER,   0x00);
    tda_write(REG_BUFFER_OUT,   0x00);
    tda_write(REG_PLL_SCG1,     0x00);
    tda_write(REG_AUDIO_DIV,    AUDIO_DIV_SERCLK_8);
    tda_write(REG_SEL_CLK,      SEL_CLK_SEL_CLK1 | SEL_CLK_ENA_SC_CLK);
    tda_write(REG_PLL_SCGN1,    0xFA);
    tda_write(REG_PLL_SCGN2,    0x00);
    tda_write(REG_PLL_SCGR1,    0x5B);
    tda_write(REG_PLL_SCGR2,    0x00);
    tda_write(REG_PLL_SCG2,     0x10);

    /* Verify PLL writes BEFORE pixel clock */
    {
        uint8_t sc = 0, sn = 0, s2 = 0;
        tda_read(REG_SEL_CLK, &sc);
        tda_read(REG_PLL_SCGN1, &sn);
        tda_read(REG_PLL_SCG2, &s2);
        uart_printf("[TDA] PLL pre-LCDC: SEL_CLK=0x%02x SCGN1=0x%02x SCG2=0x%02x\n",
                    sc, sn, s2);
    }

    /* Default MUX register */
    tda_write(REG_MUX_VP_VIP_OUT, 0x24);

    /* Read version */
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

    /* Disable multi-master on I2C */
    tda_set(REG_I2C_MASTER, I2C_MASTER_DIS_MM);

    /* FRO clock config */
    tda_cec_write(TDA_CEC_FRO_IM_CLK_CTRL,
                  FRO_IM_CLK_CTRL_GHOST_DIS | FRO_IM_CLK_CTRL_IMCLK_SEL);

    uart_printf("[TDA] Probe complete (PLL written before LCDC)\n");
}

/* ============================================================
 * AVI InfoFrame — mandatory in HDMI mode
 * Without this, TV will blank after detecting the source.
 * ============================================================ */

static void tda_write_avi_infoframe(void)
{
    /* NXP BSL pattern: disable IF2 → write all bytes → enable IF2 */
    uint8_t buf[17];
    uint8_t sum;

    /* Step 1: Disable IF2 transmission before writing */
    tda_clear(REG_DIP_IF_FLAGS, DIP_IF_FLAGS_IF1);

    /* Step 2: Build AVI InfoFrame for 720p RGB (CEA-861 VIC=4) */
    buf[0]  = 0x82;   /* HB0: AVI InfoFrame type */
    buf[1]  = 0x02;   /* HB1: version 2 */
    buf[2]  = 0x0D;   /* HB2: length = 13 bytes */
    buf[4]  = 0x10;   /* PB1: Y=00 (RGB), A0=1 (active format valid) */
    buf[5]  = 0x18;   /* PB2: C=00 (default), M=01 (4:3), R=1000 (same as coded) */
    buf[6]  = 0x00;   /* PB3 */
    buf[7]  = 0x01;   /* PB4: VIC = 1 (640x480 60Hz) */
    buf[8]  = 0x00;   /* PB5: no pixel repetition */
    buf[9]  = 0x00;   buf[10] = 0x00;
    buf[11] = 0x00;   buf[12] = 0x00;
    buf[13] = 0x00;   buf[14] = 0x00;
    buf[15] = 0x00;   buf[16] = 0x00;

    /* Checksum: sum of all bytes (including PB0) must be 0 mod 256 */
    sum = 0;
    for (int i = 0; i < 17; i++) {
        if (i == 3) continue;
        sum += buf[i];
    }
    buf[3] = (uint8_t)(256 - sum);

    /* Step 3: Write all 17 bytes to IF2 registers */
    for (int i = 0; i < 17; i++) {
        tda_write(REG_AVI_IF + i, buf[i]);
    }

    /* Step 4: Enable IF2 transmission */
    tda_set(REG_DIP_IF_FLAGS, DIP_IF_FLAGS_IF1);

    uart_printf("[TDA] AVI InfoFrame: checksum=0x%02x, PB1=0x%02x PB2=0x%02x VIC=%d\n",
                buf[3], buf[4], buf[5], buf[7]);
}

/* ============================================================
 * Enable: video path configuration + output enable
 * ============================================================ */

static void tda_enable_video(void)
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

    /* Disable output during config, set HDMI mode (not DVI).
     * Modern TVs on HDMI input often require HDMI signaling — DVI mode
     * causes TV to detect source but show black screen. */
    tda_set(REG_TBG_CNTRL_1, TBG_CNTRL_1_DWIN_DIS);
    tda_set(REG_TX33, TX33_HDMI);
    tda_write(REG_ENC_CNTRL, ENC_CNTRL_CTL_CODE(0));

    uart_printf("[TDA] enable_720p: step 3 — video path config\n");

    /* No pre-filter or interpolator */
    tda_write(REG_HVF_CNTRL_0, HVF_CNTRL_0_PREFIL(0) | HVF_CNTRL_0_INTPOL(0));

    /* QNX does NOT write FEAT_POWERDOWN — leave at default */

    tda_write(REG_VIP_CNTRL_5, VIP_CNTRL_5_SP_CNT(0));
    /* No test pattern — pass through LCDC pixel data (QNX default) */
    tda_write(REG_VIP_CNTRL_4, VIP_CNTRL_4_BLANKIT(0) | VIP_CNTRL_4_BLC(0));

    uart_printf("[TDA] enable_720p: step 3 — PLL + serializer config (page 0x02)\n");

    /* QNX uses read-modify-write clears for PLL_SERIAL bits */
    tda_clear(REG_PLL_SERIAL_3, PLL_SERIAL_3_SRL_CCIR);
    tda_clear(REG_PLL_SERIAL_1, PLL_SERIAL_1_SRL_MAN_IZ);
    tda_clear(REG_PLL_SERIAL_3, PLL_SERIAL_3_SRL_DE);
    tda_write(REG_SERIALIZER, 0);
    tda_write(REG_HVF_CNTRL_1, HVF_CNTRL_1_VQR(0));
    tda_write(REG_RPT_CNTRL, 0);
    tda_write(REG_SEL_CLK, SEL_CLK_SEL_VRF_CLK(0) |
              SEL_CLK_SEL_CLK1 | SEL_CLK_ENA_SC_CLK);
    /* NOSC = 148500/pixel_clock_kHz - 1, clamped to 3.
     * 640x480@25MHz: 148500/25000=5, -1=4, clamp=3 */
    tda_write(REG_PLL_SERIAL_2, PLL_SERIAL_2_SRL_NOSC(3) |
              PLL_SERIAL_2_SRL_PR(0));

    /* Color matrix: bypass only (QNX: reg_set MAT_BP, no MAT_SC change) */
    tda_set(REG_MAT_CONTRL, MAT_CONTRL_MAT_BP);

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

    tda_write16(REG_NPIX_MSB,  TVGA_HTOTAL);                      /* 800  */
    tda_write16(REG_NLINE_MSB, TVGA_VTOTAL);                     /* 525  */

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

    tda_clear(REG_TBG_CNTRL_0, TBG_CNTRL_0_SYNC_MTHD);

    /* VIP_CNTRL_3: sync on HSYNC.
     * 640x480 has NHSYNC/NVSYNC → toggle both to make TDA see positive.
     * QNX (hdmi.c line 563-565): set H_TGL for NHSYNC, V_TGL for NVSYNC. */
    tda_write(REG_VIP_CNTRL_3, 0);
    tda_set(REG_VIP_CNTRL_3, VIP_CNTRL_3_SYNC_HS);
    tda_set(REG_VIP_CNTRL_3, VIP_CNTRL_3_H_TGL);
    tda_set(REG_VIP_CNTRL_3, VIP_CNTRL_3_V_TGL);

    /* TBG_CNTRL_1: TGL_EN + revert the toggles at output stage.
     * QNX (hdmi.c line 571-575): always TGL_EN, add H_TGL/V_TGL
     * for negative sync modes to undo the input toggle. */
    tda_write(REG_TBG_CNTRL_1, TBG_CNTRL_1_TGL_EN |
              TBG_CNTRL_1_H_TGL | TBG_CNTRL_1_V_TGL);

    /* Enable HDMI + encoder */
    tda_clear(REG_TBG_CNTRL_1, TBG_CNTRL_1_DWIN_DIS);
    tda_write(REG_ENC_CNTRL, ENC_CNTRL_CTL_CODE(1));

    /* MUST BE LAST register set (QNX line 607) */
    tda_clear(REG_TBG_CNTRL_0, TBG_CNTRL_0_SYNC_ONCE);

    /* AVI InfoFrame — write AFTER video path is fully enabled.
     * NXP BSL pattern: disable IF2 → write → enable IF2. */
    tda_write_avi_infoframe();

    uart_printf("[TDA] enable_720p: step 6 — TBG + timing + AVI done\n");
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
    uint8_t v = 0;

    uart_printf("[TDA] Initializing TDA19988 HDMI transmitter\n");

    /* Step 1: Probe — CEC enable, reset, PLL common config, version */
    tda_probe();

    /* Step 2: HPD check */
    if (i2c_read_reg(TDA_CEC_I2C_ADDR, TDA_CEC_RXSHPDLEV, &v) == 0) {
        uart_printf("[TDA] HPD=%d  RXSENS=%d\n", v & 1, (v >> 1) & 1);
    }

    /* Step 3: Enable ports + VIP mux + full video path (QNX: dpms → mode_set)
     * This runs BEFORE LCDC raster starts, matching QNX init_hdmi() order. */
    tda_enable_video();
    uart_printf("[TDA] Video path configured (640x480@60Hz)\n");
    uart_printf("[TDA] Waiting for LCDC raster to provide pixel clock + data\n");

    /* Comprehensive diagnostic readback */
    {
        uint8_t v = 0;
        tda_read(REG_PLL_SERIAL_2, &v);
        uart_printf("[TDA] DIAG: PLL_SERIAL_2=0x%02x\n", v);
        tda_read(REG_SEL_CLK, &v);
        uart_printf("[TDA] DIAG: SEL_CLK=0x%02x\n", v);
        tda_read(REG_ANA_GENERAL, &v);
        uart_printf("[TDA] DIAG: ANA_GENERAL=0x%02x\n", v);
        tda_read(REG_VIP_CNTRL_0, &v);
        uart_printf("[TDA] DIAG: VIP_CNTRL_0=0x%02x\n", v);
        tda_read(REG_VIP_CNTRL_1, &v);
        uart_printf("[TDA] DIAG: VIP_CNTRL_1=0x%02x\n", v);
        tda_read(REG_VIP_CNTRL_2, &v);
        uart_printf("[TDA] DIAG: VIP_CNTRL_2=0x%02x\n", v);
        tda_read(REG_VIP_CNTRL_3, &v);
        uart_printf("[TDA] DIAG: VIP_CNTRL_3=0x%02x\n", v);
        tda_read(REG_VIP_CNTRL_4, &v);
        uart_printf("[TDA] DIAG: VIP_CNTRL_4=0x%02x\n", v);
        tda_read(REG_TBG_CNTRL_0, &v);
        uart_printf("[TDA] DIAG: TBG_CNTRL_0=0x%02x\n", v);
        tda_read(REG_TBG_CNTRL_1, &v);
        uart_printf("[TDA] DIAG: TBG_CNTRL_1=0x%02x\n", v);
        tda_read(REG_TX33, &v);
        uart_printf("[TDA] DIAG: TX33=0x%02x (HDMI=%d)\n", v, (v >> 1) & 1);
        tda_read(REG_ENC_CNTRL, &v);
        uart_printf("[TDA] DIAG: ENC_CNTRL=0x%02x\n", v);
        tda_read(REG_MUX_VP_VIP_OUT, &v);
        uart_printf("[TDA] DIAG: MUX_VP_VIP_OUT=0x%02x\n", v);
        tda_read(REG_MAT_CONTRL, &v);
        uart_printf("[TDA] DIAG: MAT_CONTRL=0x%02x\n", v);
        tda_read(REG_DIP_IF_FLAGS, &v);
        uart_printf("[TDA] DIAG: DIP_IF_FLAGS=0x%02x\n", v);
        tda_read(REG_FEAT_POWERDOWN, &v);
        uart_printf("[TDA] DIAG: FEAT_POWERDOWN=0x%02x\n", v);

        if (i2c_read_reg(TDA_CEC_I2C_ADDR, TDA_CEC_RXSHPDLEV, &v) == 0) {
            uart_printf("[TDA] DIAG: HPD=%d RXSENS=%d\n", v & 1, (v >> 1) & 1);
        }
    }
}
