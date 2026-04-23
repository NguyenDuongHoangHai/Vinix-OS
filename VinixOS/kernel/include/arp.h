/* ============================================================
 * arp.h
 * ------------------------------------------------------------
 * ARP Protocol — Address Resolution Protocol (RFC 826)
 *
 * Trách nhiệm:
 *   - Resolve IP address to MAC address
 *   - Manage ARP cache
 *   - Handle ARP requests/replies
 *
 * Dependencies:
 *   - ether.h: ether_tx(), ether_set_arp_handler()
 *   - uart.h: uart_printf()
 *   - string.h: memcpy(), memset()
 * ============================================================ */

#ifndef ARP_H
#define ARP_H

#include "types.h"

/* ============================================================
 * Constants
 * ============================================================ */

#define ARP_HW_TYPE_ETHER    1       /* Ethernet */
#define ARP_PROTO_TYPE_IPV4   0x0800  /* IPv4 */
#define ARP_OPCODE_REQUEST    1       /* ARP request */
#define ARP_OPCODE_REPLY      2       /* ARP reply */
#define ARP_PKT_LEN           28      /* ARP packet size */
#define ARP_CACHE_SIZE        8       /* ARP cache entries */

/* ============================================================
 * ARP Packet Structure (RFC 826)
 * ============================================================ */

typedef struct {
    uint16_t hw_type;        /* Hardware type (e.g., Ethernet) */
    uint16_t proto_type;      /* Protocol type (e.g., IPv4) */
    uint8_t  hw_len;         /* Hardware address length */
    uint8_t  proto_len;       /* Protocol address length */
    uint16_t opcode;         /* Operation (request/reply) */
    uint8_t  sender_mac[6];   /* Sender hardware address */
    uint32_t sender_ip;       /* Sender protocol address */
    uint8_t  target_mac[6];   /* Target hardware address */
    uint32_t target_ip;       /* Target protocol address */
} __attribute__((packed)) arp_pkt_t;

/* ============================================================
 * ARP Cache Entry
 * ============================================================ */

typedef struct {
    uint32_t ip;             /* IP address */
    uint8_t  mac[6];         /* MAC address */
    uint32_t timeout;        /* Entry timeout (seconds) */
    uint8_t  valid;          /* Entry valid flag */
} arp_cache_entry_t;

/* ============================================================
 * Public Interface
 * ============================================================ */

/* Initialize ARP subsystem */
void arp_init(void);

/* Resolve IP address to MAC address */
/* Returns 0 on success, -1 on timeout */
int arp_resolve(uint32_t ip, uint8_t mac[6]);

/* Update ARP cache entry */
void arp_cache_update(uint32_t ip, const uint8_t mac[6]);

/* Lookup ARP cache entry */
/* Returns 0 on found, -1 on not found */
int arp_cache_lookup(uint32_t ip, uint8_t mac[6]);

/* Flush ARP cache */
void arp_cache_flush(void);

/* Set my IP and MAC addresses */
void arp_set_my_ip(uint32_t ip);
void arp_set_my_mac(const uint8_t mac[6]);

/* ============================================================
 * Internal Functions (called by ether.c)
 * ============================================================ */

/* Handle incoming ARP packet */
void arp_rx(const uint8_t *payload, uint16_t len);

#endif /* ARP_H */
