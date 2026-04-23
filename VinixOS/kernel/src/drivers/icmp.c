/* ============================================================
 * icmp.c
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

#include "icmp.h"
#include "ipv4.h"
#include "ether.h"
#include "uart.h"
#include "string.h"

/* ============================================================
 * Global Variables
 * ============================================================ */

static icmp_stats_t s_icmp_stats = {0};

/* ============================================================
 * Helper Functions
 * ============================================================ */

static inline uint16_t icmp_get_checksum(const icmp_hdr_t *hdr)
{
    return (hdr->checksum >> 8) | (hdr->checksum << 8);
}

static inline uint16_t icmp_get_identifier(const icmp_echo_t *echo)
{
    return (echo->identifier >> 8) | (echo->identifier << 8);
}

static inline uint16_t icmp_get_sequence(const icmp_echo_t *echo)
{
    return (echo->sequence >> 8) | (echo->sequence << 8);
}

/* ============================================================
 * Public API Implementation
 * ============================================================ */

void icmp_init(void)
{
    memset(&s_icmp_stats, 0, sizeof(s_icmp_stats));
    uart_printf("[ICMP] initialized\n");
}

void icmp_rx(const uint8_t *payload, uint16_t len, uint32_t src_ip)
{
    s_icmp_stats.rx_total++;

    /* Basic length check */
    if (len < sizeof(icmp_hdr_t)) {
        uart_printf("[ICMP] RX: packet too short (%u bytes), drop\n", len);
        s_icmp_stats.rx_dropped++;
        return;
    }

    const icmp_hdr_t *hdr = (const icmp_hdr_t *)payload;

    uart_printf("[ICMP] RX: type=%u code=%u len=%u src=", 
                hdr->type, hdr->code, len);
    ipv4_print_ip(src_ip);
    uart_printf("\n");

    /* Process based on type */
    switch (hdr->type) {
        case ICMP_TYPE_ECHO_REQUEST:
            s_icmp_stats.rx_echo_req++;
            uart_printf("[ICMP] RX: Echo Request, sending Echo Reply\n");
            
            /* Extract echo header */
            if (len < sizeof(icmp_echo_packet_t)) {
                uart_printf("[ICMP] RX: Echo Request too short, drop\n");
                s_icmp_stats.rx_dropped++;
                return;
            }

            const icmp_echo_packet_t *echo_pkt = (const icmp_echo_packet_t *)payload;
            uint16_t identifier = icmp_get_identifier(&echo_pkt->echo);
            uint16_t sequence = icmp_get_sequence(&echo_pkt->echo);
            
            /* Get data portion */
            const uint8_t *data = payload + sizeof(icmp_echo_packet_t);
            size_t data_len = len - sizeof(icmp_echo_packet_t);

            /* Send Echo Reply */
            icmp_pong(src_ip, identifier, sequence, data, data_len);
            break;

        case ICMP_TYPE_ECHO_REPLY:
            s_icmp_stats.rx_echo_reply++;
            s_icmp_stats.ping_received++;
            uart_printf("[ICMP] RX: Echo Reply received\n");
            
            /* TODO: Handle ping reply for user applications */
            break;

        case ICMP_TYPE_DEST_UNREACH:
        case ICMP_TYPE_SOURCE_QUENCH:
        case ICMP_TYPE_REDIRECT:
        case ICMP_TYPE_TIME_EXCEEDED:
        case ICMP_TYPE_PARAM_PROBLEM:
            uart_printf("[ICMP] RX: %s, not implemented\n", 
                        icmp_type_to_string(hdr->type));
            break;

        default:
            uart_printf("[ICMP] RX: unknown type %u, drop\n", hdr->type);
            s_icmp_stats.rx_dropped++;
            break;
    }
}

int icmp_ping(uint32_t dst_ip, uint16_t identifier, uint16_t sequence,
              const void *data, size_t data_len)
{
    /* Build ICMP Echo Request packet */
    uint8_t packet[sizeof(icmp_echo_packet_t) + data_len];
    icmp_echo_packet_t *echo_pkt = (icmp_echo_packet_t *)packet;

    /* Fill ICMP header */
    echo_pkt->hdr.type = ICMP_TYPE_ECHO_REQUEST;
    echo_pkt->hdr.code = 0;
    echo_pkt->hdr.checksum = 0;  /* Will be calculated later */

    /* Fill echo header */
    echo_pkt->echo.identifier = (identifier >> 8) | (identifier << 8);
    echo_pkt->echo.sequence = (sequence >> 8) | (sequence << 8);

    /* Copy data if provided */
    if (data && data_len > 0) {
        memcpy(packet + sizeof(icmp_echo_packet_t), data, data_len);
    }

    /* Calculate checksum */
    echo_pkt->hdr.checksum = icmp_checksum(packet, sizeof(packet));

    /* Send via IPv4 */
    int ret = ipv4_tx(dst_ip, IPV4_PROTO_ICMP, packet, sizeof(packet));
    if (ret != 0) {
        uart_printf("[ICMP] ping: ipv4_tx failed\n");
        s_icmp_stats.tx_dropped++;
        return -1;
    }

    s_icmp_stats.tx_total++;
    s_icmp_stats.tx_echo_req++;
    s_icmp_stats.ping_sent++;

    uart_printf("[ICMP] ping: dst=");
    ipv4_print_ip(dst_ip);
    uart_printf(" id=%u seq=%u len=%zu\n", identifier, sequence, data_len);

    return 0;
}

