/* ============================================================
 * uart.c
 * ------------------------------------------------------------
 * UART Driver
 * ============================================================ */

#include "types.h"
#include "uart.h"
#include "mmio.h"
#include "cpu.h"
#include "irq.h"
#include "intc.h"
#include "wait_queue.h"
#include <stdarg.h>

/* Waiters blocked in sys_read waiting for RX data. */
wait_queue_head_t uart_rx_wq = { .head = 0 };

/* Hardware Definitions */
#define UART0_BASE      0x44E09000
#define UART0_IRQ       72

/* Clock Module */
#define CM_PER_BASE             0x44E00000
#define CM_PER_UART0_CLKCTRL    (CM_PER_BASE + 0x6C)

/* UART Register Offsets */
#define UART_RHR        0x00
#define UART_THR        0x00
#define UART_IER        0x04
#define UART_IIR        0x08
#define UART_FCR        0x08
#define UART_LCR        0x0C
#define UART_LSR        0x14
#define UART_SCR        0x40

/* IER bits */
#define IER_RHR_IT      (1 << 0)

/* IIR bits */
#define IIR_IT_PENDING  (1 << 0)
#define IIR_IT_TYPE_MASK 0x3E

/* LSR bits */
#define LSR_DR          (1 << 0)
#define LSR_OE          (1 << 1)
#define LSR_PE          (1 << 2)
#define LSR_FE          (1 << 3)
#define LSR_BI          (1 << 4)
#define LSR_THRE        (1 << 5)
#define LSR_ERROR_MASK  (LSR_OE | LSR_PE | LSR_FE | LSR_BI)

/* FCR bits */
#define FCR_FIFO_EN     (1 << 0)
#define FCR_RX_CLR      (1 << 1)
#define FCR_TX_CLR      (1 << 2)

/* Ring Buffer */
struct uart_rx_buffer {
    uint8_t data[UART_RX_BUFFER_SIZE];
    volatile uint32_t head;
    volatile uint32_t tail;
    volatile uint32_t overflow;
};

static struct uart_rx_buffer rx_buffer;
static volatile uint32_t uart_irq_fire_count = 0;

/* ============================================================
 * RX Interrupt Handler
 * ============================================================
 */

static void uart_rx_irq_handler(void *data)
{
    uart_irq_fire_count++;
    
    uint32_t iir, lsr;
    uint8_t ch;
    
    /* Read IIR to identify interrupt type */
    iir = mmio_read32(UART0_BASE + UART_IIR);
    
    if (iir & IIR_IT_PENDING) {
        return;  /* No interrupt pending */
    }
    
    /* Extract interrupt type */
    uint32_t it_type = (iir & IIR_IT_TYPE_MASK) >> 1;
    
    /* Handle LINE_STS interrupt (IT_TYPE = 0x06) */
    /* This clears error flags by reading LSR */
    if (it_type == 0x06) {
        lsr = mmio_read32(UART0_BASE + UART_LSR);
        
        /* If no data available after reading LSR, we're done */
        if (!(lsr & LSR_DR)) {
            return;
        }
        /* Fall through to read data */
    }
    
    /* Read all available characters from RX FIFO */
    while (1) {
        lsr = mmio_read32(UART0_BASE + UART_LSR);
        
        /* Check for errors - discard character if error */
        if (lsr & LSR_ERROR_MASK) {
            (void)mmio_read32(UART0_BASE + UART_RHR);
            continue;
        }
        
        /* Check if data ready */
        if (!(lsr & LSR_DR)) {
            break;  /* No more data */
        }
        
        /* Read character */
        ch = mmio_read32(UART0_BASE + UART_RHR) & 0xFF;
        
        /* Store in ring buffer */
        uint32_t next_head = (rx_buffer.head + 1) % UART_RX_BUFFER_SIZE;
        
        if (next_head == rx_buffer.tail) {
            rx_buffer.overflow++;
            continue;  /* Buffer full, discard */
        }
        
        rx_buffer.data[rx_buffer.head] = ch;
        rx_buffer.head = next_head;

        /* Wake the first sys_read waiter per byte — lets shell proceed. */
        wake_up(&uart_rx_wq);
    }
}

