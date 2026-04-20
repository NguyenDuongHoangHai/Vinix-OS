/* ============================================================
 * i2c.c
 * ------------------------------------------------------------
 * AM335x I2C0 polling-mode driver — 7-bit master TX/RX.
 * ============================================================ */

#include "types.h"
#include "i2c.h"
#include "mmio.h"
#include "uart.h"

/* ============================================================
 * Pinmux (Control Module)
 * ============================================================ */

/* Control module base for pad configuration */
#define CTRL_MOD_BASE           0x44E10000
/* Dedicated I2C0 pads — mode 0 = I2C0 directly (not muxed from UART) */
#define CONF_I2C0_SDA           (CTRL_MOD_BASE + 0x988)
#define CONF_I2C0_SCL           (CTRL_MOD_BASE + 0x98C)
/* mode 0 | pullup enabled | RX active | slow slew */
#define PAD_I2C0_MODE           0x70

/* ============================================================
 * Clock Management
 * ============================================================ */

/* I2C0 is in Wakeup domain — CM_WKUP */
#define CM_WKUP_BASE            0x44E00400
#define CM_WKUP_I2C0_CLKCTRL   (CM_WKUP_BASE + 0xB8)

/* Module mode bits */
#define MODULEMODE_ENABLE       0x2
#define IDLEST_SHIFT            16
#define IDLEST_MASK             0x3
#define IDLEST_FUNC             0x0

/* ============================================================
 * I2C0 Register Offsets
 * ============================================================ */

#define I2C_REVNB_LO    0x00    /* Module revision (low) */
#define I2C_REVNB_HI    0x04    /* Module revision (high) */
#define I2C_SYSC        0x10    /* System configuration */
#define I2C_IRQSTATUS_RAW 0x24  /* IRQ raw status */
#define I2C_IRQSTATUS   0x28    /* IRQ status */
#define I2C_IRQENABLE_SET 0x2C  /* IRQ enable set */
#define I2C_IRQENABLE_CLR 0x30  /* IRQ enable clear */
#define I2C_SYSS        0x90    /* System status */
#define I2C_BUF         0x94    /* Buffer configuration */
#define I2C_CNT         0x98    /* Data counter */
#define I2C_DATA        0x9C    /* Data access */
#define I2C_CON         0xA4    /* Configuration */
#define I2C_OA          0xA8    /* Own address */
#define I2C_SA          0xAC    /* Slave address */
#define I2C_PSC         0xB0    /* Clock prescaler */
#define I2C_SCLL        0xB4    /* SCL low time */
#define I2C_SCLH        0xB8    /* SCL high time */
#define I2C_BUFSTAT     0xC0    /* Buffer status */
#define I2C_SYSTEST     0xBC    /* System test register */

/* ============================================================
 * I2C_SYSC bits
 * ============================================================ */

#define I2C_SYSC_SRST   (1 << 1)    /* Software reset */

/* ============================================================
 * I2C_SYSS bits
 * ============================================================ */

#define I2C_SYSS_RDONE  (1 << 0)    /* Reset done */

/* ============================================================
 * I2C_CON bits
 * ============================================================ */

#define I2C_CON_EN      (1 << 15)   /* Module enable */
#define I2C_CON_MST     (1 << 10)   /* Master mode */
#define I2C_CON_TRX     (1 << 9)    /* Transmitter mode */
#define I2C_CON_STP     (1 << 1)    /* Stop condition */
#define I2C_CON_STT     (1 << 0)    /* Start condition */

/* ============================================================
 * I2C_IRQSTATUS bits
 * ============================================================ */

#define I2C_STAT_BB     (1 << 12)   /* Bus busy */
#define I2C_STAT_ROVR   (1 << 11)   /* Receive overrun */
#define I2C_STAT_XUDF   (1 << 10)   /* Transmit underflow */
#define I2C_STAT_AAS    (1 << 9)    /* Addressed as slave */
#define I2C_STAT_BF     (1 << 8)    /* Bus free */
#define I2C_STAT_AERR   (1 << 7)    /* Access error */
#define I2C_STAT_GC     (1 << 5)    /* General call */
#define I2C_STAT_XRDY   (1 << 4)    /* Transmit data ready */
#define I2C_STAT_RRDY   (1 << 3)    /* Receive data ready */
#define I2C_STAT_ARDY   (1 << 2)    /* Register access ready */
#define I2C_STAT_NACK   (1 << 1)    /* No acknowledgement */
#define I2C_STAT_AL     (1 << 0)    /* Arbitration lost */

