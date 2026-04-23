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
    
    /* Register ARP handler with Ethernet layer */
    ether_set_arp_handler(arp_rx);
    
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
    arp->sender_ip  = 0x640a8ac0;  /* 192.168.10.100 (big-endian) */
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
    /* TODO: Implement IPv4 routing and sending */
    uart_printf("[NETCORE] send: dst=");
    uart_printf("%u.%u.%u.%u",
                (dst_ip >> 24) & 0xFF,
                (dst_ip >> 16) & 0xFF,
                (dst_ip >> 8) & 0xFF,
                dst_ip & 0xFF);
    uart_printf(" proto=%u len=%zu\n", protocol, len);
    
    /* For now, just return not implemented */
    return -1;
}
