/* ============================================================
 * ether.h
 * ------------------------------------------------------------
 * Ethernet Frame Layer — Public Interface
 *
 * Trách nhiệm DUY NHẤT:
 *   TX: nhận payload từ tầng trên → build Ethernet header → cpsw_tx()
 *   RX: nhận raw frame từ CPDMA → strip header → dispatch lên tầng trên
 *
 * KHÔNG xử lý ARP, IP, ICMP — chỉ biết EtherType và MAC address.
 * ARP, IP, ICMP sẽ được thêm sau khi Ethernet layer hoạt động.
 * ============================================================ */

#ifndef ETHER_H
#define ETHER_H

#include "types.h"
#include "net_driver_ops.h"

/* ============================================================
 * Constants
 * ============================================================ */

#define ETH_ADDR_LEN     6
#define ETH_HEADER_LEN   14      /* dst(6) + src(6) + ethertype(2) */
#define ETH_MIN_FRAME    60      /* minimum frame size without FCS */
#define ETH_MAX_PAYLOAD  1010    /* CPSW_FRAME_MAXLEN(1024) - ETH_HEADER_LEN */

/* EtherType values — IEEE 802.3 */
#define ETHERTYPE_IPV4   0x0800
#define ETHERTYPE_ARP    0x0806

/* ============================================================
 * Ethernet Header Struct
 * ============================================================ */

typedef struct {
    uint8_t  dst[ETH_ADDR_LEN];
    uint8_t  src[ETH_ADDR_LEN];
    uint16_t ethertype;          /* network byte order */
} __attribute__((packed)) eth_hdr_t;

/* ============================================================
 * RX Dispatch Callbacks
 * Tầng trên đăng ký callback để nhận frame theo EtherType.
 * ============================================================ */

typedef void (*ether_rx_handler_t)(const uint8_t *payload, uint16_t len);

void ether_set_ipv4_handler(ether_rx_handler_t handler);
void ether_set_arp_handler(ether_rx_handler_t handler);

/* ============================================================
 * Init — gọi sau cpsw_init()
 * ops->get_mac() cung cấp MAC; không cần truyền mac vào ngoài.
 * ============================================================ */

void ether_init(const net_driver_ops_t *ops);

/* ============================================================
 * TX — gọi từ tầng trên (ARP, IP)
 * Returns 0 thành công, -1 lỗi
 * ============================================================ */

int ether_tx(const uint8_t dst_mac[ETH_ADDR_LEN],
             uint16_t ethertype,
             const uint8_t *payload,
             uint16_t len);

/* ============================================================
 * RX — registered as CPDMA callback via cpsw_set_rx_callback()
 * ============================================================ */

void ether_rx(const uint8_t *frame, uint16_t len);

/* ============================================================
 * Utilities
 * ============================================================ */

int  ether_mac_eq(const uint8_t *a, const uint8_t *b);
int  ether_mac_is_bcast(const uint8_t *mac);
void ether_mac_copy(uint8_t *dst, const uint8_t *src);
void ether_mac_print(const uint8_t *mac);
void ether_get_mac(uint8_t mac[ETH_ADDR_LEN]);

#endif /* ETHER_H */
