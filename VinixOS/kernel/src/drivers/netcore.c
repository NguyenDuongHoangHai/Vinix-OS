/* ============================================================
 * netcore.c
 * ------------------------------------------------------------
 * Network Core — Unified Network Interface
 *
 * Trách nhiệm:
 *   - Cung cấp unified interface cho network protocols
 *   - Protocol registration và dispatch
 *   - Wrapper functions cho ARP, IPv4, ICMP
 *
 * Dependencies:
 *   - arp.h: ARP protocol implementation
 *   - ether.h: Ethernet layer
 *   - uart.h: Debug output
 * ============================================================ */

#include "netcore.h"
#include "arp.h"
#include "ipv4.h"
#include "icmp.h"
#include "ether.h"
#include "uart.h"
#include "string.h"

/* ============================================================
 * Constants
 * ============================================================ */

#define NETCORE_BROADCAST_MAC {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF}

/* ============================================================
 * Public API Implementation
 * ============================================================ */

void netcore_init(void)
{
    /* Initialize ARP subsystem */
    arp_init();
    
    /* Initialize IPv4 subsystem */
    ipv4_init();
    
    /* Initialize ICMP subsystem */
    icmp_init();
    
    /* Register handlers with Ethernet layer */
    ether_set_arp_handler(arp_rx);
    ether_set_ipv4_handler(ipv4_rx);
    
    uart_printf("[NETCORE] initialized\n");
}

int netcore_build_arp_request(uint8_t *buf, size_t len, uint32_t target_ip)
{
    if (len < (14 + 28)) {  /* Ethernet header + ARP packet */
        uart_printf("[NETCORE] buffer too small for ARP request\n");
        return -1;
    }
    
    /* Build Ethernet header */
    static const uint8_t bcast[6] = NETCORE_BROADCAST_MAC;
    uint8_t my_mac[6];
    cpsw_get_mac(my_mac);
    
    /* Destination MAC (broadcast) */
    memcpy(buf, bcast, 6);
    /* Source MAC (our MAC) */
    memcpy(buf + 6, my_mac, 6);
    /* EtherType (ARP) */
    buf[12] = 0x08;
    buf[13] = 0x06;
    
    /* Build ARP packet */
    arp_pkt_t *arp = (arp_pkt_t*)(buf + 14);
    memset(arp, 0, sizeof(arp_pkt_t));
    
    arp->hw_type    = 0x0100;  /* Ethernet (big-endian) */
    arp->proto_type = 0x0008;  /* IPv4 (big-endian) */
    arp->hw_len     = 6;
    arp->proto_len  = 4;
    arp->opcode     = 0x0100;  /* Request (big-endian) */
    
    memcpy(arp->sender_mac, my_mac, 6);
    /* Get IP from ARP subsystem */
    arp->sender_ip  = s_my_ip;  /* Use current IP setting */
    memset(arp->target_mac, 0, 6);
    arp->target_ip  = target_ip;    /* Already in big-endian format */
    
    return 14 + 28;  /* Return total packet length */
}

int netcore_arp_resolve(uint32_t ip, uint8_t mac[6])
{
    return arp_resolve(ip, mac);
}

int netcore_send(uint32_t dst_ip, uint8_t protocol, const void *data, size_t len)
{
    /* Send via IPv4 */
    return ipv4_tx(dst_ip, protocol, data, len);
}

int netcore_ping(uint32_t dst_ip, uint16_t identifier, uint16_t sequence,
                 const void *data, size_t data_len)
{
    /* Send ICMP ping request */
    return icmp_ping(dst_ip, identifier, sequence, data, data_len);
}
