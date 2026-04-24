/* ============================================================
 * icmp.c — ICMP Protocol (RFC 792)
 * Scope: Echo Request/Reply (ping). Other types are logged only.
 * ============================================================ */

#include "icmp.h"
#include "ipv4.h"
#include "ether.h"
#include "uart.h"
#include "string.h"
#include "atomic.h"
#include "slab.h"
#include "errno.h"

/* ============================================================
 * Statistics
 * ============================================================ */

static icmp_stats_t s_icmp_stats = {
    .rx_total        = ATOMIC_INIT(0),
    .rx_dropped      = ATOMIC_INIT(0),
    .rx_checksum_err = ATOMIC_INIT(0),
    .rx_echo_req     = ATOMIC_INIT(0),
    .rx_echo_reply   = ATOMIC_INIT(0),
    .tx_total        = ATOMIC_INIT(0),
    .tx_dropped      = ATOMIC_INIT(0),
    .tx_echo_req     = ATOMIC_INIT(0),
    .tx_echo_reply   = ATOMIC_INIT(0),
    .ping_sent       = ATOMIC_INIT(0),
    .ping_received   = ATOMIC_INIT(0),
};

/* ============================================================
 * Public API
 * ============================================================ */

void icmp_init(void)
{
    uart_printf("[ICMP] initialized\n");
}

void icmp_rx(const uint8_t *payload, uint16_t len, uint32_t src_ip)
{
    atomic_inc(&s_icmp_stats.rx_total);

    if (len < sizeof(icmp_hdr_t)) {
        uart_printf("[ICMP] RX: too short (%u), drop\n", len);
        atomic_inc(&s_icmp_stats.rx_dropped);
        return;
    }

    /* Verify checksum before any processing */
    if (icmp_checksum(payload, len) != 0) {
        uart_printf("[ICMP] RX: bad checksum, drop\n");
        atomic_inc(&s_icmp_stats.rx_dropped);
        atomic_inc(&s_icmp_stats.rx_checksum_err);
        return;
    }

    const icmp_hdr_t *hdr = (const icmp_hdr_t *)payload;

    uart_printf("[ICMP] RX: type=%u code=%u len=%u\n",
                hdr->type, hdr->code, len);

    switch (hdr->type) {
        case ICMP_TYPE_ECHO_REQUEST: {
            atomic_inc(&s_icmp_stats.rx_echo_req);
            if (len < sizeof(icmp_echo_packet_t)) {
                uart_printf("[ICMP] RX: echo request too short, drop\n");
                atomic_inc(&s_icmp_stats.rx_dropped);
                return;
            }
            const icmp_echo_packet_t *ep = (const icmp_echo_packet_t *)payload;
            const uint8_t *data     = payload + sizeof(icmp_echo_packet_t);
            size_t         data_len = len - sizeof(icmp_echo_packet_t);
            /* identifier and sequence are already in network byte order;
             * pass them through unchanged — icmp_pong will echo them back as-is */
            icmp_pong(src_ip, ep->echo.identifier, ep->echo.sequence,
                      data, data_len);
            break;
        }

        case ICMP_TYPE_ECHO_REPLY:
            atomic_inc(&s_icmp_stats.rx_echo_reply);
            atomic_inc(&s_icmp_stats.ping_received);
            uart_printf("[ICMP] RX: echo reply\n");
            break;

        case ICMP_TYPE_DEST_UNREACH:
        case ICMP_TYPE_SOURCE_QUENCH:
        case ICMP_TYPE_REDIRECT:
        case ICMP_TYPE_TIME_EXCEEDED:
        case ICMP_TYPE_PARAM_PROBLEM:
            uart_printf("[ICMP] RX: %s, ignored\n",
                        icmp_type_to_string(hdr->type));
            break;

        default:
            uart_printf("[ICMP] RX: unknown type %u, drop\n", hdr->type);
            atomic_inc(&s_icmp_stats.rx_dropped);
            break;
    }
}

int icmp_ping(uint32_t dst_ip, uint16_t identifier, uint16_t sequence,
              const void *data, size_t data_len)
{
    size_t pkt_len = sizeof(icmp_echo_packet_t) + data_len;
    if (pkt_len > (size_t)(ETH_MAX_PAYLOAD - sizeof(ipv4_hdr_t))) {
        uart_printf("[ICMP] ping: payload too large\n");
        return -EINVAL;
    }

    uint8_t *packet = (uint8_t *)kmalloc((uint32_t)pkt_len, GFP_KERNEL);
    if (!packet) {
        uart_printf("[ICMP] ping: kmalloc failed\n");
        atomic_inc(&s_icmp_stats.tx_dropped);
        return -ENOMEM;
    }

    icmp_echo_packet_t *ep = (icmp_echo_packet_t *)packet;
    ep->hdr.type      = ICMP_TYPE_ECHO_REQUEST;
    ep->hdr.code      = 0;
    ep->hdr.checksum  = 0;
    ep->echo.identifier = identifier;
    ep->echo.sequence   = sequence;

    if (data && data_len > 0)
        memcpy(packet + sizeof(icmp_echo_packet_t), data, data_len);

    ep->hdr.checksum = icmp_checksum(packet, pkt_len);

    int ret = ipv4_tx(dst_ip, IPV4_PROTO_ICMP, packet, pkt_len);
    kfree(packet);

    if (ret != 0) {
        uart_printf("[ICMP] ping: ipv4_tx failed %d\n", ret);
        atomic_inc(&s_icmp_stats.tx_dropped);
        return -EIO;
    }

    atomic_inc(&s_icmp_stats.tx_total);
    atomic_inc(&s_icmp_stats.tx_echo_req);
    atomic_inc(&s_icmp_stats.ping_sent);
    uart_printf("[ICMP] ping: seq=%u id=%u len=%u\n",
                sequence, identifier, (uint32_t)data_len);
    return 0;
}