/* ============================================================
 * Timeout
 * ============================================================ */

#define I2C_TIMEOUT     100000

/* ============================================================
 * Internal Helpers
 * ============================================================ */

/**
 * Wait until bus is not busy.
 * Polls BB bit in IRQSTATUS_RAW.
 */
static int i2c_wait_bus_free(void)
{
    uint32_t timeout = I2C_TIMEOUT;

    while ((mmio_read32(I2C0_BASE + I2C_IRQSTATUS_RAW) & I2C_STAT_BB) && timeout--) {
        /* Busy wait */
    }

    if (!timeout) {
        uart_printf("[I2C] ERROR: Bus busy timeout\n");
        return -1;
    }
    return 0;
}

/**
 * Wait for specific status flag(s) to be set.
 * Returns raw status on success, 0 on timeout.
 */
static uint32_t i2c_wait_status(uint32_t mask)
{
    uint32_t timeout = I2C_TIMEOUT;
    uint32_t stat;

    while (timeout--) {
        stat = mmio_read32(I2C0_BASE + I2C_IRQSTATUS_RAW);
        if (stat & (mask | I2C_STAT_NACK | I2C_STAT_AL)) {
            return stat;
        }
    }
    return 0;
}

/**
 * Clear all interrupt status flags.
 */
static void i2c_clear_status(void)
{
    mmio_write32(I2C0_BASE + I2C_IRQSTATUS, 0x7FFF);
}

/**
 * Flush FIFO and reset I2C state machine after a failed transaction.
 * Forces STOP on the bus before resetting the module to recover
 * from stuck-bus conditions (e.g. slave holding SDA low).
 */
static void i2c_flush(void)
{
    /* Force STOP condition to release any stuck slave */
    mmio_write32(I2C0_BASE + I2C_CON, I2C_CON_EN | I2C_CON_MST | I2C_CON_STP);
    for (volatile int i = 0; i < 500; i++);

    /* Disable module to reset internal FIFO and state machine */
    mmio_write32(I2C0_BASE + I2C_CON, 0);
    for (volatile int i = 0; i < 100; i++);

    /* Re-enable with EN only — do NOT restore STT/STP, that would
     * immediately trigger a new transaction with stale state */
    mmio_write32(I2C0_BASE + I2C_CON, I2C_CON_EN);
    for (volatile int i = 0; i < 100; i++);

    i2c_clear_status();
}

/* ============================================================
 * I2C Initialization
 * ============================================================ */

