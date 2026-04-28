/* ============================================================
 * net/icmp.h
 * ------------------------------------------------------------
 * ICMP — echo reply (ping) handler.
 * Chưa sử dụng — chờ Phase 2 (Internet layer).
 * ============================================================ */

#ifndef NET_ICMP_H
#define NET_ICMP_H

#include "linux/skbuff.h"

void icmp_input(struct sk_buff *skb);

#endif /* NET_ICMP_H */
