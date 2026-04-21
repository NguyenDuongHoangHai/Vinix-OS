/* ============================================================
 * netcore.c
 * ------------------------------------------------------------
 * Network Core — Ethernet + ARP + IPv4 + ICMP merged layer
 *
 * Merge của 4 file gốc: ether.c + arp.c + ip.c + icmp.c.
 * Lý do merge: loại bỏ cross-layer extern, 1 static state block,
 * gọi trực tiếp giữa sub-layer thay vì function pointer.
 *
 * Dependency duy nhất từ hardware: cpsw.h (TX + RX callback).
 * UDP layer (udp.c) hook vào qua netcore_set_udp_handler().
 * ============================================================ */

#include "netcore.h"
#include "cpsw.h"
#include "uart.h"
#include "string.h"

extern uint32_t timer_get_ticks(void);

/* ============================================================
 * === CONSTANTS ===
 * ============================================================ */

#define ETHERTYPE_ARP    0x0806
#define ETHERTYPE_IPV4   0x0800

#define IP_PROTO_ICMP    1
#define IP_PROTO_UDP     17

#define ETH_HEADER_LEN   14
#define ETH_MIN_FRAME    60
/* ETH_PAYLOAD_MAX: limited by CPSW TX buffer (1024 bytes total) */
#define ETH_PAYLOAD_MAX  (CPSW_FRAME_MAXLEN - ETH_HEADER_LEN)    /* 1010 */

#define ARP_CACHE_SIZE   8
#define ARP_TIMEOUT      6000     /* ticks = 60 s at 10 ms/tick (RFC 1122) */
#define ARP_RESOLVE_ITER 50000    /* poll iterations before giving up */

#define IP_HEADER_LEN    20
#define IP_VER_IHL       0x45     /* version=4, IHL=5 (no options) */
#define IP_TTL           64       /* RFC 1122 recommended default */
#define IP_PAYLOAD_MAX   (ETH_PAYLOAD_MAX - IP_HEADER_LEN)       /* 990  */

#define ICMP_HEADER_LEN  8
#define ICMP_ECHO_REQ    8
#define ICMP_ECHO_REP    0
#define ICMP_DATA_LEN    56       /* standard ping payload = 56 bytes */
#define ICMP_WAIT_ITER   200000   /* ~2 s before timeout */

#define UDP_HEADER_LEN   8
#define UDP_DATA_MAX     (IP_PAYLOAD_MAX - UDP_HEADER_LEN)        /* 982  */

/* ============================================================
 * === PACKED STRUCTS ===
 * ============================================================ */

typedef struct {
    uint8_t  dst[6];
    uint8_t  src[6];
    uint16_t ethertype;
} __attribute__((packed)) eth_hdr_t;

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

typedef struct {
    uint8_t  ver_ihl;
    uint8_t  tos;
    uint16_t total_len;
    uint16_t id;
    uint16_t flags_frag;
    uint8_t  ttl;
    uint8_t  protocol;
    uint16_t checksum;
    uint32_t src_ip;
    uint32_t dst_ip;
} __attribute__((packed)) ip_hdr_t;

typedef struct {
    uint8_t  type;
    uint8_t  code;
    uint16_t checksum;
    uint16_t id;
    uint16_t seq;
} __attribute__((packed)) icmp_hdr_t;

typedef struct {
    uint16_t src_port;
    uint16_t dst_port;
    uint16_t length;
    uint16_t checksum;
} __attribute__((packed)) udp_hdr_t;

/* ============================================================
 * === STATIC STATE ===
 * ============================================================ */

static uint8_t  g_mac[6];
static uint32_t g_ip;
static uint16_t g_ip_id;
static uint8_t  g_tx_frame[CPSW_FRAME_MAXLEN];   /* 1024 bytes */

typedef struct {
    uint32_t ip;
    uint8_t  mac[6];
    uint32_t ts;
    uint8_t  valid;
} arp_entry_t;

static arp_entry_t g_arp[ARP_CACHE_SIZE];

static struct {
    uint8_t  got;
    uint16_t id;
    uint16_t seq;
    uint32_t ts;
} g_icmp_rep;

static void (*g_udp_cb)(uint32_t src_ip, uint16_t src_port,
                         uint16_t dst_port,
                         const uint8_t *data, uint16_t len);

/* ============================================================
 * === HELPERS ===
 * ============================================================ */

static inline uint16_t bswap16(uint16_t v)
{
    return (uint16_t)((v >> 8) | (v << 8));
}

