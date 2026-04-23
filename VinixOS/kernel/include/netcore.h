/* ============================================================
 * netcore.h
 * ------------------------------------------------------------
 * Network Core — Unified Network Interface
 *
 * Trách nhiệm:
 *   - Cung cấp unified interface cho network protocols
 *   - Protocol registration và dispatch
 *   - Wrapper functions cho ARP, IPv4, ICMP
 *
 * Dependencies:
 *   - arp.h: ARP protocol implementation
 *   - ether.h: Ethernet layer
 *   - uart.h: Debug output
 * ============================================================ */

#ifndef NETCORE_H
#define NETCORE_H

#include "types.h"

/* ============================================================
 * Public Interface
 * ============================================================ */

/* Initialize network core */
void netcore_init(void);

/* Build ARP request packet in buffer */
/* Returns packet length on success, -1 on error */
int netcore_build_arp_request(uint8_t *buf, size_t len, uint32_t target_ip);

/* Resolve IP address to MAC address */
/* Returns 0 on success, -1 on timeout */
int netcore_arp_resolve(uint32_t ip, uint8_t mac[6]);

/* Send network packet (IPv4) */
/* Returns 0 on success, -1 on error */
int netcore_send(uint32_t dst_ip, uint8_t protocol, const void *data, size_t len);

#endif /* NETCORE_H */
