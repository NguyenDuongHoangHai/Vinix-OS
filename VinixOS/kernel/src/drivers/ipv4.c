/* ============================================================
 * ipv4.c — IPv4 Protocol (RFC 791)
 * ============================================================ */

#include "ipv4.h"
#include "ether.h"
#include "arp.h"
#include "icmp.h"
#include "uart.h"
#include "string.h"
#include "atomic.h"
#include "errno.h"

/* ============================================================
 * Statistics
 * ============================================================ */

static ipv4_stats_t s_ipv4_stats = {
    .rx_total       = ATOMIC_INIT(0),
    .rx_dropped     = ATOMIC_INIT(0),
    .rx_checksum_err= ATOMIC_INIT(0),
    .rx_version_err = ATOMIC_INIT(0),
    .rx_hdr_len_err = ATOMIC_INIT(0),
    .rx_frag_drop   = ATOMIC_INIT(0),
    .tx_total       = ATOMIC_INIT(0),
    .tx_dropped     = ATOMIC_INIT(0),
};

/* ============================================================
 * Field accessors — handle network byte order (big-endian)
 * on little-endian ARM
 * ============================================================ */

static inline uint8_t ipv4_get_version(const ipv4_hdr_t *hdr)
{
    return (hdr->ver_ihl >> 4) & 0x0F;
}

static inline uint8_t ipv4_get_ihl(const ipv4_hdr_t *hdr)
{
    return hdr->ver_ihl & 0x0F;
}

static inline uint16_t bswap16(uint16_t v)
{
    return (uint16_t)((v >> 8) | (v << 8));
}

static inline uint32_t bswap32(uint32_t v)
{
    return ((v & 0x000000FFu) << 24) |
           ((v & 0x0000FF00u) <<  8) |
           ((v & 0x00FF0000u) >>  8) |
           ((v & 0xFF000000u) >> 24);
}

/* ============================================================
 * Public API
 * ============================================================ */

void ipv4_init(void)
{
    uart_printf("[IP] initialized\n");
}

void ipv4_rx(const uint8_t *payload, uint16_t len)
{
    atomic_inc(&s_ipv4_stats.rx_total);

    if (len < sizeof(ipv4_hdr_t)) {
        uart_printf("[IP] RX: too short (%u), drop\n", len);
        atomic_inc(&s_ipv4_stats.rx_dropped);
        return;
    }

    const ipv4_hdr_t *hdr = (const ipv4_hdr_t *)payload;

    /* Version must be 4 */
    uint8_t version = ipv4_get_version(hdr);
    if (version != 4) {
        uart_printf("[IP] RX: bad version %u, drop\n", version);
        atomic_inc(&s_ipv4_stats.rx_dropped);
        atomic_inc(&s_ipv4_stats.rx_version_err);
        return;
    }

    /* IHL sanity */
    uint8_t ihl = ipv4_get_ihl(hdr);
    if (ihl < 5) {
        uart_printf("[IP] RX: bad IHL %u, drop\n", ihl);
        atomic_inc(&s_ipv4_stats.rx_dropped);
        atomic_inc(&s_ipv4_stats.rx_hdr_len_err);
        return;
    }

    uint16_t hdr_len = (uint16_t)(ihl * 4);
    if (len < hdr_len) {
        uart_printf("[IP] RX: truncated header, drop\n");
        atomic_inc(&s_ipv4_stats.rx_dropped);
        atomic_inc(&s_ipv4_stats.rx_hdr_len_err);
        return;
    }

    /* Header checksum verification */
    if (ipv4_checksum(hdr) != 0) {
        uart_printf("[IP] RX: bad checksum, drop\n");
        atomic_inc(&s_ipv4_stats.rx_dropped);
        atomic_inc(&s_ipv4_stats.rx_checksum_err);
        return;
    }

    uint16_t total_len = bswap16(hdr->total_len);
    if (total_len < hdr_len || len < total_len) {
        uart_printf("[IP] RX: bad total_len %u, drop\n", total_len);
        atomic_inc(&s_ipv4_stats.rx_dropped);
        return;
    }

    /* Fragmented packets — hard drop, no reassembly */
    uint16_t flags_frag = bswap16(hdr->flags_frag);
    uint8_t  mf         = (flags_frag >> 13) & 1;
    uint16_t frag_off   = flags_frag & 0x1FFF;
    if (mf || frag_off) {
        uart_printf("[IP] RX: fragmented packet (MF=%u off=%u), drop\n",
                    mf, frag_off);
        atomic_inc(&s_ipv4_stats.rx_dropped);
        atomic_inc(&s_ipv4_stats.rx_frag_drop);
        return;
    }

    /* s_my_ip uses network-byte-order integer notation (MSB = first octet).
     * hdr->dst_ip is read as LE uint32 from the network packet — need bswap32 to compare. */
    uint32_t dst_raw = hdr->dst_ip;
    if (bswap32(dst_raw) != s_my_ip && dst_raw != 0xFFFFFFFFu) {
        uart_printf("[IP] RX: not for us, drop\n");
        atomic_inc(&s_ipv4_stats.rx_dropped);
        return;
    }

    /* Convert LE-read src_ip back to network-byte-order integer (arp/icmp convention) */
    uint32_t src_raw = bswap32(hdr->src_ip);

    uart_printf("[IP] RX: proto=%u len=%u ttl=%u\n",
                hdr->protocol, total_len, hdr->ttl);

    const uint8_t *upper = payload + hdr_len;
    uint16_t       upper_len = (uint16_t)(total_len - hdr_len);

    switch (hdr->protocol) {
        case IPV4_PROTO_ICMP:
            icmp_rx(upper, upper_len, src_raw);
            break;
        case IPV4_PROTO_UDP:
            uart_printf("[IP] RX: UDP, not yet implemented\n");
            break;
        case IPV4_PROTO_TCP:
            uart_printf("[IP] RX: TCP not supported, drop\n");
            atomic_inc(&s_ipv4_stats.rx_dropped);
            break;
        default:
            uart_printf("[IP] RX: unknown proto %u, drop\n", hdr->protocol);
            atomic_inc(&s_ipv4_stats.rx_dropped);
            break;
    }
}

