/* ============================================================
 * irq_core.c
 * ------------------------------------------------------------
 * Generic IRQ dispatcher — request_irq / free_irq table backed
 * by the INTC driver (drivers/irqchip).
 * ============================================================ */

#include "irq.h"
#include "intc.h"
#include "uart.h"
#include "types.h"

struct irq_desc {
    irq_handler_t  handler;
    void          *dev;
    const char    *name;
    unsigned long  flags;
    uint32_t       count;
};

static struct irq_desc irq_table[MAX_IRQS];

void irq_init(void)
{
    for (int i = 0; i < MAX_IRQS; i++) {
        irq_table[i].handler = NULL;
        irq_table[i].dev     = NULL;
        irq_table[i].name    = NULL;
        irq_table[i].flags   = 0;
        irq_table[i].count   = 0;
    }
}

int request_irq(unsigned int irq, irq_handler_t handler,
                unsigned long flags, const char *name, void *dev)
{
    if (irq >= MAX_IRQS)        return -1;
    if (handler == NULL)        return -1;
    if (irq_table[irq].handler) return -1;

    irq_table[irq].handler = handler;
    irq_table[irq].dev     = dev;
    irq_table[irq].name    = name;
    irq_table[irq].flags   = flags;
    irq_table[irq].count   = 0;
    return 0;
}

void free_irq(unsigned int irq, void *dev)
{
    if (irq >= MAX_IRQS) return;
    if (irq_table[irq].dev != dev) return;

    irq_table[irq].handler = NULL;
    irq_table[irq].dev     = NULL;
    irq_table[irq].name    = NULL;
    irq_table[irq].flags   = 0;
}

void enable_irq(unsigned int irq)
{
    intc_enable_irq(irq);
}

void disable_irq(unsigned int irq)
{
    intc_disable_irq(irq);
}

void irq_dispatch(void *ctx)
{
    (void)ctx;
    uint32_t irq = intc_get_active_irq();

    if (irq >= MAX_IRQS) {
        intc_eoi();
        return;
    }

    if (irq_table[irq].handler == NULL) {
        intc_eoi();
        return;
    }

    irq_table[irq].handler(irq_table[irq].dev);
    irq_table[irq].count++;
    intc_eoi();
}

uint32_t irq_get_count(uint32_t irq)
{
    if (irq >= MAX_IRQS) return 0;
    return irq_table[irq].count;
}