void i2c_init(void)
{
    uint32_t val;
    uint32_t timeout;

    uart_printf("[I2C] Initializing I2C0...\n");

    /* Step 0: Configure dedicated I2C0 pads (mode 0, pullup, RX active)
     * ROM bootloader already sets these for PMIC access; we re-assert to own state */
    mmio_write32(CONF_I2C0_SCL, PAD_I2C0_MODE);
    mmio_write32(CONF_I2C0_SDA, PAD_I2C0_MODE);
    uart_printf("[I2C] Pinmux configured (SCL=0x%x, SDA=0x%x)\n",
                mmio_read32(CONF_I2C0_SCL), mmio_read32(CONF_I2C0_SDA));

    /* Step 1: Enable I2C0 module clock
     * I2C0 is in Wakeup domain (CM_WKUP_I2C0_CLKCTRL)
     * Functional clock = 48MHz (PER_CLKOUTM2 / 4) */
    mmio_write32(CM_WKUP_I2C0_CLKCTRL, MODULEMODE_ENABLE);

    timeout = I2C_TIMEOUT;
    while (timeout--) {
        val = mmio_read32(CM_WKUP_I2C0_CLKCTRL);
        if (((val >> IDLEST_SHIFT) & IDLEST_MASK) == IDLEST_FUNC) {
            break;
        }
    }
    if (!timeout) {
        uart_printf("[I2C] ERROR: Clock enable timeout\n");
        return;
    }
    uart_printf("[I2C] Clock enabled\n");

    /* Step 2: Disable module before configuration */
    mmio_write32(I2C0_BASE + I2C_CON, 0);

    /* Step 3: Software reset */
    mmio_write32(I2C0_BASE + I2C_SYSC, I2C_SYSC_SRST);

    timeout = I2C_TIMEOUT;
    while (!(mmio_read32(I2C0_BASE + I2C_SYSS) & I2C_SYSS_RDONE) && timeout--) {
        /* Wait for reset completion */
    }
    if (!timeout) {
        uart_printf("[I2C] ERROR: Soft reset timeout\n");
        return;
    }
    uart_printf("[I2C] Soft reset complete\n");

    /* Step 4: Configure prescaler
     * ICLK = SCLK / (PSC + 1)
     * SCLK = 48MHz (functional clock)
     * PSC = 3 → ICLK = 48MHz / 4 = 12MHz
     * ~12MHz recommended for standard/fast mode */
    mmio_write32(I2C0_BASE + I2C_PSC, 3);

    /* Step 5: Configure SCL timing for ~100kHz standard mode
     * tLOW  = (SCLL + 7) × ICLK_period
     * tHIGH = (SCLH + 5) × ICLK_period
     * For 100kHz: period = 10us → tLOW = tHIGH = 5us
     * SCLL = (5us × 12MHz) - 7 = 60 - 7 = 53
     * SCLH = (5us × 12MHz) - 5 = 60 - 5 = 55 */
    mmio_write32(I2C0_BASE + I2C_SCLL, 53);
    mmio_write32(I2C0_BASE + I2C_SCLH, 55);

    /* Step 6: Set own address (arbitrary, we are master) */
    mmio_write32(I2C0_BASE + I2C_OA, 0x01);

    /* Step 7: Clear all interrupt status */
    i2c_clear_status();

    /* Step 8: Disable all interrupts (polling mode) */
    mmio_write32(I2C0_BASE + I2C_IRQENABLE_CLR, 0x7FFF);

    /* Step 9: Enable module */
    mmio_write32(I2C0_BASE + I2C_CON, I2C_CON_EN);

    /* Small delay for module to stabilize */
    for (volatile int i = 0; i < 1000; i++);

    /* Verify: read module revision */
    val = mmio_read32(I2C0_BASE + I2C_REVNB_LO);
    uart_printf("[I2C] Module revision = 0x%x\n", val);

    /* Physical bus state: SCL_I_FUNC=bit8, SDA_I_FUNC=bit6
     * Both should be 1 (pulled high) when bus is idle.
     * If either is 0 → stuck low → hardware/pinmux problem. */
    val = mmio_read32(I2C0_BASE + I2C_SYSTEST);
    uart_printf("[I2C] SYSTEST=0x%x  SCL_I=%d  SDA_I=%d  (1=high=OK)\n",
                val, (val >> 8) & 1, (val >> 6) & 1);

    uart_printf("[I2C] I2C0 initialized (100kHz standard mode)\n");
}

/* ============================================================
 * Write Register
 * ============================================================
 *
 * I2C write transaction: [S] [slave_addr+W] [reg] [val] [P]
 * Master transmits 2 bytes: register address, then value.
 */

int i2c_write_reg(uint8_t slave_addr, uint8_t reg, uint8_t val)
{
    uint32_t stat;

    /* Wait for bus free */
    if (i2c_wait_bus_free() != 0) {
        i2c_flush();
        return -1;
    }

    i2c_clear_status();

    /* Set slave address */
    mmio_write32(I2C0_BASE + I2C_SA, slave_addr);

    /* Set data count = 2 (register addr + value) */
    mmio_write32(I2C0_BASE + I2C_CNT, 2);

    /* Configure: master, transmitter, start, stop */
    mmio_write32(I2C0_BASE + I2C_CON,
                 I2C_CON_EN | I2C_CON_MST | I2C_CON_TRX | I2C_CON_STT | I2C_CON_STP);

    /* Send register address */
    stat = i2c_wait_status(I2C_STAT_XRDY);
    if (!stat || (stat & I2C_STAT_NACK)) {
        uart_printf("[I2C] NACK on write addr 0x%x reg 0x%x\n", slave_addr, reg);
        i2c_flush();
        return -1;
    }
    mmio_write32(I2C0_BASE + I2C_DATA, reg);
    mmio_write32(I2C0_BASE + I2C_IRQSTATUS, I2C_STAT_XRDY);

    /* Send value */
    stat = i2c_wait_status(I2C_STAT_XRDY);
    if (!stat || (stat & I2C_STAT_NACK)) {
        uart_printf("[I2C] NACK on write data 0x%x reg 0x%x\n", slave_addr, reg);
        i2c_flush();
        return -1;
    }
    mmio_write32(I2C0_BASE + I2C_DATA, val);
    mmio_write32(I2C0_BASE + I2C_IRQSTATUS, I2C_STAT_XRDY);

    /* Wait for transfer complete (ARDY = access ready) */
    stat = i2c_wait_status(I2C_STAT_ARDY);
    if (!stat) {
        uint32_t raw = mmio_read32(I2C0_BASE + I2C_IRQSTATUS_RAW);
        uart_printf("[I2C] ARDY timeout write sa=0x%x reg=0x%x raw=0x%x\n",
                    slave_addr, reg, raw);
        i2c_flush();
        return -1;
    }
    mmio_write32(I2C0_BASE + I2C_IRQSTATUS, I2C_STAT_ARDY);

    return 0;
}