/* ----------------------------------------------------------
 * [FIX] net_cksum — tránh unaligned memory access
 * ----------------------------------------------------------
 * Vấn đề: cast (uint8_t *) → (uint16_t *) rồi dereference.
 * Nguyên nhân: ARMv7-A phát Data Abort khi đọc uint16_t từ
 *   địa chỉ lẻ (không align 2-byte). RX buffer từ CPSW và
 *   các static buffer không đảm bảo align.
 * Fix: đọc từng byte thủ công, ghép thành word 16-bit.
 * ---------------------------------------------------------- */
static uint16_t net_cksum(const uint8_t *p, uint32_t len)
{
    uint32_t s = 0;
    while (len > 1) {
        s += ((uint32_t)p[0] << 8) | p[1];
        p += 2; len -= 2;
    }
    if (len) s += (uint32_t)p[0] << 8;
    while (s >> 16) s = (s & 0xFFFF) + (s >> 16);
    return (uint16_t)(~s);
}
/* ---------------------------------------------------------- */

static int mac_is_bcast(const uint8_t *m)
{
    return (m[0] & m[1] & m[2] & m[3] & m[4] & m[5]) == 0xFF;
}

static int mac_eq(const uint8_t *a, const uint8_t *b)
{
    return (a[0] == b[0] && a[1] == b[1] && a[2] == b[2] &&
            a[3] == b[3] && a[4] == b[4] && a[5] == b[5]);
}

/* ============================================================
 * === ETHERNET TX ===
 * Defined first — called by ARP and IP layers below.
 * ============================================================ */

static int ether_tx(const uint8_t *dst, uint16_t etype,
                    const uint8_t *payload, uint16_t len)
{
    if (len > (uint16_t)ETH_PAYLOAD_MAX) {
        uart_printf("[NET] ether_tx: payload %d > max %d\n",
                    len, ETH_PAYLOAD_MAX);
        return -1;
    }

    eth_hdr_t *hdr = (eth_hdr_t *)g_tx_frame;
    memcpy(hdr->dst, dst, 6);
    memcpy(hdr->src, g_mac, 6);
    hdr->ethertype = bswap16(etype);
    memcpy(g_tx_frame + ETH_HEADER_LEN, payload, len);

    uint16_t flen = (uint16_t)(ETH_HEADER_LEN + len);
    if (flen < ETH_MIN_FRAME) {
        memset(g_tx_frame + flen, 0, ETH_MIN_FRAME - flen);
        flen = ETH_MIN_FRAME;
    }
    return cpsw_tx(g_tx_frame, flen);
}

/* ============================================================
 * === ARP CACHE ===
 * ============================================================ */

static arp_entry_t *arp_find(uint32_t ip)
{
    uint32_t now = timer_get_ticks();
    for (int i = 0; i < ARP_CACHE_SIZE; i = i + 1) {
        if (g_arp[i].valid && g_arp[i].ip == ip) {
            if ((now - g_arp[i].ts) < ARP_TIMEOUT)
                return &g_arp[i];
            g_arp[i].valid = 0;   /* expired — lazy evict */
        }
    }
    return 0;
}

static arp_entry_t *arp_alloc(void)
{
    int oldest = 0;
    for (int i = 0; i < ARP_CACHE_SIZE; i = i + 1) {
        if (!g_arp[i].valid) return &g_arp[i];
        if (g_arp[i].ts < g_arp[oldest].ts) oldest = i;
    }
    return &g_arp[oldest];   /* evict oldest */
}

static void arp_cache_update(uint32_t ip, const uint8_t *mac)
{
    arp_entry_t *e = arp_find(ip);
    if (!e) e = arp_alloc();
    e->ip    = ip;
    e->ts    = timer_get_ticks();
    e->valid = 1;
    memcpy(e->mac, mac, 6);
}

/* ============================================================
 * === ARP TX ===
 * ============================================================ */

static void arp_request(uint32_t target_ip)
{
    static const uint8_t bcast[6] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
    arp_pkt_t p;

    p.hw_type    = bswap16(0x0001);
    p.proto_type = bswap16(ETHERTYPE_IPV4);
    p.hw_len     = 6;
    p.proto_len  = 4;
    p.opcode     = bswap16(0x0001);   /* Request */
    memcpy(p.sender_mac, g_mac, 6);
    p.sender_ip  = g_ip;
    memset(p.target_mac, 0, 6);
    p.target_ip  = target_ip;

    ether_tx(bcast, ETHERTYPE_ARP, (const uint8_t *)&p, sizeof(p));
}

