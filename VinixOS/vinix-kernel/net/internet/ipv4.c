/* ============================================================
 * net/internet/ipv4.c
 * ------------------------------------------------------------
 * IPv4 Internet layer — chưa implement, chờ Phase 2.
 * Stub để linker resolve symbol ip_input từ ether.c.
 * ============================================================ */

#include "linux/skbuff.h"
#include "net/ipv4.h"

void ip_input(struct sk_buff *skb)
{
    /* Chưa sử dụng — chờ Phase 2 (Internet layer). */
    kfree_skb(skb);
}

void ip_output(struct sk_buff *skb, uint32_t dst_ip, uint8_t proto)
{
    /* Chưa sử dụng — chờ Phase 2. */
    (void)dst_ip;
    (void)proto;
    kfree_skb(skb);
}

void ip_set_local(uint32_t ip)
{
    /* Chưa sử dụng — chờ Phase 2. */
    (void)ip;
}
