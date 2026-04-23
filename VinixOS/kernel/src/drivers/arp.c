/* ============================================================
 * arp.c
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

#include "arp.h"
#include "ether.h"
#include "uart.h"
#include "string.h"

/* ============================================================
 * Static State
 * ============================================================ */

static uint32_t s_my_ip = 0;
static uint8_t  s_my_mac[6] = {0};
static arp_cache_entry_t s_cache[ARP_CACHE_SIZE];
static uint32_t s_cache_timeout = 60; /* 60 seconds timeout */

/* ============================================================
 * Helper Functions
 * ============================================================ */

static inline uint16_t bswap16(uint16_t v)
{
    return (uint16_t)((v >> 8) | (v << 8));
}

static void arp_print_ip(uint32_t ip)
{
    uart_printf("%u.%u.%u.%u",
                (ip >> 24) & 0xFF,
                (ip >> 16) & 0xFF,
                (ip >> 8) & 0xFF,
                ip & 0xFF);
}

static void arp_print_mac(const uint8_t mac[6])
{
    uart_printf("%02x:%02x:%02x:%02x:%02x:%02x",
                mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

/* ============================================================
 * ARP Cache Management
 * ============================================================ */

static int cache_find_entry(uint32_t ip)
{
    for (int i = 0; i < ARP_CACHE_SIZE; i++) {
        if (s_cache[i].valid && s_cache[i].ip == ip) {
            return i;
        }
    }
    return -1;
}

static int cache_find_free(void)
{
    for (int i = 0; i < ARP_CACHE_SIZE; i++) {
        if (!s_cache[i].valid) {
            return i;
        }
    }
    /* Cache full - use LRU (simple: use first entry) */
    return 0;
}

static void cache_update_timeout(void)
{
    /* Simple timeout handling - mark expired entries as invalid */
    for (int i = 0; i < ARP_CACHE_SIZE; i++) {
        if (s_cache[i].valid && s_cache[i].timeout == 0) {
            s_cache[i].valid = 0;
        } else if (s_cache[i].valid) {
            s_cache[i].timeout--;
        }
    }
}

/* ============================================================
 * Public API
 * ============================================================ */

void arp_init(void)
{
    memset(s_cache, 0, sizeof(s_cache));
    uart_printf("[ARP] init IP=");
    arp_print_ip(s_my_ip);
    uart_printf(" (raw=0x%08x)\n", s_my_ip);
}

void arp_set_my_ip(uint32_t ip)
{
    s_my_ip = ip;
}

void arp_set_my_mac(const uint8_t mac[6])
{
    memcpy(s_my_mac, mac, 6);
}

int arp_cache_lookup(uint32_t ip, uint8_t mac[6])
{
    int idx = cache_find_entry(ip);
    if (idx >= 0) {
        memcpy(mac, s_cache[idx].mac, 6);
        return 0;
    }
    return -1;
}

void arp_cache_update(uint32_t ip, const uint8_t mac[6])
{
    int idx = cache_find_entry(ip);
    if (idx < 0) {
        idx = cache_find_free();
        s_cache[idx].ip = ip;
    }
    
    memcpy(s_cache[idx].mac, mac, 6);
    s_cache[idx].timeout = s_cache_timeout;
    s_cache[idx].valid = 1;
    
    uart_printf("[ARP] cache update: ");
    arp_print_ip(ip);
    uart_printf(" -> ");
    arp_print_mac(mac);
    uart_printf("\n");
}

void arp_cache_flush(void)
{
    memset(s_cache, 0, sizeof(s_cache));
    uart_printf("[ARP] cache flushed\n");
}

/* ============================================================
 * Internal Functions
 * ============================================================ */

static void arp_send_request(uint32_t target_ip)
{
    static const uint8_t bcast[6] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};
    
    arp_pkt_t pkt;
    memset(&pkt, 0, sizeof(pkt));
    
    pkt.hw_type    = bswap16(ARP_HW_TYPE_ETHER);
    pkt.proto_type = bswap16(ARP_PROTO_TYPE_IPV4);
    pkt.hw_len     = 6;
    pkt.proto_len  = 4;
    pkt.opcode     = bswap16(ARP_OPCODE_REQUEST);
    
    memcpy(pkt.sender_mac, s_my_mac, 6);
    pkt.sender_ip  = s_my_ip;
    memcpy(pkt.target_mac, bcast, 6);  /* Broadcast */
    pkt.target_ip  = target_ip;
    
    uart_printf("[ARP] Sending who-has ");
    arp_print_ip(target_ip);
    uart_printf("\n");
    uart_printf("[ARP] My MAC: ");
    arp_print_mac(s_my_mac);
    uart_printf("\n");
    uart_printf("[ARP] My IP: ");
    arp_print_ip(s_my_ip);
    uart_printf("\n");
    
    /* Debug: packet dump */
    uart_printf("[ARP] packet dump (%u bytes):\n", ARP_PKT_LEN);
    uint8_t *data = (uint8_t*)&pkt;
    for (int i = 0; i < ARP_PKT_LEN; i++) {
        if (i % 16 == 0) uart_printf("[ARP] %d: ", i);
        uart_printf("%02x ", data[i]);
        if (i % 16 == 15) uart_printf("\n");
    }
    if (ARP_PKT_LEN % 16 != 0) uart_printf("\n");
    
    int ret = ether_tx(bcast, ETHERTYPE_ARP, (uint8_t*)&pkt, ARP_PKT_LEN);
    if (ret == 0) {
        uart_printf("[ARP] ARP request queued for TX\n");
    } else {
        uart_printf("[ARP] ARP request TX failed: %d\n", ret);
    }
}

static void arp_send_reply(uint32_t target_ip, const uint8_t target_mac[6])
{
    arp_pkt_t pkt;
    memset(&pkt, 0, sizeof(pkt));
    
    pkt.hw_type    = bswap16(ARP_HW_TYPE_ETHER);
    pkt.proto_type = bswap16(ARP_PROTO_TYPE_IPV4);
    pkt.hw_len     = 6;
    pkt.proto_len  = 4;
    pkt.opcode     = bswap16(ARP_OPCODE_REPLY);
    
    memcpy(pkt.sender_mac, s_my_mac, 6);
    pkt.sender_ip  = s_my_ip;
    memcpy(pkt.target_mac, target_mac, 6);
    pkt.target_ip  = target_ip;
    
    uart_printf("[ARP] Reply -> ");
    arp_print_ip(target_ip);
    uart_printf("\n");
    
    int ret = ether_tx(target_mac, ETHERTYPE_ARP, (uint8_t*)&pkt, ARP_PKT_LEN);
    if (ret != 0) {
        uart_printf("[ARP] ARP reply TX failed: %d\n", ret);
    }
}

void arp_rx(const uint8_t *payload, uint16_t len)
{
    if (len < ARP_PKT_LEN) {
        uart_printf("[ARP] RX: packet too short (%u), drop\n", len);
        return;
    }
    
    const arp_pkt_t *pkt = (const arp_pkt_t*)payload;
    
    /* Verify packet format */
    if (bswap16(pkt->hw_type) != ARP_HW_TYPE_ETHER ||
        bswap16(pkt->proto_type) != ARP_PROTO_TYPE_IPV4 ||
        pkt->hw_len != 6 || pkt->proto_len != 4) {
        uart_printf("[ARP] RX: invalid packet format\n");
        return;
    }
    
    uint16_t opcode = bswap16(pkt->opcode);
    uint32_t sender_ip = pkt->sender_ip;
    uint32_t target_ip = pkt->target_ip;
    
    uart_printf("[ARP] RX: opcode=%u, sender=", opcode);
    arp_print_ip(sender_ip);
    uart_printf(", target=");
    arp_print_ip(target_ip);
    uart_printf("\n");
    
    /* Update cache with sender info */
    arp_cache_update(sender_ip, pkt->sender_mac);
    
    /* Process packet */
    uart_printf("[ARP] Checking: opcode=%u, target_ip=", opcode);
    arp_print_ip(target_ip);
    uart_printf(", my_ip=");
    arp_print_ip(s_my_ip);
    uart_printf("\n");
    
    if (opcode == ARP_OPCODE_REQUEST && target_ip == s_my_ip) {
        /* ARP request for us - send reply */
        uart_printf("[ARP] Got request for us, sending reply\n");
        arp_send_reply(sender_ip, pkt->sender_mac);
    } else if (opcode == ARP_OPCODE_REPLY) {
        /* ARP reply - cache already updated */
        uart_printf("[ARP] Got reply, cache updated\n");
    } else {
        uart_printf("[ARP] Not our request - ignoring\n");
    }
}

int arp_resolve(uint32_t ip, uint8_t mac[6])
{
    /* Check cache first */
    if (arp_cache_lookup(ip, mac) == 0) {
        return 0;
    }
    
    /* Send ARP request */
    arp_send_request(ip);
    
    /* Wait for reply (simple polling) */
    uint32_t timeout = 50000; /* ~5 seconds */
    while (timeout > 0) {
        if (arp_cache_lookup(ip, mac) == 0) {
            return 0;
        }
        
        /* Simple delay */
        for (volatile int i = 0; i < 1000; i++);
        timeout -= 1000;
        
        if (timeout % 10000 == 0) {
            uart_printf("[ARP] still polling... %u\n", timeout/1000);
        }
    }
    
    uart_printf("[ARP] timeout resolving ");
    arp_print_ip(ip);
    uart_printf("\n");
    return -1;
}