/* ============================================================
 * Read Register
 * ============================================================
 *
 * I2C read with repeated start:
 *   Phase 1: [S] [slave_addr+W] [reg]          (set register pointer)
 *   Phase 2: [Sr] [slave_addr+R] [data] [P]    (read data)
 */

int i2c_read_reg(uint8_t slave_addr, uint8_t reg, uint8_t *val)
{
    uint32_t stat;

    /* Wait for bus free */
    if (i2c_wait_bus_free() != 0) {
        i2c_flush();
        return -1;
    }

    i2c_clear_status();

    /* === Phase 1: Write register address (no stop) === */

    mmio_write32(I2C0_BASE + I2C_SA, slave_addr);
    mmio_write32(I2C0_BASE + I2C_CNT, 1);

    /* Master, transmitter, start, NO stop (repeated start follows) */
    mmio_write32(I2C0_BASE + I2C_CON,
                 I2C_CON_EN | I2C_CON_MST | I2C_CON_TRX | I2C_CON_STT);

    /* Wait for XRDY and send register address */
    stat = i2c_wait_status(I2C_STAT_XRDY);
    if (!stat || (stat & I2C_STAT_NACK)) {
        uart_printf("[I2C] NACK on read phase1 addr 0x%x reg 0x%x\n", slave_addr, reg);
        i2c_flush();
        return -1;
    }
    mmio_write32(I2C0_BASE + I2C_DATA, reg);
    mmio_write32(I2C0_BASE + I2C_IRQSTATUS, I2C_STAT_XRDY);

    /* Wait for ARDY (register address sent) */
    stat = i2c_wait_status(I2C_STAT_ARDY);
    if (!stat) {
        uart_printf("[I2C] Timeout on read phase1 ARDY\n");
        i2c_flush();
        return -1;
    }
    mmio_write32(I2C0_BASE + I2C_IRQSTATUS, I2C_STAT_ARDY);

    /* === Phase 2: Read data (repeated start + stop) === */

    i2c_clear_status();
    mmio_write32(I2C0_BASE + I2C_CNT, 1);

    /* Master, receiver (TRX=0), start, stop */
    mmio_write32(I2C0_BASE + I2C_CON,
                 I2C_CON_EN | I2C_CON_MST | I2C_CON_STT | I2C_CON_STP);

    /* Wait for RRDY */
    stat = i2c_wait_status(I2C_STAT_RRDY);
    if (!stat || (stat & I2C_STAT_NACK)) {
        uart_printf("[I2C] NACK on read phase2 addr 0x%x reg 0x%x\n", slave_addr, reg);
        i2c_flush();
        return -1;
    }
    *val = (uint8_t)(mmio_read32(I2C0_BASE + I2C_DATA) & 0xFF);
    mmio_write32(I2C0_BASE + I2C_IRQSTATUS, I2C_STAT_RRDY);

    /* Wait for transfer complete */
    stat = i2c_wait_status(I2C_STAT_ARDY);
    if (stat) {
        mmio_write32(I2C0_BASE + I2C_IRQSTATUS, I2C_STAT_ARDY);
    }

    return 0;
}

/* ============================================================
 * Read Block
 * ============================================================
 *
 * Same as read_reg but reads multiple bytes in phase 2.
 * Used for EDID reading (128/256 bytes).
 */

