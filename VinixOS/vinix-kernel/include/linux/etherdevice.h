/* ============================================================
 * linux/etherdevice.h
 * ------------------------------------------------------------
 * Ethernet frame constants, header layout, and inline helpers.
 *
 * IP address convention: HOST byte order internally.
 * Use htonl()/ntohl() only at sk_buff read/write boundary.
 * ============================================================ */

#ifndef LINUX_ETHERDEVICE_H
#define LINUX_ETHERDEVICE_H

#include "types.h"
#include "linux/skbuff.h"

#define ETH_ALEN    6
#define ETH_HLEN    14
#define ETH_P_IP    0x0800
#define ETH_P_ARP   0x0806

struct ethhdr {
    uint8_t  h_dest[ETH_ALEN];
    uint8_t  h_source[ETH_ALEN];
    uint16_t h_proto;
} __attribute__((packed));

static inline struct ethhdr *eth_hdr(const struct sk_buff *skb)
{
    return (struct ethhdr *)skb->data;
}

static inline int is_broadcast_ether_addr(const uint8_t *addr)
{
    return (addr[0] & addr[1] & addr[2] & addr[3] & addr[4] & addr[5]) == 0xff;
}

static inline void ether_addr_copy(uint8_t *dst, const uint8_t *src)
{
    dst[0] = src[0]; dst[1] = src[1]; dst[2] = src[2];
    dst[3] = src[3]; dst[4] = src[4]; dst[5] = src[5];
}

#define htons(x)  __builtin_bswap16((uint16_t)(x))
#define ntohs(x)  __builtin_bswap16((uint16_t)(x))
#define htonl(x)  __builtin_bswap32((uint32_t)(x))
#define ntohl(x)  __builtin_bswap32((uint32_t)(x))

#endif /* LINUX_ETHERDEVICE_H */
