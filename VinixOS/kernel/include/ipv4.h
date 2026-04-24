/* ============================================================
 * ipv4.h
 * ------------------------------------------------------------
 * IPv4 Protocol — RFC 791
 * Scope: header build/parse, one's-complement checksum,
 * single static route (1 gateway). Fragmented packets are
 * hard-dropped with a warning — no reassembly.
 * ============================================================ */

#ifndef IPV4_H
#define IPV4_H

#include "types.h"
#include "atomic.h"

/* ============================================================
 * Constants
 * ============================================================ */

#define IPV4_HDR_LEN      20
#define IPV4_TTL_DEFAULT  64

/* Protocol numbers */
#define IPV4_PROTO_ICMP    1
#define IPV4_PROTO_TCP     6
#define IPV4_PROTO_UDP    17

/* Flags in flags_frag field (network byte order, upper bits) */
#define IPV4_FLAG_MF      0x2000   /* More Fragments — LE view after bswap16 */
#define IPV4_FRAG_OFF_MASK 0x1FFF  /* 13-bit fragment offset */

/* ============================================================
 * IPv4 Header — 20 bytes minimum, network byte order
 * ============================================================ */

typedef struct __attribute__((packed)) {
    uint8_t  ver_ihl;
    uint8_t  dscp_ecn;
    uint16_t total_len;
    uint16_t identification;
    uint16_t flags_frag;
    uint8_t  ttl;
    uint8_t  protocol;
    uint16_t hdr_checksum;
    uint32_t src_ip;
    uint32_t dst_ip;
} ipv4_hdr_t;

/* ============================================================
 * Statistics — all fields atomic_t per handoff rule
 * ============================================================ */

typedef struct {
    atomic_t rx_total;
    atomic_t rx_dropped;
    atomic_t rx_checksum_err;
    atomic_t rx_version_err;
    atomic_t rx_hdr_len_err;
    atomic_t rx_frag_drop;
    atomic_t tx_total;
    atomic_t tx_dropped;
} ipv4_stats_t;

/* ============================================================
 * Public Interface
 * ============================================================ */

void     ipv4_init(void);
void     ipv4_rx(const uint8_t *payload, uint16_t len);
int      ipv4_tx(uint32_t dst_ip, uint8_t protocol,
                 const void *data, size_t len);
uint16_t ipv4_checksum(const ipv4_hdr_t *hdr);
const ipv4_stats_t *ipv4_get_stats(void);
void     ipv4_print_ip(uint32_t ip);

#endif /* IPV4_H */