int icmp_pong(uint32_t dst_ip, uint16_t identifier, uint16_t sequence,
              const void *data, size_t data_len)
{
    size_t pkt_len = sizeof(icmp_echo_packet_t) + data_len;
    if (pkt_len > (size_t)(ETH_MAX_PAYLOAD - sizeof(ipv4_hdr_t))) {
        uart_printf("[ICMP] pong: payload too large\n");
        return -EINVAL;
    }

    uint8_t *packet = (uint8_t *)kmalloc((uint32_t)pkt_len, GFP_KERNEL);
    if (!packet) {
        uart_printf("[ICMP] pong: kmalloc failed\n");
        atomic_inc(&s_icmp_stats.tx_dropped);
        return -ENOMEM;
    }

    icmp_echo_packet_t *ep = (icmp_echo_packet_t *)packet;
    ep->hdr.type      = ICMP_TYPE_ECHO_REPLY;
    ep->hdr.code      = 0;
    ep->hdr.checksum  = 0;
    ep->echo.identifier = identifier;
    ep->echo.sequence   = sequence;

    if (data && data_len > 0)
        memcpy(packet + sizeof(icmp_echo_packet_t), data, data_len);

    ep->hdr.checksum = icmp_checksum(packet, pkt_len);

    int ret = ipv4_tx(dst_ip, IPV4_PROTO_ICMP, packet, pkt_len);
    kfree(packet);

    if (ret != 0) {
        uart_printf("[ICMP] pong: ipv4_tx failed %d\n", ret);
        atomic_inc(&s_icmp_stats.tx_dropped);
        return -EIO;
    }

    atomic_inc(&s_icmp_stats.tx_total);
    atomic_inc(&s_icmp_stats.tx_echo_reply);
    uart_printf("[ICMP] pong: seq=%u id=%u len=%u\n",
                sequence, identifier, (uint32_t)data_len);
    return 0;
}

int icmp_dest_unreachable(uint32_t dst_ip, uint8_t code,
                          const void *original_hdr, size_t hdr_len)
{
    if (!original_hdr || hdr_len == 0) {
        uart_printf("[ICMP] dest_unreach: invalid args\n");
        return -EINVAL;
    }

    /* RFC 792: icmp_hdr (4B) + unused (4B) + original IP hdr + 8B */
    size_t pkt_len = sizeof(icmp_hdr_t) + 4 + hdr_len;
    if (pkt_len > (size_t)(ETH_MAX_PAYLOAD - sizeof(ipv4_hdr_t))) {
        uart_printf("[ICMP] dest_unreach: payload too large\n");
        return -EINVAL;
    }

    uint8_t *packet = (uint8_t *)kmalloc((uint32_t)pkt_len, GFP_KERNEL);
    if (!packet) {
        uart_printf("[ICMP] dest_unreach: kmalloc failed\n");
        atomic_inc(&s_icmp_stats.tx_dropped);
        return -ENOMEM;
    }

    icmp_hdr_t *hdr = (icmp_hdr_t *)packet;
    hdr->type     = ICMP_TYPE_DEST_UNREACH;
    hdr->code     = code;
    hdr->checksum = 0;

    memset(packet + sizeof(icmp_hdr_t), 0, 4);
    memcpy(packet + sizeof(icmp_hdr_t) + 4, original_hdr, hdr_len);

    hdr->checksum = icmp_checksum(packet, pkt_len);

    int ret = ipv4_tx(dst_ip, IPV4_PROTO_ICMP, packet, pkt_len);
    kfree(packet);

    if (ret != 0) {
        uart_printf("[ICMP] dest_unreach: ipv4_tx failed %d\n", ret);
        atomic_inc(&s_icmp_stats.tx_dropped);
        return -EIO;
    }

    atomic_inc(&s_icmp_stats.tx_total);
    uart_printf("[ICMP] dest_unreach: code=%u sent\n", code);
    return 0;
}

uint16_t icmp_checksum(const void *data, size_t len)
{
    uint32_t       sum = 0;
    const uint8_t *ptr = (const uint8_t *)data;

    for (size_t i = 0; i + 1 < len; i += 2)
        sum += (uint16_t)((uint16_t)ptr[i] | ((uint16_t)ptr[i + 1] << 8));

    if (len & 1)
        sum += ptr[len - 1];

    while (sum >> 16)
        sum = (sum & 0xFFFF) + (sum >> 16);

    return (uint16_t)(~sum);
}

const icmp_stats_t *icmp_get_stats(void)
{
    return &s_icmp_stats;
}

const char *icmp_type_to_string(uint8_t type)
{
    switch (type) {
        case ICMP_TYPE_ECHO_REPLY:      return "Echo Reply";
        case ICMP_TYPE_DEST_UNREACH:    return "Destination Unreachable";
        case ICMP_TYPE_SOURCE_QUENCH:   return "Source Quench";
        case ICMP_TYPE_REDIRECT:        return "Redirect";
        case ICMP_TYPE_ECHO_REQUEST:    return "Echo Request";
        case ICMP_TYPE_TIME_EXCEEDED:   return "Time Exceeded";
        case ICMP_TYPE_PARAM_PROBLEM:   return "Parameter Problem";
        case ICMP_TYPE_TIMESTAMP:       return "Timestamp";
        case ICMP_TYPE_TIMESTAMP_REPLY: return "Timestamp Reply";
        default:                        return "Unknown";
    }
}
