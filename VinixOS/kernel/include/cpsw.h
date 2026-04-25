/* ============================================================
 * cpsw.h
 * ------------------------------------------------------------
 * CPSW Ethernet MAC Driver — Public Interface
 * ============================================================ */

#ifndef CPSW_H
#define CPSW_H

#include "types.h"
#include "net_driver_ops.h"

#define CPSW_MAC_LEN       6
#define CPSW_FRAME_MAXLEN  1024

/* Must be called after mdio_init(). Returns 0 on success, -1 on timeout. */
int  cpsw_init(void);

void cpsw_set_mac(const uint8_t mac[CPSW_MAC_LEN]);
void cpsw_get_mac(uint8_t mac[CPSW_MAC_LEN]);

/* Returns 0 on success, -1 if TX busy or timeout. */
int  cpsw_tx(const uint8_t *buf, uint16_t len);

/* Register callback invoked by cpsw_rx_poll() for each received frame. */
void cpsw_set_rx_callback(void (*cb)(const uint8_t *buf, uint16_t len));

/* Call from idle task. Invokes rx callback if a frame is ready. */
void cpsw_rx_poll(void);

/* Call after irq_init() to enable interrupt-driven RX. */
void cpsw_rx_irq_enable(void);

/* Ops instance — pass to ether_init() to wire CPSW as the network driver. */
extern const net_driver_ops_t cpsw_net_ops;

#endif /* CPSW_H */