/* ============================================================
 * UART Initialization
 * ============================================================
 */

void uart_init(void)
{
    rx_buffer.head = 0;
    rx_buffer.tail = 0;
    rx_buffer.overflow = 0;
}

void uart_enable_rx_interrupt(void)
{
    /* Enable UART0 module clock (CRITICAL!) */
    mmio_write32(CM_PER_UART0_CLKCTRL, 0x2);
    
    /* Wait for clock to be functional */
    while ((mmio_read32(CM_PER_UART0_CLKCTRL) & 0x30000) != 0);
    
    /* Save current line control register */
    uint32_t lcr_save = mmio_read32(UART0_BASE + UART_LCR);
    
    /* Ensure operational mode */
    mmio_write32(UART0_BASE + UART_LCR, lcr_save & 0x7F);
    
    /* Clear interrupt enable register */
    mmio_write32(UART0_BASE + UART_IER, 0x00);
    
    /* Configure FIFO - simple legacy mode
     * Enable FIFO + clear RX/TX + 8-char trigger */
    mmio_write32(UART0_BASE + UART_FCR, 0x07);
    
    /* Small delay for FIFO configuration to take effect */
    for (volatile int i = 0; i < 1000; i++);
    
    /* Configure supplementary control register for 1-char granularity */
    mmio_write32(UART0_BASE + UART_SCR, 0xC0);
    
    /* Restore line control register */
    mmio_write32(UART0_BASE + UART_LCR, lcr_save);
    
    /* Enable RX interrupt */
    mmio_write32(UART0_BASE + UART_IER, IER_RHR_IT);
    
    /* Register IRQ handler */
    int ret = request_irq(UART0_IRQ, uart_rx_irq_handler, 0, "omap-uart", NULL);
    if (ret != 0) {
        return;  /* Registration failed */
    }
    
    /* Enable IRQ in interrupt controller */
    enable_irq(UART0_IRQ);
}

/* ============================================================
 * TX Functions
 * ============================================================
 */

void uart_putc(char c)
{
    while (!(mmio_read32(UART0_BASE + UART_LSR) & LSR_THRE));
    mmio_write32(UART0_BASE + UART_THR, c);
}

void uart_puts(const char *s)
{
    while (*s) {
        if (*s == '\n')
            uart_putc('\r');
        uart_putc(*s++);
    }
}

/* ============================================================
 * RX Functions
 * ============================================================
 */

int uart_getc(void)
{
    static uint32_t call_count = 0;
    call_count++;

    uint32_t flags = irq_save();
    
    if (rx_buffer.head == rx_buffer.tail) {
        irq_restore(flags);
        return -1;
    }
    
    uint8_t ch = rx_buffer.data[rx_buffer.tail];
    rx_buffer.tail = (rx_buffer.tail + 1) % UART_RX_BUFFER_SIZE;
    
    irq_restore(flags);
    return (int)ch;
}

int uart_rx_available(void)
{
    uint32_t head = rx_buffer.head;
    uint32_t tail = rx_buffer.tail;
    
    if (head >= tail) {
        return head - tail;
    } else {
        return UART_RX_BUFFER_SIZE - tail + head;
    }
}

void uart_rx_clear(void)
{
    uint32_t flags = irq_save();
    rx_buffer.head = 0;
    rx_buffer.tail = 0;
    irq_restore(flags);
}

uint32_t uart_get_irq_fire_count(void)
{
    return uart_irq_fire_count;
}

/* ============================================================
 * Platform driver wiring
 * ============================================================ */

#include "platform_device.h"
#include "platform_drivers.h"

static int omap_uart_probe(struct platform_device *pdev)
{
    struct resource *mem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
    int irq = platform_get_irq(pdev, 0);
    uart_printf("[UART] probing %s @ 0x%08x irq %d\n",
                pdev->name, mem ? mem->start : 0, irq);
    uart_init();
    return 0;
}

static struct platform_driver omap_uart_driver = {
    .drv   = { .name = "omap-uart" },
    .probe = omap_uart_probe,
};

#include "vinix/init.h"
static int __init omap_uart_driver_init(void)
{
    return platform_driver_register(&omap_uart_driver);
}
arch_initcall(omap_uart_driver_init);