/* Cache miss: broadcast Request then poll for Reply via cpsw_rx_poll.
 * cpsw_rx_poll() → netcore_rx() → arp_rx() → arp_cache_update().
 * Polling here avoids needing interrupt-driven RX for bring-up. */
static int arp_resolve(uint32_t ip, uint8_t *mac_out)
{
    arp_entry_t *e = arp_find(ip);
    if (e) { memcpy(mac_out, e->mac, 6); return 0; }

    arp_request(ip);
    for (int i = ARP_RESOLVE_ITER; i > 0; i = i - 1) {
        cpsw_rx_poll();
        e = arp_find(ip);
        if (e) { memcpy(mac_out, e->mac, 6); return 0; }
    }

    uart_printf("[NET] ARP timeout for %d.%d.%d.%d\n",
                ip & 0xFF, (ip >> 8) & 0xFF,
                (ip >> 16) & 0xFF, (ip >> 24) & 0xFF);
    return -1;
}

/* ============================================================
 * === ARP RX ===
 * ============================================================ */

static void arp_rx(const uint8_t *payload, uint16_t len)
{
    if (len < (uint16_t)sizeof(arp_pkt_t)) return;

    const arp_pkt_t *p = (const arp_pkt_t *)payload;

    /* RFC 826: update cache on any valid ARP frame from sender */
    arp_cache_update(p->sender_ip, p->sender_mac);

    if (bswap16(p->opcode) == 0x0001 && p->target_ip == g_ip) {
        arp_pkt_t rep;
        rep.hw_type    = bswap16(0x0001);
        rep.proto_type = bswap16(ETHERTYPE_IPV4);
        rep.hw_len     = 6;
        rep.proto_len  = 4;
        rep.opcode     = bswap16(0x0002);   /* Reply */
        memcpy(rep.sender_mac, g_mac, 6);
        rep.sender_ip  = g_ip;
        memcpy(rep.target_mac, p->sender_mac, 6);
        rep.target_ip  = p->sender_ip;

        ether_tx(p->sender_mac, ETHERTYPE_ARP,
                 (const uint8_t *)&rep, sizeof(rep));
        uart_printf("[NET] ARP Reply → %d.%d.%d.%d\n",
                    p->sender_ip & 0xFF, (p->sender_ip >> 8) & 0xFF,
                    (p->sender_ip >> 16) & 0xFF, (p->sender_ip >> 24) & 0xFF);
    }
}

/* ============================================================
 * === IP TX ===
 * ============================================================ */

static int ip_tx(uint32_t dst_ip, uint8_t proto,
                 const uint8_t *payload, uint16_t len)
{
    static uint8_t tx[ETH_PAYLOAD_MAX];   /* IP header + IP payload */

    if (len > (uint16_t)IP_PAYLOAD_MAX) return -1;

    uint8_t dst_mac[6];
    if (arp_resolve(dst_ip, dst_mac) != 0) return -1;

    ip_hdr_t *hdr = (ip_hdr_t *)tx;
    hdr->ver_ihl    = IP_VER_IHL;
    hdr->tos        = 0;
    hdr->total_len  = bswap16((uint16_t)(IP_HEADER_LEN + len));
    hdr->id         = bswap16(g_ip_id);
    g_ip_id         = g_ip_id + 1;
    hdr->flags_frag = 0;
    hdr->ttl        = IP_TTL;
    hdr->protocol   = proto;
    hdr->checksum   = 0;
    hdr->src_ip     = g_ip;
    hdr->dst_ip     = dst_ip;
    hdr->checksum   = net_cksum(tx, IP_HEADER_LEN);

    memcpy(tx + IP_HEADER_LEN, payload, len);

    return ether_tx(dst_mac, ETHERTYPE_IPV4,
                    tx, (uint16_t)(IP_HEADER_LEN + len));
}

/* ============================================================
 * === ICMP RX ===
 * ============================================================ */

