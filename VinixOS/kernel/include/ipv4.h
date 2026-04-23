/* ============================================================
 * ipv4.h
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

#ifndef IPV4_H
#define IPV4_H

#include "types.h"

/* ============================================================
 * IPv4 Header Constants
 * ============================================================ */

#define IPV4_HDR_LEN      20      /* Minimum IPv4 header length */
#define IPV4_MAX_HDR_LEN  60      /* Maximum IPv4 header length */
#define IPV4_MAX_PACKET   65535   /* Maximum IPv4 packet size */
#define IPV4_TTL_DEFAULT  64      /* Default Time To Live */

/* IPv4 Protocol numbers */
#define IPV4_PROTO_ICMP   1       /* Internet Control Message Protocol */
#define IPV4_PROTO_TCP    6       /* Transmission Control Protocol */
#define IPV4_PROTO_UDP    17      /* User Datagram Protocol */

/* IPv4 Header structure (20 bytes minimum) */
typedef struct __attribute__((packed)) {
    uint8_t  ver_ihl;        /* Version (4) + IHL (Header Length) */
    uint8_t  dscp_ecn;       /* DSCP + ECN */
    uint16_t total_len;      /* Total length */
    uint16_t identification; /* Identification */
    uint16_t flags_frag;     /* Flags + Fragment offset */
    uint8_t  ttl;            /* Time To Live */
    uint8_t  protocol;       /* Protocol */
    uint16_t hdr_checksum;   /* Header checksum */
    uint32_t src_ip;         /* Source IP address */
    uint32_t dst_ip;         /* Destination IP address */
    /* Options may follow if IHL > 5 */
} ipv4_hdr_t;

/* ============================================================
 * IPv4 Statistics
 * ============================================================ */

typedef struct {
    uint32_t rx_total;
    uint32_t rx_dropped;
    uint32_t rx_checksum_err;
    uint32_t rx_version_err;
    uint32_t rx_hdr_len_err;
    uint32_t tx_total;
    uint32_t tx_dropped;
} ipv4_stats_t;

/* ============================================================
 * Public Interface
 * ============================================================ */

/* Initialize IPv4 subsystem */
void ipv4_init(void);

/* Process incoming IPv4 packet */
/* Called from Ethernet layer */
void ipv4_rx(const uint8_t *payload, uint16_t len);

/* Send IPv4 packet */
/* Returns 0 on success, -1 on error */
int ipv4_tx(uint32_t dst_ip, uint8_t protocol, const void *data, size_t len);

/* Calculate IPv4 header checksum */
uint16_t ipv4_checksum(const ipv4_hdr_t *hdr);

/* Get IPv4 statistics */
const ipv4_stats_t *ipv4_get_stats(void);

/* Format IPv4 address for printing */
void ipv4_print_ip(uint32_t ip);

#endif /* IPV4_H */
