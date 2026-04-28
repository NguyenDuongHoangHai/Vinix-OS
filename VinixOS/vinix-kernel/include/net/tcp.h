/* ============================================================
 * net/tcp.h
 * ------------------------------------------------------------
 * TCP — minimal passive-open state machine for HTTP server.
 * Chưa sử dụng — chờ Phase 3 (Transport layer).
 * ============================================================ */

#ifndef NET_TCP_H
#define NET_TCP_H

#include "linux/skbuff.h"

void tcp_input(struct sk_buff *skb);

#endif /* NET_TCP_H */
