/* ============================================================
 * net/arp.h
 * ------------------------------------------------------------
 * ARP — address resolution for IPv4 over Ethernet.
 * Implemented in Bước 7: net/link/arp.c
 * ============================================================ */

#ifndef NET_ARP_H
#define NET_ARP_H

#include "types.h"
#include "linux/skbuff.h"

void arp_input(struct sk_buff *skb);
void arp_output(uint32_t target_ip);
int  arp_lookup(uint32_t ip, uint8_t mac_out[6]);
void arp_set_local(uint32_t ip, const uint8_t mac[6]);

#endif /* NET_ARP_H */