static void icmp_rx(uint32_t src_ip, const uint8_t *payload, uint16_t len)
{
    if (len < ICMP_HEADER_LEN) return;

    const icmp_hdr_t *hdr = (const icmp_hdr_t *)payload;

    if (hdr->type == ICMP_ECHO_REQ) {
        static uint8_t reply_buf[ICMP_HEADER_LEN + ICMP_DATA_LEN];

        icmp_hdr_t *rep = (icmp_hdr_t *)reply_buf;
        rep->type     = ICMP_ECHO_REP;
        rep->code     = 0;
        rep->checksum = 0;
        rep->id       = hdr->id;
        rep->seq      = hdr->seq;

        uint16_t dlen = (uint16_t)(len - ICMP_HEADER_LEN);
        if (dlen > ICMP_DATA_LEN) dlen = ICMP_DATA_LEN;
        memcpy(reply_buf + ICMP_HEADER_LEN, payload + ICMP_HEADER_LEN, dlen);

        uint16_t rlen = (uint16_t)(ICMP_HEADER_LEN + dlen);
        rep->checksum = net_cksum(reply_buf, rlen);

        ip_tx(src_ip, IP_PROTO_ICMP, reply_buf, rlen);
        uart_printf("[NET] ICMP Echo Reply → %d.%d.%d.%d\n",
                    src_ip & 0xFF, (src_ip >> 8) & 0xFF,
                    (src_ip >> 16) & 0xFF, (src_ip >> 24) & 0xFF);

    } else if (hdr->type == ICMP_ECHO_REP) {
        g_icmp_rep.got = 1;
        g_icmp_rep.id  = bswap16(hdr->id);
        g_icmp_rep.seq = bswap16(hdr->seq);
        g_icmp_rep.ts  = timer_get_ticks();
    }
}

/* ============================================================
 * === UDP RX ===
 * ============================================================ */

static void udp_rx(uint32_t src_ip, const uint8_t *payload, uint16_t len)
{
    if (len < UDP_HEADER_LEN) return;

    const udp_hdr_t *hdr = (const udp_hdr_t *)payload;
    uint16_t dlen = bswap16(hdr->length);
    if (dlen < UDP_HEADER_LEN) return;
    dlen = (uint16_t)(dlen - UDP_HEADER_LEN);
    /* ----------------------------------------------------------
     * [FIX] Clamp dlen theo bytes thực nhận
     * ----------------------------------------------------------
     * Vấn đề: hdr->length là giá trị trong UDP header — có thể
     *   lớn hơn số bytes thực sự nhận được (truncated packet).
     * Nguyên nhân: không có bound check → callback đọc ngoài
     *   RX buffer.
     * Fix: clamp dlen xuống max_dlen tính từ len thực tế.
     * ---------------------------------------------------------- */
    uint16_t max_dlen = (len > UDP_HEADER_LEN) ? (len - UDP_HEADER_LEN) : 0;
    if (dlen > max_dlen) dlen = max_dlen;
    /* ---------------------------------------------------------- */

    if (g_udp_cb)
        g_udp_cb(src_ip, bswap16(hdr->src_port), bswap16(hdr->dst_port),
                 payload + UDP_HEADER_LEN, dlen);
}

/* ============================================================
 * === IP RX ===
 * ============================================================ */

static void ip_rx(const uint8_t *payload, uint16_t len)
{
    if (len < IP_HEADER_LEN) return;

    const ip_hdr_t *hdr = (const ip_hdr_t *)payload;

    if (hdr->ver_ihl != IP_VER_IHL) return;

    /* Drop fragments — no reassembly on bare-metal */
    if (hdr->flags_frag & bswap16(0x1FFF)) return;

    if (net_cksum(payload, IP_HEADER_LEN) != 0) return;

    if (hdr->dst_ip != g_ip && hdr->dst_ip != 0xFFFFFFFF) return;

    uint16_t total   = bswap16(hdr->total_len);
    /* ----------------------------------------------------------
     * [FIX] Guard ip_plen underflow — malformed packet
     * ----------------------------------------------------------
     * Vấn đề: nếu total_len < IP_HEADER_LEN hoặc total_len >
     *   len thực nhận, phép trừ tạo ra giá trị rất lớn (wrap).
     * Nguyên nhân: uint16_t underflow — không có bound check.
     * Fix: validate total trước khi tính ip_plen.
     * ---------------------------------------------------------- */
    if (total < IP_HEADER_LEN || total > len) return;
    uint16_t ip_plen      = total - IP_HEADER_LEN;
    /* ---------------------------------------------------------- */
    const uint8_t *ip_pay = payload + IP_HEADER_LEN;

    if (hdr->protocol == IP_PROTO_ICMP)
        icmp_rx(hdr->src_ip, ip_pay, ip_plen);
    else if (hdr->protocol == IP_PROTO_UDP)
        udp_rx(hdr->src_ip, ip_pay, ip_plen);
}

/* ============================================================
 * === PUBLIC API ===
 * ============================================================ */

