/* ============================================================
 * vinix/printk.h
 * ------------------------------------------------------------
 * Linux-style log facility wrappers — pr_info/pr_err/pr_warn/
 * pr_debug/pr_emerg + KERN_* loglevel string prefixes.
 *
 * Today everything funnels into uart_printf; KERN_* prefixes
 * are empty so output is unchanged. A future printk dispatcher
 * can parse the prefix to filter by level.
 * ============================================================ */

#ifndef VINIX_PRINTK_H
#define VINIX_PRINTK_H

#include "uart.h"

#define KERN_EMERG   ""
#define KERN_ALERT   ""
#define KERN_CRIT    ""
#define KERN_ERR     ""
#define KERN_WARN    ""
#define KERN_NOTICE  ""
#define KERN_INFO    ""
#define KERN_DEBUG   ""

#define printk(fmt, ...)         uart_printf(fmt, ##__VA_ARGS__)
#define pr_emerg(fmt, ...)       printk(KERN_EMERG  fmt, ##__VA_ARGS__)
#define pr_alert(fmt, ...)       printk(KERN_ALERT  fmt, ##__VA_ARGS__)
#define pr_crit(fmt, ...)        printk(KERN_CRIT   fmt, ##__VA_ARGS__)
#define pr_err(fmt, ...)         printk(KERN_ERR    fmt, ##__VA_ARGS__)
#define pr_warn(fmt, ...)        printk(KERN_WARN   fmt, ##__VA_ARGS__)
#define pr_notice(fmt, ...)      printk(KERN_NOTICE fmt, ##__VA_ARGS__)
#define pr_info(fmt, ...)        printk(KERN_INFO   fmt, ##__VA_ARGS__)
#define pr_debug(fmt, ...)       printk(KERN_DEBUG  fmt, ##__VA_ARGS__)

#endif /* VINIX_PRINTK_H */
