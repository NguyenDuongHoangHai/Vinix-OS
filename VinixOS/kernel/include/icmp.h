/* ============================================================
 * icmp.h
 * ------------------------------------------------------------
 * ICMP Protocol Implementation
 *
 * Trách nhiệm:
 *   - ICMP packet processing
 *   - Ping functionality (Echo Request/Reply)
 *   - Error reporting
 *   - Network diagnostics
 *
 * Dependencies:
 *   - ipv4.h: IPv4 layer
 *   - ether.h: Ethernet layer
 *   - uart.h: Debug output
 * ============================================================ */

#ifndef ICMP_H
#define ICMP_H

#include "types.h"

/* ============================================================
 * ICMP Message Types
 * ============================================================ */

#define ICMP_TYPE_ECHO_REPLY      0   /* Echo Reply */
#define ICMP_TYPE_DEST_UNREACH    1   /* Destination Unreachable */
#define ICMP_TYPE_SOURCE_QUENCH   2   /* Source Quench */
#define ICMP_TYPE_REDIRECT        3   /* Redirect */
#define ICMP_TYPE_ECHO_REQUEST    8   /* Echo Request */
#define ICMP_TYPE_TIME_EXCEEDED   11  /* Time Exceeded */
#define ICMP_TYPE_PARAM_PROBLEM   12  /* Parameter Problem */
#define ICMP_TYPE_TIMESTAMP      13  /* Timestamp Request */
#define ICMP_TYPE_TIMESTAMP_REPLY 14  /* Timestamp Reply */

/* ICMP Message Codes for Destination Unreachable */
#define ICMP_CODE_NET_UNREACH     0   /* Network Unreachable */
#define ICMP_CODE_HOST_UNREACH    1   /* Host Unreachable */
#define ICMP_CODE_PROTO_UNREACH   2   /* Protocol Unreachable */
#define ICMP_CODE_PORT_UNREACH    3   /* Port Unreachable */

/* ============================================================
 * ICMP Header Structure
 * ============================================================ */

typedef struct __attribute__((packed)) {
    uint8_t  type;           /* Message type */
    uint8_t  code;           /* Message code */
    uint16_t checksum;       /* Checksum */
    /* Rest of header varies by type */
} icmp_hdr_t;

/* Echo Request/Reply header */
typedef struct __attribute__((packed)) {
    uint16_t identifier;     /* Identifier */
    uint16_t sequence;       /* Sequence number */
} icmp_echo_t;

/* Full ICMP Echo packet */
typedef struct __attribute__((packed)) {
    icmp_hdr_t hdr;
    icmp_echo_t echo;
    /* Data follows */
} icmp_echo_packet_t;

/* ============================================================
 * ICMP Statistics
 * ============================================================ */

typedef struct {
    uint32_t rx_total;
    uint32_t rx_dropped;
    uint32_t rx_checksum_err;
    uint32_t rx_echo_req;
    uint32_t rx_echo_reply;
    uint32_t tx_total;
    uint32_t tx_dropped;
    uint32_t tx_echo_req;
    uint32_t tx_echo_reply;
    uint32_t ping_sent;
    uint32_t ping_received;
} icmp_stats_t;

/* ============================================================
 * Public Interface
 * ============================================================ */

/* Initialize ICMP subsystem */
void icmp_init(void);

/* Process incoming ICMP packet */
/* Called from IPv4 layer */
void icmp_rx(const uint8_t *payload, uint16_t len, uint32_t src_ip);

/* Send ICMP Echo Request (ping) */
/* Returns 0 on success, -1 on error */
int icmp_ping(uint32_t dst_ip, uint16_t identifier, uint16_t sequence, 
              const void *data, size_t data_len);

/* Send ICMP Echo Reply (pong) */
/* Returns 0 on success, -1 on error */
int icmp_pong(uint32_t dst_ip, uint16_t identifier, uint16_t sequence,
              const void *data, size_t data_len);

/* Send ICMP Destination Unreachable */
/* Returns 0 on success, -1 on error */
int icmp_dest_unreachable(uint32_t dst_ip, uint8_t code, 
                         const void *original_hdr, size_t hdr_len);

/* Calculate ICMP checksum */
uint16_t icmp_checksum(const void *data, size_t len);

/* Get ICMP statistics */
const icmp_stats_t *icmp_get_stats(void);

/* Format ICMP type for printing */
const char *icmp_type_to_string(uint8_t type);

#endif /* ICMP_H */