int icmp_pong(uint32_t dst_ip, uint16_t identifier, uint16_t sequence,
              const void *data, size_t data_len)
{
    /* Build ICMP Echo Reply packet */
    uint8_t packet[sizeof(icmp_echo_packet_t) + data_len];
    icmp_echo_packet_t *echo_pkt = (icmp_echo_packet_t *)packet;

    /* Fill ICMP header */
    echo_pkt->hdr.type = ICMP_TYPE_ECHO_REPLY;
    echo_pkt->hdr.code = 0;
    echo_pkt->hdr.checksum = 0;  /* Will be calculated later */

    /* Fill echo header */
    echo_pkt->echo.identifier = (identifier >> 8) | (identifier << 8);
    echo_pkt->echo.sequence = (sequence >> 8) | (sequence << 8);

    /* Copy data if provided */
    if (data && data_len > 0) {
        memcpy(packet + sizeof(icmp_echo_packet_t), data, data_len);
    }

    /* Calculate checksum */
    echo_pkt->hdr.checksum = icmp_checksum(packet, sizeof(packet));

    /* Send via IPv4 */
    int ret = ipv4_tx(dst_ip, IPV4_PROTO_ICMP, packet, sizeof(packet));
    if (ret != 0) {
        uart_printf("[ICMP] pong: ipv4_tx failed\n");
        s_icmp_stats.tx_dropped++;
        return -1;
    }

    s_icmp_stats.tx_total++;
    s_icmp_stats.tx_echo_reply++;

    uart_printf("[ICMP] pong: dst=");
    ipv4_print_ip(dst_ip);
    uart_printf(" id=%u seq=%u len=%zu\n", identifier, sequence, data_len);

    return 0;
}

int icmp_dest_unreachable(uint32_t dst_ip, uint8_t code,
                         const void *original_hdr, size_t hdr_len)
{
    /* Build ICMP Destination Unreachable packet */
    uint8_t packet[sizeof(icmp_hdr_t) + sizeof(uint8_t) + sizeof(uint16_t) + hdr_len];
    icmp_hdr_t *hdr = (icmp_hdr_t *)packet;

    /* Fill ICMP header */
    hdr->type = ICMP_TYPE_DEST_UNREACH;
    hdr->code = code;
    hdr->checksum = 0;  /* Will be calculated later */

    /* Unused field (must be 0) */
    packet[sizeof(icmp_hdr_t)] = 0;
    packet[sizeof(icmp_hdr_t) + 1] = 0;

    /* Copy original header */
    if (original_hdr && hdr_len > 0) {
        memcpy(packet + sizeof(icmp_hdr_t) + 2, original_hdr, hdr_len);
    }

    /* Calculate checksum */
    hdr->checksum = icmp_checksum(packet, sizeof(packet));

    /* Send via IPv4 */
    int ret = ipv4_tx(dst_ip, IPV4_PROTO_ICMP, packet, sizeof(packet));
    if (ret != 0) {
        uart_printf("[ICMP] dest_unreachable: ipv4_tx failed\n");
        s_icmp_stats.tx_dropped++;
        return -1;
    }

    s_icmp_stats.tx_total++;

    uart_printf("[ICMP] dest_unreachable: dst=");
    ipv4_print_ip(dst_ip);
    uart_printf(" code=%u\n", code);

    return 0;
}

uint16_t icmp_checksum(const void *data, size_t len)
{
    /* Calculate ICMP checksum */
    uint32_t sum = 0;
    const uint16_t *ptr = (const uint16_t *)data;

    /* Sum all 16-bit words */
    for (size_t i = 0; i < len / 2; i++) {
        sum += ptr[i];
    }

    /* Handle odd byte if present */
    if (len % 2 == 1) {
        sum += ((const uint8_t *)ptr)[len - 1] << 8;
    }

    /* Add carry bits */
    while (sum >> 16) {
        sum = (sum & 0xFFFF) + (sum >> 16);
    }

    /* One's complement */
    return ~sum;
}

const icmp_stats_t *icmp_get_stats(void)
{
    return &s_icmp_stats;
}

const char *icmp_type_to_string(uint8_t type)
{
    switch (type) {
        case ICMP_TYPE_ECHO_REPLY:     return "Echo Reply";
        case ICMP_TYPE_DEST_UNREACH:   return "Destination Unreachable";
        case ICMP_TYPE_SOURCE_QUENCH:  return "Source Quench";
        case ICMP_TYPE_REDIRECT:       return "Redirect";
        case ICMP_TYPE_ECHO_REQUEST:   return "Echo Request";
        case ICMP_TYPE_TIME_EXCEEDED:  return "Time Exceeded";
        case ICMP_TYPE_PARAM_PROBLEM:  return "Parameter Problem";
        case ICMP_TYPE_TIMESTAMP:      return "Timestamp Request";
        case ICMP_TYPE_TIMESTAMP_REPLY: return "Timestamp Reply";
        default:                       return "Unknown";
    }
}
