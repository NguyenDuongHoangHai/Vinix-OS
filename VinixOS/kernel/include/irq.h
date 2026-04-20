/* ============================================================
 * irq.h
 * ------------------------------------------------------------
 * IRQ framework interface.
 * ============================================================ */

#ifndef IRQ_H
#define IRQ_H

#include "types.h"

/* ============================================================
 * IRQ Configuration
 * ============================================================ */

/* Maximum number of IRQ sources on AM335x */
#define MAX_IRQS    128

/* Common IRQ numbers (AM335x specific) */
#define IRQ_TIMER0      66
#define IRQ_TIMER1      67
#define IRQ_TIMER2      68
#define IRQ_TIMER3      69
#define IRQ_TIMER4      92
#define IRQ_TIMER5      93
#define IRQ_TIMER6      94
#define IRQ_TIMER7      95

#define IRQ_UART0       72
#define IRQ_UART1       73
#define IRQ_UART2       74
#define IRQ_UART3       44
#define IRQ_UART4       45
#define IRQ_UART5       46

#define IRQ_GPIO0A      96
#define IRQ_GPIO0B      97
#define IRQ_GPIO1A      98
#define IRQ_GPIO1B      99
#define IRQ_GPIO2A      32
#define IRQ_GPIO2B      33
#define IRQ_GPIO3A      62
#define IRQ_GPIO3B      63

/* ============================================================
 * IRQ Handler Type
 * ============================================================ */

typedef void (*irq_handler_t)(void *data);

/* ============================================================
 * IRQ Framework API
 * ============================================================ */

void irq_init(void);

/* Returns -1 if irq_num >= MAX_IRQS, handler is NULL, or the slot
 * is already taken. Must run before enabling the IRQ in INTC. */
int irq_register_handler(uint32_t irq_num, irq_handler_t handler, void *data);

/* Precondition: IRQ already disabled in INTC. */
void irq_unregister_handler(uint32_t irq_num);

/* CRITICAL: always sends EOI — even for spurious/unhandled IRQs. */
void irq_dispatch(void *ctx);

uint32_t irq_get_count(uint32_t irq_num);

#endif /* IRQ_H */