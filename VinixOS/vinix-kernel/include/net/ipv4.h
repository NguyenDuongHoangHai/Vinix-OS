/* ============================================================
 * net/ipv4.h
 * ------------------------------------------------------------
 * IPv4 — Internet layer input/output.
 * Chưa sử dụng — chờ Phase 2 (Internet layer).
 * ============================================================ */

#ifndef NET_IPV4_H
#define NET_IPV4_H

#include "types.h"
#include "linux/skbuff.h"

void ip_input(struct sk_buff *skb);
void ip_output(struct sk_buff *skb, uint32_t dst_ip, uint8_t proto);
void ip_set_local(uint32_t ip);

#endif /* NET_IPV4_H */
