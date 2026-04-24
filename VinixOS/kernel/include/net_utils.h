/* ============================================================
 * net_utils.h
 * ------------------------------------------------------------
 * Network utility functions: byte-order, address parse/format
 * ============================================================ */

#ifndef NET_UTILS_H
#define NET_UTILS_H

#include "types.h"

/* ============================================================
 * Byte order — AM335x is little-endian, network is big-endian
 * ============================================================ */

uint16_t htons(uint16_t v);
uint16_t ntohs(uint16_t v);
uint32_t htonl(uint32_t v);
uint32_t ntohl(uint32_t v);

/* ============================================================
 * Address parsing
 * net_parse_ip  : "192.168.1.1"           → uint32_t (little-endian packed)
 * net_parse_mac : "DE:AD:BE:EF:00:01"     → uint8_t[6]
 *
 * IP convention: out = a|(b<<8)|(c<<16)|(d<<24)
 *   bytes in memory [a,b,c,d] = network byte order on wire.
 *   Matches NETCORE_IP4(a,b,c,d) used in existing tests.
 *
 * Returns 0 on success, -1 on malformed input.
 * ============================================================ */

int net_parse_ip(const char *str, uint32_t *out);
int net_parse_mac(const char *str, uint8_t out[6]);

/* ============================================================
 * Address formatting (null-terminated output)
 * net_fmt_ip  : uint32_t     → "a.b.c.d\0"          (max 16 bytes)
 * net_fmt_mac : uint8_t[6]   → "xx:xx:xx:xx:xx:xx\0" (max 18 bytes)
 *
 * buflen must be at least 16 / 18 respectively.
 * Output is silently truncated if buflen is too small.
 * ============================================================ */

void net_fmt_ip(uint32_t ip, char *buf, int buflen);
void net_fmt_mac(const uint8_t mac[6], char *buf, int buflen);

#endif /* NET_UTILS_H */
