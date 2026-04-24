/* ============================================================
 * icmp.h
 * ------------------------------------------------------------
 * ICMP Protocol — RFC 792
 * Scope: Echo Request/Reply (ping) only. Other types logged,
 * not acted upon (dest_unreachable reserved for future use).
 * ============================================================ */

#ifndef ICMP_H
#define ICMP_H

#include "types.h"
#include "atomic.h"

/* ============================================================
 * ICMP Message Types — RFC 792 Table
 * ============================================================ */

#define ICMP_TYPE_ECHO_REPLY       0
#define ICMP_TYPE_DEST_UNREACH     3
#define ICMP_TYPE_SOURCE_QUENCH    4
#define ICMP_TYPE_REDIRECT         5
#define ICMP_TYPE_ECHO_REQUEST     8
#define ICMP_TYPE_TIME_EXCEEDED   11
#define ICMP_TYPE_PARAM_PROBLEM   12
#define ICMP_TYPE_TIMESTAMP       13
#define ICMP_TYPE_TIMESTAMP_REPLY 14

/* Codes for ICMP_TYPE_DEST_UNREACH */
#define ICMP_CODE_NET_UNREACH      0
#define ICMP_CODE_HOST_UNREACH     1
#define ICMP_CODE_PROTO_UNREACH    2
#define ICMP_CODE_PORT_UNREACH     3

/* ============================================================
 * ICMP Packet Structures
 * ============================================================ */

typedef struct __attribute__((packed)) {
    uint8_t  type;
    uint8_t  code;
    uint16_t checksum;
} icmp_hdr_t;

typedef struct __attribute__((packed)) {
    uint16_t identifier;
    uint16_t sequence;
} icmp_echo_t;

typedef struct __attribute__((packed)) {
    icmp_hdr_t  hdr;
    icmp_echo_t echo;
    /* variable-length data follows */
} icmp_echo_packet_t;

/* ============================================================
 * Statistics — all fields atomic_t per handoff rule
 * ============================================================ */

typedef struct {
    atomic_t rx_total;
    atomic_t rx_dropped;
    atomic_t rx_checksum_err;
    atomic_t rx_echo_req;
    atomic_t rx_echo_reply;
    atomic_t tx_total;
    atomic_t tx_dropped;
    atomic_t tx_echo_req;
    atomic_t tx_echo_reply;
    atomic_t ping_sent;
    atomic_t ping_received;
} icmp_stats_t;

/* ============================================================
 * Public Interface
 * ============================================================ */

void     icmp_init(void);
void     icmp_rx(const uint8_t *payload, uint16_t len, uint32_t src_ip);
int      icmp_ping(uint32_t dst_ip, uint16_t identifier, uint16_t sequence,
                   const void *data, size_t data_len);
int      icmp_pong(uint32_t dst_ip, uint16_t identifier, uint16_t sequence,
                   const void *data, size_t data_len);
/* RFC 792: Destination Unreachable — original_hdr should be IP hdr + 8 bytes */
int      icmp_dest_unreachable(uint32_t dst_ip, uint8_t code,
                               const void *original_hdr, size_t hdr_len);
uint16_t icmp_checksum(const void *data, size_t len);
const icmp_stats_t *icmp_get_stats(void);
const char         *icmp_type_to_string(uint8_t type);

#endif /* ICMP_H */
