/* ============================================================
 * net_driver_ops.h
 * ------------------------------------------------------------
 * Hardware-agnostic Ethernet driver abstraction
 * ============================================================ */

#ifndef NET_DRIVER_OPS_H
#define NET_DRIVER_OPS_H

#include "types.h"

/* Three callbacks cover everything ether.c needs from hardware:
 *   tx              — send one raw Ethernet frame
 *   get_mac         — query the hardware MAC address
 *   set_rx_callback — register the ISR-side RX callback
 *
 * Porting to a new NIC = fill this struct, pass to ether_init(). */
typedef struct {
    int  (*tx)(const uint8_t *frame, uint16_t len);
    void (*get_mac)(uint8_t mac[6]);
    void (*set_rx_callback)(void (*cb)(const uint8_t *frame, uint16_t len));
} net_driver_ops_t;

#endif /* NET_DRIVER_OPS_H */
