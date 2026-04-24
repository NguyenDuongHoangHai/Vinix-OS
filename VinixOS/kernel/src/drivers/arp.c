/* ============================================================
 * arp.c — ARP Protocol (RFC 826)
 * ============================================================ */

#include "arp.h"
#include "ether.h"
#include "uart.h"
#include "string.h"
#include "spinlock.h"
#include "wait_queue.h"
#include "timer.h"
#include "errno.h"

/* ============================================================
 * Static State
 * ============================================================ */

uint32_t s_my_ip  = 0;
static uint8_t s_my_mac[6] = {0};

static arp_cache_entry_t s_cache[ARP_CACHE_SIZE];
static spinlock_t        s_cache_lock = SPINLOCK_INIT;

/* wait queue: arp_resolve blocks here until arp_rx calls wake_up */
static wait_queue_head_t s_arp_wq;

/* ============================================================
 * Byte-swap helpers (no system header available in kernel)
 * ============================================================ */

static inline uint16_t bswap16(uint16_t v)
{
    return (uint16_t)((v >> 8) | (v << 8));
}

static inline uint32_t bswap32(uint32_t v)
{
    return ((v & 0x000000FFu) << 24) |
           ((v & 0x0000FF00u) <<  8) |
           ((v & 0x00FF0000u) >>  8) |
           ((v & 0xFF000000u) >> 24);
}

/* ============================================================
 * Print helpers
 * ============================================================ */

static void arp_print_ip(uint32_t ip)
{
    uart_printf("%u.%u.%u.%u",
                (ip >> 24) & 0xFF, (ip >> 16) & 0xFF,
                (ip >>  8) & 0xFF,  ip         & 0xFF);
}

