/* ============================================================
 * netcore.h
 * ------------------------------------------------------------
 * Network Core — Ethernet + ARP + IPv4 + ICMP public interface
 * ============================================================ */

#ifndef NETCORE_H
#define NETCORE_H

#include "types.h"

/* Build an IPv4 address in network byte order from 4 octets.
 * Example: NETCORE_IP4(192,168,1,100) */
#define NETCORE_IP4(a,b,c,d) \
    ((uint32_t)(a) | ((uint32_t)(b) << 8) | \
     ((uint32_t)(c) << 16) | ((uint32_t)(d) << 24))

/* Must be called after cpsw_init(). my_ip must use NETCORE_IP4(). */
int      netcore_init(uint32_t my_ip);

/* Registered as CPSW RX callback — also callable directly for testing. */
void     netcore_rx(const uint8_t *frame, uint16_t len);

/* Build and send a UDP datagram. Ports in host byte order. */
int      netcore_send_udp(uint32_t dst_ip, uint16_t dst_port,
                          uint16_t src_port,
                          const uint8_t *data, uint16_t len);

/* Register callback invoked for each received UDP datagram. */
void     netcore_set_udp_handler(void (*cb)(uint32_t src_ip,
                                             uint16_t src_port,
                                             uint16_t dst_port,
                                             const uint8_t *data,
                                             uint16_t len));

/* Send ICMP Echo Request and poll for reply. rtt_ticks may be NULL.
 * Returns 0 on reply, -1 on timeout (~2 s). */
int      netcore_ping(uint32_t dst_ip, uint16_t id, uint16_t seq,
                      uint32_t *rtt_ticks);

void     netcore_get_mac(uint8_t mac[6]);
uint32_t netcore_get_ip(void);

#endif /* NETCORE_H */
