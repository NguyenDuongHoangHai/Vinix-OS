/* ============================================================
 * timer.h
 * ------------------------------------------------------------
 * DMTimer driver interface
 * Target: AM335x DMTimer2 (test driver)
 * ============================================================ */

#ifndef TIMER_H
#define TIMER_H

#include "types.h"

/* ============================================================
 * Timer Configuration
 * ============================================================ */

/* DMTimer2 base address */
#define DMTIMER2_BASE       0x48040000

/* Timer IRQ number */
#define TIMER2_IRQ          68

/* ============================================================
 * Timer API
 * ============================================================ */

/**
 * Initialize Timer2
 * - Configures timer for periodic interrupts
 * - Registers IRQ handler
 * - Enables IRQ in INTC
 * - Starts timer
 * 
 * Note: Does NOT enable IRQ in CPSR (kernel_main must do this)
 */
void timer_init(void);

/**
 * Get timer tick count
 * @return Number of timer interrupts handled
 */
uint32_t timer_get_ticks(void);

/**
 * Early timer init — enables DMTimer2 clock + free-running mode.
 * Call once before any delay_ms() usage.
 * timer_init() later reconfigures for scheduler interrupts.
 */
void timer_early_init(void);

/**
 * Accurate millisecond delay using DMTimer2 hardware counter.
 * Requires timer_early_init() or timer_init() called first.
 * Polling-based — no interrupts needed.
 */
void delay_ms(uint32_t ms);

#endif /* TIMER_H */