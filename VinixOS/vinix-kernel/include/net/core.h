/* ============================================================
 * net/core.h
 * ------------------------------------------------------------
 * Net subsystem entry points — called from init/main.c.
 * ============================================================ */

#ifndef NET_CORE_H
#define NET_CORE_H

struct net_device;

/* Called BEFORE do_initcalls(6) — prepares rx ring and wait queue. */
void net_init(void);

/* Called AFTER do_initcalls(6) — configures ARP with device MAC + static IP. */
void net_configure(void);

void net_rx_task_entry(void);

struct net_device *net_get_device(void);

#endif /* NET_CORE_H */