int i2c_read_block(uint8_t slave_addr, uint8_t reg, uint8_t *buf, int len)
{
    uint32_t stat;
    int i;

    if (len <= 0 || buf == NULL) {
        return -1;
    }

    /* Wait for bus free */
    if (i2c_wait_bus_free() != 0) {
        i2c_flush();
        return -1;
    }

    i2c_clear_status();

    /* === Phase 1: Write register address === */

    mmio_write32(I2C0_BASE + I2C_SA, slave_addr);
    mmio_write32(I2C0_BASE + I2C_CNT, 1);
    mmio_write32(I2C0_BASE + I2C_CON,
                 I2C_CON_EN | I2C_CON_MST | I2C_CON_TRX | I2C_CON_STT);

    stat = i2c_wait_status(I2C_STAT_XRDY);
    if (!stat || (stat & I2C_STAT_NACK)) {
        i2c_flush();
        return -1;
    }
    mmio_write32(I2C0_BASE + I2C_DATA, reg);
    mmio_write32(I2C0_BASE + I2C_IRQSTATUS, I2C_STAT_XRDY);

    stat = i2c_wait_status(I2C_STAT_ARDY);
    if (!stat) {
        i2c_flush();
        return -1;
    }
    mmio_write32(I2C0_BASE + I2C_IRQSTATUS, I2C_STAT_ARDY);

    /* === Phase 2: Read len bytes === */

    i2c_clear_status();
    mmio_write32(I2C0_BASE + I2C_CNT, len);
    mmio_write32(I2C0_BASE + I2C_CON,
                 I2C_CON_EN | I2C_CON_MST | I2C_CON_STT | I2C_CON_STP);

    for (i = 0; i < len; i++) {
        stat = i2c_wait_status(I2C_STAT_RRDY);
        if (!stat || (stat & I2C_STAT_NACK)) {
            uart_printf("[I2C] Block read error at byte %d\n", i);
            i2c_flush();
            return -1;
        }
        buf[i] = (uint8_t)(mmio_read32(I2C0_BASE + I2C_DATA) & 0xFF);
        mmio_write32(I2C0_BASE + I2C_IRQSTATUS, I2C_STAT_RRDY);
    }

    /* Wait for transfer complete */
    stat = i2c_wait_status(I2C_STAT_ARDY);
    if (stat) {
        mmio_write32(I2C0_BASE + I2C_IRQSTATUS, I2C_STAT_ARDY);
    }

    return 0;
}

/* ============================================================
 * Bus Scan (Diagnostic)
 * ============================================================
 *
 * Probes addresses 0x08-0x77. For each address, attempts a
 * 1-byte write and checks for ACK vs NACK.
 * Prints which addresses respond — helps confirm whether
 * TDA19988 (0x34/0x70) is on this I2C bus.
 */
void i2c_scan(void)
{
    uint8_t addr;
    int found = 0;

    uart_printf("[I2C] Bus scan starting (0x08-0x77)...\n");

    for (addr = 0x08; addr <= 0x77; addr++) {
        uint32_t stat;

        if (i2c_wait_bus_free() != 0) {
            i2c_flush();
            continue;
        }

        i2c_clear_status();
        mmio_write32(I2C0_BASE + I2C_SA, addr);
        mmio_write32(I2C0_BASE + I2C_CNT, 1);
        mmio_write32(I2C0_BASE + I2C_CON,
                     I2C_CON_EN | I2C_CON_MST | I2C_CON_TRX |
                     I2C_CON_STT | I2C_CON_STP);

        stat = i2c_wait_status(I2C_STAT_XRDY);
        if (!stat || (stat & I2C_STAT_NACK)) {
            mmio_write32(I2C0_BASE + I2C_IRQSTATUS, stat);
            i2c_flush();
            continue;
        }

        /* Write dummy byte */
        mmio_write32(I2C0_BASE + I2C_DATA, 0x00);
        mmio_write32(I2C0_BASE + I2C_IRQSTATUS, I2C_STAT_XRDY);

        stat = i2c_wait_status(I2C_STAT_ARDY);
        if (stat && !(stat & I2C_STAT_NACK)) {
            uart_printf("[I2C] FOUND device at 0x%x\n", addr);
            found++;
        }
        mmio_write32(I2C0_BASE + I2C_IRQSTATUS, stat);
        i2c_flush();
    }

    if (!found)
        uart_printf("[I2C] Bus scan: no devices found\n");
    else
        uart_printf("[I2C] Bus scan complete: %d device(s)\n", found);
}
