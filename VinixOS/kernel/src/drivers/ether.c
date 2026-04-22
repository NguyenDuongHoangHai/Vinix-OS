/* ============================================================
 * ether.c
 * ------------------------------------------------------------
 * Ethernet Frame Layer — Layer 3
 *
 * Trách nhiệm DUY NHẤT:
 *   TX: build Ethernet header + gửi qua cpsw_tx()
 *   RX: strip Ethernet header + dispatch lên tầng trên
 *
 * KHÔNG biết gì về ARP, IP, ICMP, UDP.
 * Chỉ biết: MAC address, EtherType, frame boundary.
 *
 * Dependency:
 *   - cpsw.h: cpsw_tx(), cpsw_get_mac(), cpsw_set_rx_callback()
 *   - uart.h: uart_printf()
 *   - string.h: memcpy(), memset()
 * ============================================================ */

#include "ether.h"
#include "cpsw.h"
#include "uart.h"
#include "string.h"

/* ============================================================
 * Static State
 * ============================================================ */

static uint8_t s_mac[ETH_ADDR_LEN];
static uint8_t s_tx_buf[1024];  /* CPSW_FRAME_MAXLEN */

static ether_rx_handler_t s_ipv4_handler = 0;
static ether_rx_handler_t s_arp_handler  = 0;

/* ============================================================
 * Byte Order Helper
 * ============================================================ */

static inline uint16_t bswap16(uint16_t v)
{
    return (uint16_t)((v >> 8) | (v << 8));
}

/* ============================================================
 * Init
 * ============================================================ */

void ether_init(const uint8_t my_mac[ETH_ADDR_LEN])
{
    memcpy(s_mac, my_mac, ETH_ADDR_LEN);
    cpsw_set_rx_callback(ether_rx);

    uart_printf("[ETH] init MAC=%02x:%02x:%02x:%02x:%02x:%02x\n",
                s_mac[0], s_mac[1], s_mac[2],
                s_mac[3], s_mac[4], s_mac[5]);
}

/* ============================================================
 * Handler Registration
 * ============================================================ */

void ether_set_ipv4_handler(ether_rx_handler_t handler)
{
    s_ipv4_handler = handler;
}

void ether_set_arp_handler(ether_rx_handler_t handler)
{
    s_arp_handler = handler;
}

/* ============================================================
 * TX
 * ============================================================ */

int ether_tx(const uint8_t dst_mac[ETH_ADDR_LEN],
             uint16_t ethertype,
             const uint8_t *payload,
             uint16_t len)
{
    if (len > (uint16_t)ETH_MAX_PAYLOAD) {
        uart_printf("[ETH] tx: payload %u > max %u, drop\n",
                    len, ETH_MAX_PAYLOAD);
        return -1;
    }

    eth_hdr_t *hdr = (eth_hdr_t *)s_tx_buf;
    memcpy(hdr->dst, dst_mac, ETH_ADDR_LEN);
    memcpy(hdr->src, s_mac,   ETH_ADDR_LEN);
    hdr->ethertype = bswap16(ethertype);

    memcpy(s_tx_buf + ETH_HEADER_LEN, payload, len);

    uint16_t flen = (uint16_t)(ETH_HEADER_LEN + len);
    if (flen < ETH_MIN_FRAME) {
        memset(s_tx_buf + flen, 0, (uint16_t)(ETH_MIN_FRAME - flen));
        flen = ETH_MIN_FRAME;
    }

    return cpsw_tx(s_tx_buf, flen);
}

/* ============================================================
 * RX — called by CPDMA via cpsw_set_rx_callback()
 * ============================================================ */

void ether_rx(const uint8_t *frame, uint16_t len)
{
    if (len < ETH_HEADER_LEN) {
        uart_printf("[ETH] rx: frame too short (%u), drop\n", len);
        return;
    }

    const eth_hdr_t *hdr = (const eth_hdr_t *)frame;

    /* Accept only frames for us or broadcast */
    if (!ether_mac_eq(hdr->dst, s_mac) && !ether_mac_is_bcast(hdr->dst))
        return;

    uint16_t ethertype     = bswap16(hdr->ethertype);
    const uint8_t *payload = frame + ETH_HEADER_LEN;
    uint16_t plen          = (uint16_t)(len - ETH_HEADER_LEN);

    if (ethertype == ETHERTYPE_IPV4) {
        if (s_ipv4_handler)
            s_ipv4_handler(payload, plen);
        /* else: IPv4 handler not registered yet — silent drop */

    } else if (ethertype == ETHERTYPE_ARP) {
        if (s_arp_handler)
            s_arp_handler(payload, plen);
        /* else: ARP handler not registered yet — silent drop */
    }
    /* Unknown EtherType: silent drop */
}

/* ============================================================
 * Utilities
 * ============================================================ */

int ether_mac_eq(const uint8_t *a, const uint8_t *b)
{
    return (a[0] == b[0] && a[1] == b[1] && a[2] == b[2] &&
            a[3] == b[3] && a[4] == b[4] && a[5] == b[5]);
}

int ether_mac_is_bcast(const uint8_t *mac)
{
    return (mac[0] & mac[1] & mac[2] & mac[3] & mac[4] & mac[5]) == 0xFF;
}

void ether_mac_copy(uint8_t *dst, const uint8_t *src)
{
    memcpy(dst, src, ETH_ADDR_LEN);
}

void ether_mac_print(const uint8_t *mac)
{
    uart_printf("%02x:%02x:%02x:%02x:%02x:%02x",
                mac[0], mac[1], mac[2],
                mac[3], mac[4], mac[5]);
}

void ether_get_mac(uint8_t mac[ETH_ADDR_LEN])
{
    memcpy(mac, s_mac, ETH_ADDR_LEN);
}