int ipv4_tx(uint32_t dst_ip, uint8_t protocol, const void *data, size_t len)
{
    /* Protocol 0 and 255 are IANA Reserved — never valid to transmit */
    if (protocol == 0 || protocol == 255)
        return -EINVAL;

    /* ETH_MAX_PAYLOAD = 1010; IPv4 header = 20; max upper payload = 990 */
    uint16_t total_len = (uint16_t)(sizeof(ipv4_hdr_t) + len);
    if (total_len > ETH_MAX_PAYLOAD) {
        uart_printf("[IP] TX: packet too large (%u > %u), drop\n",
                    total_len, ETH_MAX_PAYLOAD);
        atomic_inc(&s_ipv4_stats.tx_dropped);
        return -EINVAL;
    }

    /* Resolve destination MAC */
    uint8_t dst_mac[6];
    if (arp_resolve(dst_ip, dst_mac) != 0) {
        uart_printf("[IP] TX: ARP resolve failed\n");
        atomic_inc(&s_ipv4_stats.tx_dropped);
        return -EIO;
    }

    /* Build IPv4 header — all fields in network byte order */
    ipv4_hdr_t hdr;
    memset(&hdr, 0, sizeof(hdr));

    hdr.ver_ihl       = (4 << 4) | 5;
    hdr.dscp_ecn      = 0;
    hdr.total_len     = bswap16(total_len);
    hdr.identification= bswap16(0x1234);   /* TODO: increment per packet */
    hdr.flags_frag    = bswap16(0x4000);   /* DF bit set, no fragmentation */
    hdr.ttl           = IPV4_TTL_DEFAULT;
    hdr.protocol      = protocol;
    hdr.hdr_checksum  = 0;
    hdr.src_ip        = bswap32(s_my_ip);   /* network-byte-order integer → LE wire bytes */
    hdr.dst_ip        = bswap32(dst_ip);

    hdr.hdr_checksum  = ipv4_checksum(&hdr);

    /* Assemble into stack buffer — safe: total_len ≤ ETH_MAX_PAYLOAD (1010) */
    uint8_t packet[ETH_MAX_PAYLOAD];
    memcpy(packet,               &hdr, sizeof(hdr));
    memcpy(packet + sizeof(hdr), data, len);

    int ret = ether_tx(dst_mac, ETHERTYPE_IPV4, packet, total_len);
    if (ret != 0) {
        uart_printf("[IP] TX: ether_tx failed %d\n", ret);
        atomic_inc(&s_ipv4_stats.tx_dropped);
        return -EIO;
    }

    atomic_inc(&s_ipv4_stats.tx_total);
    uart_printf("[IP] TX: proto=%u len=%u\n", protocol, total_len);
    return 0;
}

uint16_t ipv4_checksum(const ipv4_hdr_t *hdr)
{
    uint32_t       sum     = 0;
    const uint8_t *ptr     = (const uint8_t *)hdr;
    uint16_t       hdr_len = (uint16_t)(ipv4_get_ihl(hdr) * 4);

    for (uint16_t i = 0; i + 1 < hdr_len; i += 2)
        sum += (uint16_t)((uint16_t)ptr[i] | ((uint16_t)ptr[i + 1] << 8));

    if (hdr_len & 1)
        sum += ptr[hdr_len - 1];

    while (sum >> 16)
        sum = (sum & 0xFFFF) + (sum >> 16);

    return (uint16_t)(~sum);
}

const ipv4_stats_t *ipv4_get_stats(void)
{
    return &s_ipv4_stats;
}

void ipv4_print_ip(uint32_t ip)
{
    uart_printf("%u.%u.%u.%u",
                (ip >> 24) & 0xFF, (ip >> 16) & 0xFF,
                (ip >>  8) & 0xFF,  ip         & 0xFF);
}
