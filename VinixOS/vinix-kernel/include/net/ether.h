/* ============================================================
 * net/ether.h
 * ------------------------------------------------------------
 * Ethernet frame dispatcher — Link layer input entry point.
 * Implemented in Bước 6: net/link/ether.c
 * ============================================================ */

#ifndef NET_ETHER_H
#define NET_ETHER_H

#include "linux/skbuff.h"

void eth_input(struct sk_buff *skb);

#endif /* NET_ETHER_H */
