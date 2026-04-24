/* ============================================================
 * netcore.h — Network Core (unified entry point for upper layers)
 * Scope: init dispatch, thin wrappers over ARP/IPv4/ICMP,
 * and one test-only helper (netcore_build_arp_request).
 * ============================================================ */

#ifndef NETCORE_H
#define NETCORE_H

#include "types.h"

void netcore_init(void);

/* Test helper — builds raw Ethernet+ARP request frame into buf.
 * Returns frame length on success, -EINVAL if buf too small. */
int  netcore_build_arp_request(uint8_t *buf, size_t len, uint32_t target_ip);

/* Wrappers — forward to arp/ipv4/icmp. Return 0 or negative errno. */
int  netcore_arp_resolve(uint32_t ip, uint8_t mac[6]);
int  netcore_send(uint32_t dst_ip, uint8_t protocol,
                  const void *data, size_t len);
int  netcore_ping(uint32_t dst_ip, uint16_t identifier, uint16_t sequence,
                  const void *data, size_t data_len);

#endif /* NETCORE_H */
