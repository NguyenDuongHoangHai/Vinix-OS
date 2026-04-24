/* ============================================================
 * net_utils.c
 * ------------------------------------------------------------
 * Network utility functions: byte-order, address parse/format
 * ============================================================ */

#include "net_utils.h"

/* ============================================================
 * Static helpers
 * ============================================================ */

/* Write decimal representation of v into out. Returns chars written (1–3). */
static int u8_to_dec(uint8_t v, char *out)
{
    int n = 0;
    if (v >= 100) { out[n++] = (char)('0' + v / 100); v = (uint8_t)(v % 100); }
    if (v >= 10 || n > 0) { out[n++] = (char)('0' + v / 10); v = (uint8_t)(v % 10); }
    out[n++] = (char)('0' + v);
    return n;
}

static const char s_hex[] = "0123456789abcdef";

/* Parse up to 3 decimal digits from s. Returns chars consumed, -1 on error. */
static int parse_dec_u8(const char *s, uint8_t *val)
{
    int n = 0;
    uint32_t v = 0;
    while (s[n] >= '0' && s[n] <= '9') {
        v = v * 10u + (uint32_t)(s[n] - '0');
        if (v > 255u) return -1;
        n++;
    }
    if (n == 0) return -1;
    *val = (uint8_t)v;
    return n;
}

/* Parse exactly 1–2 hex digits from s. Returns chars consumed, -1 on error. */
static int parse_hex_u8(const char *s, uint8_t *val)
{
    int n = 0;
    uint32_t v = 0;
    while (n < 2) {
        char c = s[n];
        uint32_t d;
        if      (c >= '0' && c <= '9') d = (uint32_t)(c - '0');
        else if (c >= 'a' && c <= 'f') d = (uint32_t)(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F') d = (uint32_t)(c - 'A' + 10);
        else break;
        v = (v << 4) | d;
        n++;
    }
    if (n == 0) return -1;
    *val = (uint8_t)v;
    return n;
}

/* ============================================================
 * Byte order
 * ============================================================ */

uint16_t htons(uint16_t v)
{
    return (uint16_t)((v >> 8) | (v << 8));
}

uint16_t ntohs(uint16_t v)
{
    return htons(v);
}

uint32_t htonl(uint32_t v)
{
    return ((v & 0xFF000000u) >> 24) |
           ((v & 0x00FF0000u) >>  8) |
           ((v & 0x0000FF00u) <<  8) |
           ((v & 0x000000FFu) << 24);
}

uint32_t ntohl(uint32_t v)
{
    return htonl(v);
}

/* ============================================================
 * Address parsing
 * ============================================================ */

int net_parse_ip(const char *str, uint32_t *out)
{
    uint8_t oct[4];
    int pos = 0;
    int i;

    for (i = 0; i < 4; i++) {
        int n = parse_dec_u8(str + pos, &oct[i]);
        if (n < 0) return -1;
        pos += n;
        if (i < 3) {
            if (str[pos] != '.') return -1;
            pos++;
        }
    }
    if (str[pos] != '\0') return -1;

    /* Little-endian packed: byte 0 = first octet.
     * Matches NETCORE_IP4(a,b,c,d) — bytes land in network order on wire. */
    *out = (uint32_t)oct[0]
         | ((uint32_t)oct[1] <<  8)
         | ((uint32_t)oct[2] << 16)
         | ((uint32_t)oct[3] << 24);
    return 0;
}

int net_parse_mac(const char *str, uint8_t out[6])
{
    int pos = 0;
    int i;

    for (i = 0; i < 6; i++) {
        int n = parse_hex_u8(str + pos, &out[i]);
        if (n < 0) return -1;
        pos += n;
        if (i < 5) {
            if (str[pos] != ':') return -1;
            pos++;
        }
    }
    if (str[pos] != '\0') return -1;
    return 0;
}

/* ============================================================
 * Address formatting
 * ============================================================ */

void net_fmt_ip(uint32_t ip, char *buf, int buflen)
{
    uint8_t oct[4];
    int pos = 0;
    int i;

    if (buflen <= 0) return;

    oct[0] = (uint8_t)(ip & 0xFFu);
    oct[1] = (uint8_t)((ip >>  8) & 0xFFu);
    oct[2] = (uint8_t)((ip >> 16) & 0xFFu);
    oct[3] = (uint8_t)((ip >> 24) & 0xFFu);

    for (i = 0; i < 4; i++) {
        if (pos + 4 >= buflen) break;
        pos += u8_to_dec(oct[i], buf + pos);
        if (i < 3) buf[pos++] = '.';
    }
    buf[pos] = '\0';
}

void net_fmt_mac(const uint8_t mac[6], char *buf, int buflen)
{
    int pos = 0;
    int i;

    if (buflen <= 0) return;

    for (i = 0; i < 6; i++) {
        if (pos + 3 >= buflen) break;
        buf[pos++] = s_hex[mac[i] >> 4];
        buf[pos++] = s_hex[mac[i] & 0xFu];
        if (i < 5) buf[pos++] = ':';
    }
    buf[pos] = '\0';
}
