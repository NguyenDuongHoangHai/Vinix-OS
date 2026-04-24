/* ============================================================
 * netcore.c — Network Core (unified entry point for upper layers)
 * ============================================================ */

#include "netcore.h"
#include "arp.h"
#include "ipv4.h"
#include "icmp.h"
#include "ether.h"
#include "uart.h"
#include "string.h"
#include "errno.h"

/* ============================================================
 * Init
 * ============================================================ */

void netcore_init(void)
{
    arp_init();
    ipv4_init();
    icmp_init();

    ether_set_arp_handler(arp_rx);
    ether_set_ipv4_handler(ipv4_rx);

    uart_printf("[NET] initialized\n");
}

/* ============================================================
 * netcore_build_arp_request
 *
 * Builds a raw Ethernet + ARP request frame into caller's buffer.
 * Used by test code to verify ARP packet format without going
 * through the live TX path.
 *
 * Layout: [Ethernet hdr 14B][ARP pkt 28B] = 42 bytes minimum.
 * Returns total byte count on success, -EINVAL if buf too small.
 * ============================================================ */

int netcore_build_arp_request(uint8_t *buf, size_t len, uint32_t target_ip)
{
    if (len < (ETH_HEADER_LEN + ARP_PKT_LEN)) {
        uart_printf("[NET] build_arp_request: buffer too small (%u)\n",
                    (uint32_t)len);
        return -EINVAL;
    }

    /* MAC from ether layer — NOT from cpsw directly */
    static const uint8_t bcast[ETH_ADDR_LEN] =
        {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    uint8_t my_mac[ETH_ADDR_LEN];
    ether_get_mac(my_mac);

    /* Ethernet header */
    memcpy(buf,                  bcast,   ETH_ADDR_LEN);  /* dst */
    memcpy(buf + ETH_ADDR_LEN,   my_mac,  ETH_ADDR_LEN);  /* src */
    buf[12] = (ETHERTYPE_ARP >> 8) & 0xFF;
    buf[13] =  ETHERTYPE_ARP       & 0xFF;

    /* ARP packet */
    arp_pkt_t *arp = (arp_pkt_t *)(buf + ETH_HEADER_LEN);
    memset(arp, 0, ARP_PKT_LEN);

    arp->hw_type    = 0x0100;   /* Ethernet — big-endian */
    arp->proto_type = 0x0008;   /* IPv4 — big-endian */
    arp->hw_len     = 6;
    arp->proto_len  = 4;
    arp->opcode     = 0x0100;   /* Request — big-endian */
    memcpy(arp->sender_mac, my_mac, ETH_ADDR_LEN);
    arp->sender_ip  = ((s_my_ip & 0x000000FFu) << 24) |
                      ((s_my_ip & 0x0000FF00u) <<  8) |
                      ((s_my_ip & 0x00FF0000u) >>  8) |
                      ((s_my_ip & 0xFF000000u) >> 24);
    memset(arp->target_mac, 0, ETH_ADDR_LEN);
    arp->target_ip  = ((target_ip & 0x000000FFu) << 24) |
                      ((target_ip & 0x0000FF00u) <<  8) |
                      ((target_ip & 0x00FF0000u) >>  8) |
                      ((target_ip & 0xFF000000u) >> 24);

    return (int)(ETH_HEADER_LEN + ARP_PKT_LEN);
}

/* ============================================================
 * Thin wrappers — single call-site for upper layers / tests
 * ============================================================ */

int netcore_arp_resolve(uint32_t ip, uint8_t mac[6])
{
    return arp_resolve(ip, mac);
}

int netcore_send(uint32_t dst_ip, uint8_t protocol,
                 const void *data, size_t len)
{
    return ipv4_tx(dst_ip, protocol, data, len);
}

int netcore_ping(uint32_t dst_ip, uint16_t identifier, uint16_t sequence,
                 const void *data, size_t data_len)
{
    return icmp_ping(dst_ip, identifier, sequence, data, data_len);
}
