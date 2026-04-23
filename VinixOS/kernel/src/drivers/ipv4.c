/* ============================================================
 * ipv4.c
 * ------------------------------------------------------------
 * IPv4 Protocol Implementation
 *
 * Trách nhiệm:
 *   - IPv4 packet processing
 *   - IP header validation
 *   - Fragmentation support (basic)
 *   - Routing and forwarding
 *
 * Dependencies:
 *   - ether.h: Ethernet layer
 *   - arp.h: ARP for MAC resolution
 *   - uart.h: Debug output
 * ============================================================ */

#include "ipv4.h"
#include "ether.h"
#include "arp.h"
#include "icmp.h"
#include "uart.h"
#include "string.h"

/* ============================================================
 * Global Variables
 * ============================================================ */

static ipv4_stats_t s_ipv4_stats = {0};

/* ============================================================
 * Helper Functions
 * ============================================================ */

static inline uint8_t ipv4_get_version(const ipv4_hdr_t *hdr)
{
    return (hdr->ver_ihl >> 4) & 0x0F;
}

static inline uint8_t ipv4_get_ihl(const ipv4_hdr_t *hdr)
{
    return hdr->ver_ihl & 0x0F;
}

static inline uint16_t ipv4_get_total_len(const ipv4_hdr_t *hdr)
{
    return (hdr->total_len >> 8) | (hdr->total_len << 8);
}

static inline uint32_t ipv4_get_src_ip(const ipv4_hdr_t *hdr)
{
    return ((hdr->src_ip & 0xFF) << 24) | 
           (((hdr->src_ip >> 8) & 0xFF) << 16) | 
           (((hdr->src_ip >> 16) & 0xFF) << 8) | 
           (hdr->src_ip >> 24);
}

static inline uint32_t ipv4_get_dst_ip(const ipv4_hdr_t *hdr)
{
    return ((hdr->dst_ip & 0xFF) << 24) | 
           (((hdr->dst_ip >> 8) & 0xFF) << 16) | 
           (((hdr->dst_ip >> 16) & 0xFF) << 8) | 
           (hdr->dst_ip >> 24);
}

/* ============================================================
 * Public API Implementation
 * ============================================================ */

void ipv4_init(void)
{
    memset(&s_ipv4_stats, 0, sizeof(s_ipv4_stats));
    uart_printf("[IPV4] initialized\n");
}

void ipv4_rx(const uint8_t *payload, uint16_t len)
{
    s_ipv4_stats.rx_total++;

    /* Basic length check */
    if (len < sizeof(ipv4_hdr_t)) {
        uart_printf("[IPV4] RX: packet too short (%u bytes), drop\n", len);
        s_ipv4_stats.rx_dropped++;
        return;
    }

    const ipv4_hdr_t *hdr = (const ipv4_hdr_t *)payload;

    /* Version check */
    uint8_t version = ipv4_get_version(hdr);
    if (version != 4) {
        uart_printf("[IPV4] RX: invalid version %u, drop\n", version);
        s_ipv4_stats.rx_dropped++;
        s_ipv4_stats.rx_version_err++;
        return;
    }

    /* Header length check */
    uint8_t ihl = ipv4_get_ihl(hdr);
    if (ihl < 5 || ihl > 15) {
        uart_printf("[IPV4] RX: invalid IHL %u, drop\n", ihl);
        s_ipv4_stats.rx_dropped++;
        s_ipv4_stats.rx_hdr_len_err++;
        return;
    }

    uint16_t hdr_len = ihl * 4;
    if (len < hdr_len) {
        uart_printf("[IPV4] RX: packet shorter than header (%u < %u), drop\n", len, hdr_len);
        s_ipv4_stats.rx_dropped++;
        s_ipv4_stats.rx_hdr_len_err++;
        return;
    }

    /* Total length check */
    uint16_t total_len = ipv4_get_total_len(hdr);
    if (total_len > IPV4_MAX_PACKET || total_len < hdr_len) {
        uart_printf("[IPV4] RX: invalid total length %u, drop\n", total_len);
        s_ipv4_stats.rx_dropped++;
        return;
    }

    if (len < total_len) {
        uart_printf("[IPV4] RX: truncated packet (%u < %u), drop\n", len, total_len);
        s_ipv4_stats.rx_dropped++;
        return;
    }

    /* Get source and destination IPs */
    uint32_t src_ip = ipv4_get_src_ip(hdr);
    uint32_t dst_ip = ipv4_get_dst_ip(hdr);

    uart_printf("[IPV4] RX: src=");
    ipv4_print_ip(src_ip);
    uart_printf(" dst=");
    ipv4_print_ip(dst_ip);
    uart_printf(" proto=%u len=%u ttl=%u\n", 
                hdr->protocol, total_len, hdr->ttl);

    /* Check if packet is for us */
    extern uint32_t s_my_ip;  /* From ARP module */
    if (dst_ip != s_my_ip && dst_ip != 0xFFFFFFFF) {  /* Not broadcast */
        uart_printf("[IPV4] RX: not for us, drop\n");
        s_ipv4_stats.rx_dropped++;
        return;
    }

    /* Process based on protocol */
    switch (hdr->protocol) {
        case IPV4_PROTO_ICMP:
            uart_printf("[IPV4] RX: ICMP packet, forwarding to ICMP handler\n");
            /* Call ICMP handler */
            icmp_rx(payload + hdr_len, total_len - hdr_len, src_ip);
            break;
        case IPV4_PROTO_TCP:
            uart_printf("[IPV4] RX: TCP packet, not implemented\n");
            break;
        case IPV4_PROTO_UDP:
            uart_printf("[IPV4] RX: UDP packet, not implemented\n");
            break;
        default:
            uart_printf("[IPV4] RX: unknown protocol %u, drop\n", hdr->protocol);
            s_ipv4_stats.rx_dropped++;
            break;
    }
}

