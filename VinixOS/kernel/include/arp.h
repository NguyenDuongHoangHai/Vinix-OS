/* ============================================================
 * arp.h
 * ------------------------------------------------------------
 * ARP Protocol — RFC 826
 * Scope: who-has / is-at, 8-entry cache with 60 s expiry.
 * ============================================================ */

#ifndef ARP_H
#define ARP_H

#include "types.h"

/* ============================================================
 * Constants
 * ============================================================ */

#define ARP_HW_TYPE_ETHER     1
#define ARP_PROTO_TYPE_IPV4   0x0800
#define ARP_OPCODE_REQUEST    1
#define ARP_OPCODE_REPLY      2
#define ARP_PKT_LEN           28
#define ARP_CACHE_SIZE        8

/* Cache entry expires after this many timer ticks.
 * Assumes scheduler timer at 10 ms/tick → 6000 ticks = 60 s.
 * Adjust if timer rate changes. */
#define ARP_CACHE_TIMEOUT_TICKS  6000

/* arp_resolve retries before giving up */
#define ARP_RESOLVE_RETRIES  3

/* Ticks to wait per retry (10 ms/tick → 300 ticks = 3 s) */
#define ARP_RETRY_TICKS  300u

/* ============================================================
 * ARP Packet — RFC 826
 * ============================================================ */

typedef struct {
    uint16_t hw_type;
    uint16_t proto_type;
    uint8_t  hw_len;
    uint8_t  proto_len;
    uint16_t opcode;
    uint8_t  sender_mac[6];
    uint32_t sender_ip;
    uint8_t  target_mac[6];
    uint32_t target_ip;
} __attribute__((packed)) arp_pkt_t;

/* ============================================================
 * ARP Cache Entry
 * ============================================================ */

typedef struct {
    uint32_t ip;
    uint8_t  mac[6];
    uint32_t created_ticks;   /* timer_get_ticks() at insertion */
    uint8_t  valid;
} arp_cache_entry_t;

/* ============================================================
 * Public Interface
 * ============================================================ */

void arp_init(void);
int  arp_resolve(uint32_t ip, uint8_t mac[6]);
void arp_cache_update(uint32_t ip, const uint8_t mac[6]);
int  arp_cache_lookup(uint32_t ip, uint8_t mac[6]);
void arp_cache_flush(void);
void arp_set_my_ip(uint32_t ip);
void arp_set_my_mac(const uint8_t mac[6]);

/* Called by ether.c RX dispatch */
void arp_rx(const uint8_t *payload, uint16_t len);

/* My IP — shared with ipv4.c (read-only outside arp.c) */
extern uint32_t s_my_ip;

#endif /* ARP_H */