void netcore_rx(const uint8_t *frame, uint16_t len)
{
    if (len < ETH_HEADER_LEN) return;

    const eth_hdr_t *hdr = (const eth_hdr_t *)frame;

    if (!mac_eq(hdr->dst, g_mac) && !mac_is_bcast(hdr->dst)) return;

    uint16_t etype         = bswap16(hdr->ethertype);
    const uint8_t *payload = frame + ETH_HEADER_LEN;
    uint16_t plen          = (uint16_t)(len - ETH_HEADER_LEN);

    if (etype == ETHERTYPE_ARP)
        arp_rx(payload, plen);
    else if (etype == ETHERTYPE_IPV4)
        ip_rx(payload, plen);
}

int netcore_init(uint32_t my_ip)
{
    cpsw_get_mac(g_mac);
    g_ip    = my_ip;
    g_ip_id = 0;
    memset(g_arp, 0, sizeof(g_arp));
    g_icmp_rep.got = 0;
    g_udp_cb       = 0;

    cpsw_set_rx_callback(netcore_rx);

    uart_printf("[NET] Init MAC=%02x:%02x:%02x:%02x:%02x:%02x"
                " IP=%d.%d.%d.%d\n",
                g_mac[0], g_mac[1], g_mac[2],
                g_mac[3], g_mac[4], g_mac[5],
                my_ip & 0xFF, (my_ip >> 8) & 0xFF,
                (my_ip >> 16) & 0xFF, (my_ip >> 24) & 0xFF);
    return 0;
}

int netcore_send_udp(uint32_t dst_ip, uint16_t dst_port,
                     uint16_t src_port,
                     const uint8_t *data, uint16_t len)
{
    static uint8_t udp_buf[UDP_HEADER_LEN + UDP_DATA_MAX];

    if (len > (uint16_t)UDP_DATA_MAX) {
        uart_printf("[NET] send_udp: data %d > max %d\n", len, UDP_DATA_MAX);
        return -1;
    }

    udp_hdr_t *hdr = (udp_hdr_t *)udp_buf;
    hdr->src_port = bswap16(src_port);
    hdr->dst_port = bswap16(dst_port);
    hdr->length   = bswap16((uint16_t)(UDP_HEADER_LEN + len));
    hdr->checksum = 0;   /* UDP checksum optional for IPv4 */

    memcpy(udp_buf + UDP_HEADER_LEN, data, len);

    return ip_tx(dst_ip, IP_PROTO_UDP,
                 udp_buf, (uint16_t)(UDP_HEADER_LEN + len));
}

int netcore_ping(uint32_t dst_ip, uint16_t id, uint16_t seq,
                 uint32_t *rtt_ticks)
{
    static uint8_t buf[ICMP_HEADER_LEN + ICMP_DATA_LEN];

    icmp_hdr_t *hdr = (icmp_hdr_t *)buf;
    hdr->type     = ICMP_ECHO_REQ;
    hdr->code     = 0;
    hdr->checksum = 0;
    hdr->id       = bswap16(id);
    hdr->seq      = bswap16(seq);

    for (int i = 0; i < ICMP_DATA_LEN; i = i + 1)
        buf[ICMP_HEADER_LEN + i] = (uint8_t)(i & 0xFF);

    uint16_t len  = (uint16_t)(ICMP_HEADER_LEN + ICMP_DATA_LEN);
    hdr->checksum = net_cksum(buf, len);

    g_icmp_rep.got = 0;
    if (ip_tx(dst_ip, IP_PROTO_ICMP, buf, len) != 0) return -1;

    uint32_t t0 = timer_get_ticks();
    for (int i = ICMP_WAIT_ITER; i > 0; i = i - 1) {
        cpsw_rx_poll();
        if (g_icmp_rep.got && g_icmp_rep.id == id && g_icmp_rep.seq == seq) {
            if (rtt_ticks)
                *rtt_ticks = timer_get_ticks() - t0;
            g_icmp_rep.got = 0;
            return 0;
        }
    }

    uart_printf("[NET] ping timeout\n");
    return -1;
}

void netcore_get_mac(uint8_t mac[6])
{
    memcpy(mac, g_mac, 6);
}

uint32_t netcore_get_ip(void)
{
    return g_ip;
}

void netcore_set_udp_handler(void (*cb)(uint32_t src_ip, uint16_t src_port,
                                        uint16_t dst_port,
                                        const uint8_t *data, uint16_t len))
{
    g_udp_cb = cb;
}