int ipv4_tx(uint32_t dst_ip, uint8_t protocol, const void *data, size_t len)
{
    /* Resolve destination MAC address */
    uint8_t dst_mac[6];
    int ret = arp_resolve(dst_ip, dst_mac);
    if (ret != 0) {
        uart_printf("[IPV4] TX: failed to resolve MAC for ");
        ipv4_print_ip(dst_ip);
        uart_printf("\n");
        s_ipv4_stats.tx_dropped++;
        return -1;
    }

    /* Build IPv4 header */
    ipv4_hdr_t hdr;
    memset(&hdr, 0, sizeof(hdr));

    /* Version (4) + IHL (5) */
    hdr.ver_ihl = (4 << 4) | 5;
    
    /* DSCP + ECN (0) */
    hdr.dscp_ecn = 0;

    /* Total length (header + data) */
    uint16_t total_len = sizeof(ipv4_hdr_t) + len;
    hdr.total_len = ((total_len & 0xFF) << 8) | ((total_len >> 8) & 0xFF);

    /* Identification (random) */
    hdr.identification = 0x1234;  /* TODO: Use random value */

    /* Flags + Fragment offset (no fragmentation) */
    hdr.flags_frag = 0;

    /* TTL */
    hdr.ttl = IPV4_TTL_DEFAULT;

    /* Protocol */
    hdr.protocol = protocol;

    /* Source IP */
    extern uint32_t s_my_ip;  /* From ARP module */
    hdr.src_ip = ((s_my_ip & 0xFF) << 24) | 
                 (((s_my_ip >> 8) & 0xFF) << 16) | 
                 (((s_my_ip >> 16) & 0xFF) << 8) | 
                 (s_my_ip >> 24);

    /* Destination IP */
    hdr.dst_ip = ((dst_ip & 0xFF) << 24) | 
                 (((dst_ip >> 8) & 0xFF) << 16) | 
                 (((dst_ip >> 16) & 0xFF) << 8) | 
                 (dst_ip >> 24);

    /* Calculate checksum */
    hdr.hdr_checksum = ipv4_checksum(&hdr);

    /* Build complete packet */
    uint8_t packet[IPV4_MAX_PACKET];
    memcpy(packet, &hdr, sizeof(hdr));
    memcpy(packet + sizeof(hdr), data, len);

    /* Send via Ethernet */
    ret = ether_tx(dst_mac, ETHERTYPE_IPV4, packet, total_len);
    if (ret != 0) {
        uart_printf("[IPV4] TX: ether_tx failed: %d\n", ret);
        s_ipv4_stats.tx_dropped++;
        return -1;
    }

    s_ipv4_stats.tx_total++;
    uart_printf("[IPV4] TX: dst=");
    ipv4_print_ip(dst_ip);
    uart_printf(" proto=%u len=%u\n", protocol, total_len);

    return 0;
}

uint16_t ipv4_checksum(const ipv4_hdr_t *hdr)
{
    /* Calculate IPv4 header checksum */
    uint32_t sum = 0;
    const uint16_t *ptr = (const uint16_t *)hdr;
    uint16_t hdr_len = ipv4_get_ihl(hdr) * 4;

    /* Sum all 16-bit words in header */
    for (uint16_t i = 0; i < hdr_len / 2; i++) {
        sum += ptr[i];
    }

    /* Handle odd byte if present */
    if (hdr_len % 2 == 1) {
        sum += ((const uint8_t *)ptr)[hdr_len - 1] << 8;
    }

    /* Add carry bits */
    while (sum >> 16) {
        sum = (sum & 0xFFFF) + (sum >> 16);
    }

    /* One's complement */
    return ~sum;
}

const ipv4_stats_t *ipv4_get_stats(void)
{
    return &s_ipv4_stats;
}

void ipv4_print_ip(uint32_t ip)
{
    uart_printf("%u.%u.%u.%u",
                (ip >> 24) & 0xFF,
                (ip >> 16) & 0xFF,
                (ip >> 8) & 0xFF,
                ip & 0xFF);
}