static void arp_print_mac(const uint8_t mac[6])
{
    uart_printf("%02x:%02x:%02x:%02x:%02x:%02x",
                mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

/* ============================================================
 * Cache — internal (caller must hold s_cache_lock)
 * ============================================================ */

/* Returns index of a valid, non-expired entry matching ip, or -1. */
static int cache_find_entry(uint32_t ip)
{
    uint32_t now = timer_get_ticks();

    for (int i = 0; i < ARP_CACHE_SIZE; i++) {
        if (!s_cache[i].valid)
            continue;

        /* Expire stale entries on the fly */
        if ((now - s_cache[i].created_ticks) > ARP_CACHE_TIMEOUT_TICKS) {
            uart_printf("[ARP] cache expired: ");
            arp_print_ip(s_cache[i].ip);
            uart_printf("\n");
            s_cache[i].valid = 0;
            continue;
        }

        if (s_cache[i].ip == ip)
            return i;
    }
    return -1;
}

/* Returns index of a free slot, or evicts index 0 (simple FIFO). */
static int cache_find_free(void)
{
    for (int i = 0; i < ARP_CACHE_SIZE; i++) {
        if (!s_cache[i].valid)
            return i;
    }
    return 0;   /* cache full — evict oldest (slot 0) */
}

/* ============================================================
 * Public cache API
 * ============================================================ */

int arp_cache_lookup(uint32_t ip, uint8_t mac[6])
{
    uint32_t flags = spin_lock_irqsave(&s_cache_lock);
    int idx = cache_find_entry(ip);
    if (idx >= 0)
        memcpy(mac, s_cache[idx].mac, 6);
    spin_unlock_irqrestore(&s_cache_lock, flags);
    return (idx >= 0) ? 0 : -EINVAL;
}

void arp_cache_update(uint32_t ip, const uint8_t mac[6])
{
    uint32_t flags = spin_lock_irqsave(&s_cache_lock);

    int idx = cache_find_entry(ip);
    if (idx < 0)
        idx = cache_find_free();

    s_cache[idx].ip            = ip;
    s_cache[idx].created_ticks = timer_get_ticks();
    s_cache[idx].valid         = 1;
    memcpy(s_cache[idx].mac, mac, 6);

    spin_unlock_irqrestore(&s_cache_lock, flags);

    uart_printf("[ARP] cache update: ");
    arp_print_ip(ip);
    uart_printf(" -> ");
    arp_print_mac(mac);
    uart_printf("\n");

    /* Wake any task blocked in arp_resolve() */
    wake_up(&s_arp_wq);
}

void arp_cache_flush(void)
{
    uint32_t flags = spin_lock_irqsave(&s_cache_lock);
    memset(s_cache, 0, sizeof(s_cache));
    spin_unlock_irqrestore(&s_cache_lock, flags);
    uart_printf("[ARP] cache flushed\n");
}

/* ============================================================
 * Init / config
 * ============================================================ */

void arp_init(void)
{
    memset(s_cache, 0, sizeof(s_cache));
    wait_queue_init(&s_arp_wq);
    uart_printf("[ARP] init IP=");
    arp_print_ip(s_my_ip);
    uart_printf("\n");
}

void arp_set_my_ip(uint32_t ip)  { s_my_ip = ip; }
void arp_set_my_mac(const uint8_t mac[6]) { memcpy(s_my_mac, mac, 6); }

/* ============================================================
 * TX helpers
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
    pkt.sender_ip  = bswap32(s_my_ip);
    memset(pkt.target_mac, 0, 6);
    pkt.target_ip  = bswap32(target_ip);

    uart_printf("[ARP] who-has ");
    arp_print_ip(target_ip);
    uart_printf(" tell ");
    arp_print_ip(s_my_ip);
    uart_printf("\n");

    int ret = ether_tx(bcast, ETHERTYPE_ARP, (uint8_t *)&pkt, ARP_PKT_LEN);
    if (ret != 0)
        uart_printf("[ARP] request TX failed: %d\n", ret);
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
    pkt.sender_ip  = bswap32(s_my_ip);
    memcpy(pkt.target_mac, target_mac, 6);
    pkt.target_ip  = bswap32(target_ip);

    uart_printf("[ARP] is-at reply -> ");
    arp_print_ip(target_ip);
    uart_printf("\n");

    int ret = ether_tx(target_mac, ETHERTYPE_ARP, (uint8_t *)&pkt, ARP_PKT_LEN);
    if (ret != 0)
        uart_printf("[ARP] reply TX failed: %d\n", ret);
}

/* ============================================================
 * RX
 * ============================================================ */

void arp_rx(const uint8_t *payload, uint16_t len)
{
    if (len < ARP_PKT_LEN) {
        uart_printf("[ARP] RX: too short (%u), drop\n", len);
        return;
    }

    const arp_pkt_t *pkt = (const arp_pkt_t *)payload;

    if (bswap16(pkt->hw_type)    != ARP_HW_TYPE_ETHER  ||
        bswap16(pkt->proto_type) != ARP_PROTO_TYPE_IPV4 ||
        pkt->hw_len    != 6 || pkt->proto_len != 4) {
        uart_printf("[ARP] RX: invalid format, drop\n");
        return;
    }

    uint16_t opcode    = bswap16(pkt->opcode);
    uint32_t sender_ip = bswap32(pkt->sender_ip);
    uint32_t target_ip = bswap32(pkt->target_ip);

    uart_printf("[ARP] RX: opcode=%u sender=", opcode);
    arp_print_ip(sender_ip);
    uart_printf(" target=");
    arp_print_ip(target_ip);
    uart_printf("\n");

    /* Always learn sender MAC/IP */
    arp_cache_update(sender_ip, pkt->sender_mac);
    /* wake_up called inside arp_cache_update */

    if (opcode == ARP_OPCODE_REQUEST && target_ip == s_my_ip) {
        uart_printf("[ARP] answering who-has for us\n");
        arp_send_reply(sender_ip, pkt->sender_mac);
    }
}

/* ============================================================
 * Resolve
 * ============================================================ */

int arp_resolve(uint32_t ip, uint8_t mac[6])
{
    /* Fast path — already cached */
    if (arp_cache_lookup(ip, mac) == 0)
        return 0;

    /* Send request and block until arp_rx() updates the cache.
     * wake_up(&s_arp_wq) is called from arp_cache_update().
     * Retry ARP_RESOLVE_RETRIES times; a timer watchdog (M5) will
     * be needed to guarantee bounded timeout when host is unreachable. */
    for (int retry = 0; retry < ARP_RESOLVE_RETRIES; retry++) {
        uart_printf("[ARP] resolve: sending request (retry %d)\n", retry);
        arp_send_request(ip);

        wait_event(s_arp_wq, arp_cache_lookup(ip, mac) == 0);

        if (arp_cache_lookup(ip, mac) == 0)
            return 0;
    }

    uart_printf("[ARP] resolve: timeout for ");
    arp_print_ip(ip);
    uart_printf("\n");
    return -EAGAIN;
}
