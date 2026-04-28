/* ============================================================
 * net/link/ether.c
 * ------------------------------------------------------------
 * Ethernet Link layer — dispatch frame by EtherType.
 * ============================================================ */

#include "linux/netdevice.h"
#include "linux/etherdevice.h"
#include "linux/skbuff.h"
#include "net/ether.h"
#include "net/arp.h"
#include "net/ipv4.h"

void eth_input(struct sk_buff *skb)
{
    if (!skb || skb->len < ETH_HLEN) {
        kfree_skb(skb);
        return;
    }

    struct ethhdr *eth = eth_hdr(skb);
    uint16_t type = ntohs(eth->h_proto);

    skb->data += ETH_HLEN;
    skb->len  -= ETH_HLEN;

    if (type == ETH_P_ARP)
        arp_input(skb);
    else if (type == ETH_P_IP)
        ip_input(skb);          /* Chưa implement — chờ Phase 2 */
    else
        kfree_skb(skb);
}
