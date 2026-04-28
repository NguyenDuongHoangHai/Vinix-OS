/* ============================================================
 * net/link/arp.c
 * ------------------------------------------------------------
 * ARP — address resolution, 8-entry static table.
 * ============================================================ */

#include "linux/netdevice.h"
#include "linux/etherdevice.h"
#include "linux/skbuff.h"
#include "net/arp.h"
#include "net/core.h"
#include "slab.h"
#include "uart.h"

/* ARP packet format (RFC 826) */
struct arp_pkt {
    uint16_t htype;       /* hardware type: 1 = Ethernet */
    uint16_t ptype;       /* protocol type: 0x0800 = IPv4 */
    uint8_t  hlen;        /* hardware addr len: 6 */
    uint8_t  plen;        /* protocol addr len: 4 */
    uint16_t oper;        /* 1=request, 2=reply */
    uint8_t  sha[6];      /* sender hardware addr */
    uint32_t spa;         /* sender protocol addr (host byte order) */
    uint8_t  tha[6];      /* target hardware addr */
    uint32_t tpa;         /* target protocol addr (host byte order) */
} __attribute__((packed));

#define ARP_OPER_REQUEST    1
#define ARP_OPER_REPLY      2

#define ARP_TABLE_SIZE      8

struct arp_entry {
    uint32_t ip;          /* host byte order, 0 = empty */
    uint8_t  mac[6];
};

static struct arp_entry s_table[ARP_TABLE_SIZE];
static uint32_t         s_my_ip;
static uint8_t          s_my_mac[6];
static int              s_init;

void arp_set_local(uint32_t ip, const uint8_t mac[6])
{
    if (s_init)
        return;
    s_my_ip = ip;
    ether_addr_copy(s_my_mac, mac);
    s_init = 1;
}

static void table_update(uint32_t ip, const uint8_t mac[6])
{
    for (int i = 0; i < ARP_TABLE_SIZE; i++) {
        if (s_table[i].ip == ip) {
            ether_addr_copy(s_table[i].mac, mac);
            return;
        }
    }
    /* Find empty slot */
    for (int i = 0; i < ARP_TABLE_SIZE; i++) {
        if (s_table[i].ip == 0) {
            s_table[i].ip = ip;
            ether_addr_copy(s_table[i].mac, mac);
            return;
        }
    }
    /* Overwrite slot 0 (LRU replacement would need timestamps) */
    s_table[0].ip = ip;
    ether_addr_copy(s_table[0].mac, mac);
}

int arp_lookup(uint32_t ip, uint8_t mac_out[6])
{
    for (int i = 0; i < ARP_TABLE_SIZE; i++) {
        if (s_table[i].ip == ip) {
            ether_addr_copy(mac_out, s_table[i].mac);
            return 0;
        }
    }
    return -1;
}

static void send_arp_reply(const struct arp_pkt *req)
{
    struct net_device *ndev = net_get_device();
    if (!ndev || !ndev->netdev_ops || !ndev->netdev_ops->ndo_start_xmit)
        return;

    uint32_t skb_size = ETH_HLEN + sizeof(struct arp_pkt);
    struct sk_buff *skb = alloc_skb(skb_size);
    if (!skb)
        return;

    /* Ethernet header */
    struct ethhdr *eth = (struct ethhdr *)skb_put(skb, ETH_HLEN);
    ether_addr_copy(eth->h_dest,   req->sha);
    ether_addr_copy(eth->h_source, s_my_mac);
    eth->h_proto = htons(ETH_P_ARP);

    /* ARP reply body */
    struct arp_pkt *rep = (struct arp_pkt *)skb_put(skb, sizeof(struct arp_pkt));
    rep->htype = htons(1);
    rep->ptype = htons(ETH_P_IP);
    rep->hlen  = 6;
    rep->plen  = 4;
    rep->oper  = htons(ARP_OPER_REPLY);
    ether_addr_copy(rep->sha, s_my_mac);
    rep->spa   = htonl(s_my_ip);
    ether_addr_copy(rep->tha, req->sha);
    rep->tpa   = req->spa;

    skb->dev = ndev;
    ndev->netdev_ops->ndo_start_xmit(skb, ndev);
}

void arp_input(struct sk_buff *skb)
{
    if (skb->len < (int)sizeof(struct arp_pkt)) {
        kfree_skb(skb);
        return;
    }

    struct arp_pkt *pkt = (struct arp_pkt *)skb->data;

    /* Convert wire byte order → host byte order at boundary */
    uint32_t sender_ip = ntohl(pkt->spa);
    uint32_t target_ip = ntohl(pkt->tpa);
    uint16_t oper      = ntohs(pkt->oper);

    /* Windows DAD probe has sender_ip=0 — ignore */
    if (sender_ip == 0) {
        kfree_skb(skb);
        return;
    }

    /* Learn sender into table */
    table_update(sender_ip, pkt->sha);

    if (oper == ARP_OPER_REQUEST && target_ip == s_my_ip) {
        send_arp_reply(pkt);
    }

    kfree_skb(skb);
}

void arp_output(uint32_t target_ip)
{
    struct net_device *ndev = net_get_device();
    if (!ndev || !ndev->netdev_ops || !ndev->netdev_ops->ndo_start_xmit)
        return;

    static const uint8_t bcast[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    static const uint8_t zero[6]  = {0};

    uint32_t skb_size = ETH_HLEN + sizeof(struct arp_pkt);
    struct sk_buff *skb = alloc_skb(skb_size);
    if (!skb)
        return;

    struct ethhdr *eth = (struct ethhdr *)skb_put(skb, ETH_HLEN);
    ether_addr_copy(eth->h_dest,   bcast);
    ether_addr_copy(eth->h_source, s_my_mac);
    eth->h_proto = htons(ETH_P_ARP);

    struct arp_pkt *req = (struct arp_pkt *)skb_put(skb, sizeof(struct arp_pkt));
    req->htype = htons(1);
    req->ptype = htons(ETH_P_IP);
    req->hlen  = 6;
    req->plen  = 4;
    req->oper  = htons(ARP_OPER_REQUEST);
    ether_addr_copy(req->sha, s_my_mac);
    req->spa   = htonl(s_my_ip);
    ether_addr_copy(req->tha, zero);
    req->tpa   = htonl(target_ip);

    skb->dev = ndev;
    ndev->netdev_ops->ndo_start_xmit(skb, ndev);
}
