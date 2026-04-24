/* ============================================================
 * net_utils_test.c
 * ------------------------------------------------------------
 * Test Suite: byte-order + address parse/format
 * Hardware  : không cần — pure logic test
 * ============================================================ */

#include "net_utils_test.h"
#include "net_utils.h"
#include "uart.h"
#include "string.h"

#define PASS 1
#define FAIL 0

static void print_sep(void)
{
    uart_printf("--------------------------------------------------\n");
}

/* ============================================================
 * T1: htons / ntohs
 *
 * Mục đích : Verify htons() swap đúng byte order 16-bit.
 *            AM335x là little-endian — htons phải đảo 2 byte.
 *
 * Test vectors:
 *   htons(0x0800) = 0x0008   ← EtherType IPv4: 0x0800 ở network order
 *                                = 0x0008 khi đọc là LE uint16_t
 *   htons(0x0806) = 0x0608   ← EtherType ARP
 *   htons(0x0001) = 0x0100
 *   ntohs(htons(0x1234)) = 0x1234  ← round-trip
 *   htons(0x0000) = 0x0000   ← zero giữ nguyên
 *   htons(0xFFFF) = 0xFFFF   ← all-ones giữ nguyên
 *
 * Không cần: cable, hardware
 * ============================================================ */
int net_utils_test_htons(void)
{
    print_sep();
    uart_printf("[T1] htons / ntohs\n");

    int ok = PASS;

#define CHECK16(expr, expect) do {                                          \
    uint16_t _v = (expr);                                                   \
    int _ok = (_v == (uint16_t)(expect));                                   \
    uart_printf("     %-32s = 0x%04x (expect 0x%04x) %s\n",                \
                #expr, _v, (uint16_t)(expect), _ok ? "OK" : "FAIL");       \
    if (!_ok) ok = FAIL;                                                    \
} while (0)

    CHECK16(htons(0x0800u), 0x0008u);
    CHECK16(htons(0x0806u), 0x0608u);
    CHECK16(htons(0x0001u), 0x0100u);
    CHECK16(htons(0x0000u), 0x0000u);
    CHECK16(htons(0xFFFFu), 0xFFFFu);
    CHECK16(ntohs(htons(0x1234u)), 0x1234u);
    CHECK16(ntohs(0x0008u), 0x0800u);

#undef CHECK16

    uart_printf("     => %s\n", ok == PASS ? "PASS" : "FAIL");
    return ok;
}

/* ============================================================
 * T2: htonl / ntohl
 *
 * Mục đích : Verify htonl() swap đúng byte order 32-bit.
 *
 * Test vectors:
 *   htonl(0x0101A8C0) = 0xC0A80101  ← 192.168.1.1 packed LE → NBO
 *   htonl(0xC0A80101) = 0x0101A8C0  ← NBO → packed LE (symmetric)
 *   htonl(0x00000001) = 0x01000000
 *   ntohl(htonl(0x12345678)) = 0x12345678  ← round-trip
 *   htonl(0x00000000) = 0x00000000
 *   htonl(0xFFFFFFFF) = 0xFFFFFFFF
 *
 * Không cần: cable, hardware
 * ============================================================ */
int net_utils_test_htonl(void)
{
    print_sep();
    uart_printf("[T2] htonl / ntohl\n");

    int ok = PASS;

#define CHECK32(expr, expect) do {                                          \
    uint32_t _v = (expr);                                                   \
    int _ok = (_v == (uint32_t)(expect));                                   \
    uart_printf("     %-32s = 0x%08x (expect 0x%08x) %s\n",                \
                #expr, _v, (uint32_t)(expect), _ok ? "OK" : "FAIL");       \
    if (!_ok) ok = FAIL;                                                    \
} while (0)

    /* 0x0101A8C0 = NETCORE_IP4(192,168,1,1) in host LE — swap gives NBO */
    CHECK32(htonl(0x0101A8C0u), 0xC0A80101u);
    CHECK32(htonl(0xC0A80101u), 0x0101A8C0u);
    CHECK32(htonl(0x00000001u), 0x01000000u);
    CHECK32(htonl(0x00000000u), 0x00000000u);
    CHECK32(htonl(0xFFFFFFFFu), 0xFFFFFFFFu);
    CHECK32(ntohl(htonl(0x12345678u)), 0x12345678u);

#undef CHECK32

    uart_printf("     => %s\n", ok == PASS ? "PASS" : "FAIL");
    return ok;
}

/* ============================================================
 * T3: net_parse_ip
 *
 * Mục đích : Verify parse IP string → packed LE uint32_t.
 *            Kết quả phải khớp NETCORE_IP4(a,b,c,d) convention.
 *
 * Test vectors:
 *   "192.168.1.1"      → 0x0101A8C0  (= NETCORE_IP4(192,168,1,1))
 *   "0.0.0.0"          → 0x00000000
 *   "255.255.255.255"  → 0xFFFFFFFF
 *   "10.0.0.1"         → 0x0100000A
 *   "192.168.1.100"    → 0x6401A8C0  (BBB IP)
 *
 * Error cases:
 *   ""         → -1
 *   "999.0.0.0"→ -1  (octet overflow)
 *   "1.2.3"    → -1  (missing octet)
 *   "1.2.3.4.5"→ -1  (trailing garbage)
 *   "a.b.c.d"  → -1  (non-digit)
 *
 * Không cần: cable, hardware
 * ============================================================ */
int net_utils_test_parse_ip(void)
{
    print_sep();
    uart_printf("[T3] net_parse_ip\n");

    int ok = PASS;
    uint32_t ip;

#define CHECK_IP(str, expect_ret, expect_val) do {                          \
    ip = 0xDEADBEEFu;                                                       \
    int _r = net_parse_ip((str), &ip);                                      \
    int _ok;                                                                \
    if ((expect_ret) == 0)                                                  \
        _ok = (_r == 0 && ip == (uint32_t)(expect_val));                    \
    else                                                                    \
        _ok = (_r != 0);                                                    \
    uart_printf("     parse_ip(%-22s) r=%2d ip=0x%08x  %s\n",              \
                "\"" str "\"", _r, ip, _ok ? "OK" : "FAIL");               \
    if (!_ok) ok = FAIL;                                                    \
} while (0)

    /* Valid */
    CHECK_IP("192.168.1.1",      0, 0x0101A8C0u);
    CHECK_IP("0.0.0.0",          0, 0x00000000u);
    CHECK_IP("255.255.255.255",  0, 0xFFFFFFFFu);
    CHECK_IP("10.0.0.1",         0, 0x0100000Au);
    CHECK_IP("192.168.1.100",    0, 0x6401A8C0u);

    /* Invalid */
    CHECK_IP("",             -1, 0);
    CHECK_IP("999.0.0.0",    -1, 0);
    CHECK_IP("1.2.3",        -1, 0);
    CHECK_IP("1.2.3.4.5",    -1, 0);
    CHECK_IP("a.b.c.d",      -1, 0);

#undef CHECK_IP

    uart_printf("     => %s\n", ok == PASS ? "PASS" : "FAIL");
    return ok;
}

/* ============================================================
 * T4: net_parse_mac
 *
 * Mục đích : Verify parse MAC string → uint8_t[6].
 *
 * Test vectors:
 *   "DE:AD:BE:EF:00:01" → {0xDE, 0xAD, 0xBE, 0xEF, 0x00, 0x01}
 *   "ff:ff:ff:ff:ff:ff" → {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}
 *   "00:00:00:00:00:00" → {0x00, ...}
 *   "02:03:04:50:60:48" → VinixOS default MAC
 *
 * Error cases:
 *   "GG:00:00:00:00:00" → -1  (invalid hex)
 *   "DE:AD:BE:EF:00"    → -1  (too short)
 *   "DE-AD-BE-EF-00-01" → -1  (wrong separator)
 *
 * Không cần: cable, hardware
 * ============================================================ */
int net_utils_test_parse_mac(void)
{
    print_sep();
    uart_printf("[T4] net_parse_mac\n");

    int ok = PASS;
    uint8_t mac[6];

#define CHECK_MAC(str, expect_ret, b0,b1,b2,b3,b4,b5) do {                 \
    int _r = net_parse_mac((str), mac);                                     \
    int _ok;                                                                \
    if ((expect_ret) == 0)                                                  \
        _ok = (_r == 0 && mac[0]==(b0) && mac[1]==(b1) && mac[2]==(b2) && \
               mac[3]==(b3) && mac[4]==(b4) && mac[5]==(b5));              \
    else                                                                    \
        _ok = (_r != 0);                                                    \
    uart_printf("     parse_mac(%-22s) r=%2d  %s\n",                       \
                "\"" str "\"", _r, _ok ? "OK" : "FAIL");                   \
    if (!_ok) ok = FAIL;                                                    \
} while (0)

    /* Valid */
    CHECK_MAC("DE:AD:BE:EF:00:01",  0, 0xDE,0xAD,0xBE,0xEF,0x00,0x01);
    CHECK_MAC("ff:ff:ff:ff:ff:ff",  0, 0xFF,0xFF,0xFF,0xFF,0xFF,0xFF);
    CHECK_MAC("00:00:00:00:00:00",  0, 0x00,0x00,0x00,0x00,0x00,0x00);
    CHECK_MAC("02:03:04:50:60:48",  0, 0x02,0x03,0x04,0x50,0x60,0x48);

    /* Invalid */
    CHECK_MAC("GG:00:00:00:00:00", -1, 0,0,0,0,0,0);
    CHECK_MAC("DE:AD:BE:EF:00",    -1, 0,0,0,0,0,0);
    CHECK_MAC("DE-AD-BE-EF-00-01", -1, 0,0,0,0,0,0);

#undef CHECK_MAC

    uart_printf("     => %s\n", ok == PASS ? "PASS" : "FAIL");
    return ok;
}

/* ============================================================
 * T5: net_fmt_ip
 *
 * Mục đích : Verify format uint32_t → IP string.
 *
 * Test vectors:
 *   0x0101A8C0 → "192.168.1.1"
 *   0x00000000 → "0.0.0.0"
 *   0xFFFFFFFF → "255.255.255.255"
 *   0x6401A8C0 → "192.168.1.100"
 *   0x0100000A → "10.0.0.1"
 *
 * Edge case: buflen = 0 → không crash (no write)
 *
 * Không cần: cable, hardware
 * ============================================================ */
int net_utils_test_fmt_ip(void)
{
    print_sep();
    uart_printf("[T5] net_fmt_ip\n");

    int ok = PASS;
    char buf[20];

#define CHECK_FMT_IP(ip_val, expect_str) do {                               \
    net_fmt_ip((ip_val), buf, (int)sizeof(buf));                            \
    int _ok = (strcmp(buf, (expect_str)) == 0);                             \
    uart_printf("     fmt_ip(0x%08x) = %-18s (expect %-18s) %s\n",         \
                (uint32_t)(ip_val), buf, (expect_str), _ok ? "OK":"FAIL");  \
    if (!_ok) ok = FAIL;                                                    \
} while (0)

    CHECK_FMT_IP(0x0101A8C0u, "192.168.1.1");
    CHECK_FMT_IP(0x00000000u, "0.0.0.0");
    CHECK_FMT_IP(0xFFFFFFFFu, "255.255.255.255");
    CHECK_FMT_IP(0x6401A8C0u, "192.168.1.100");
    CHECK_FMT_IP(0x0100000Au, "10.0.0.1");

#undef CHECK_FMT_IP

    /* buflen=0 must not crash */
    net_fmt_ip(0x0101A8C0u, buf, 0);
    uart_printf("     fmt_ip(buflen=0) no crash OK\n");

    uart_printf("     => %s\n", ok == PASS ? "PASS" : "FAIL");
    return ok;
}

/* ============================================================
 * T6: net_fmt_mac
 *
 * Mục đích : Verify format uint8_t[6] → MAC string (lowercase hex).
 *
 * Test vectors:
 *   {0xDE,0xAD,0xBE,0xEF,0x00,0x01} → "de:ad:be:ef:00:01"
 *   {0xFF,...}                       → "ff:ff:ff:ff:ff:ff"
 *   {0x00,...}                       → "00:00:00:00:00:00"
 *   {0x02,0x03,0x04,0x50,0x60,0x48} → "02:03:04:50:60:48"
 *
 * Không cần: cable, hardware
 * ============================================================ */
int net_utils_test_fmt_mac(void)
{
    print_sep();
    uart_printf("[T6] net_fmt_mac\n");

    int ok = PASS;
    char buf[20];

#define CHECK_FMT_MAC(b0,b1,b2,b3,b4,b5, expect_str) do {                  \
    uint8_t _m[6] = {(b0),(b1),(b2),(b3),(b4),(b5)};                        \
    net_fmt_mac(_m, buf, (int)sizeof(buf));                                 \
    int _ok = (strcmp(buf, (expect_str)) == 0);                             \
    uart_printf("     fmt_mac({%02x..}) = %-20s (expect %-20s) %s\n",      \
                (uint8_t)(b0), buf, (expect_str), _ok ? "OK" : "FAIL");    \
    if (!_ok) ok = FAIL;                                                    \
} while (0)

    CHECK_FMT_MAC(0xDE,0xAD,0xBE,0xEF,0x00,0x01, "de:ad:be:ef:00:01");
    CHECK_FMT_MAC(0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,  "ff:ff:ff:ff:ff:ff");
    CHECK_FMT_MAC(0x00,0x00,0x00,0x00,0x00,0x00,  "00:00:00:00:00:00");
    CHECK_FMT_MAC(0x02,0x03,0x04,0x50,0x60,0x48,  "02:03:04:50:60:48");

#undef CHECK_FMT_MAC

    uart_printf("     => %s\n", ok == PASS ? "PASS" : "FAIL");
    return ok;
}

/* ============================================================
 * T7: Round-trip parse → format
 *
 * Mục đích : Verify parse → format trả về đúng string gốc.
 *            Đây là test tích hợp xác nhận cả hai hàm khớp nhau.
 *
 * Test vectors:
 *   IP : "192.168.1.1"  "0.0.0.0"  "10.20.30.40"  "255.255.255.255"
 *   MAC: "de:ad:be:ef:00:01"  "ff:ff:ff:ff:ff:ff"
 *
 * Không cần: cable, hardware
 * ============================================================ */
int net_utils_test_roundtrip(void)
{
    print_sep();
    uart_printf("[T7] Round-trip parse→format\n");

    int ok = PASS;
    char buf[20];
    uint32_t ip;
    uint8_t  mac[6];

    /* IP round-trip */
    static const char *ip_cases[] = {
        "192.168.1.1",
        "0.0.0.0",
        "10.20.30.40",
        "255.255.255.255",
        "192.168.1.100",
        (const char *)0
    };

    int i;
    for (i = 0; ip_cases[i] != (const char *)0; i++) {
        net_parse_ip(ip_cases[i], &ip);
        net_fmt_ip(ip, buf, (int)sizeof(buf));
        int match = (strcmp(buf, ip_cases[i]) == 0);
        uart_printf("     IP  round-trip %-18s => %-18s %s\n",
                    ip_cases[i], buf, match ? "OK" : "FAIL");
        if (!match) ok = FAIL;
    }

    /* MAC round-trip (lowercase input matches lowercase output) */
    static const char *mac_cases[] = {
        "de:ad:be:ef:00:01",
        "ff:ff:ff:ff:ff:ff",
        "00:00:00:00:00:00",
        "02:03:04:50:60:48",
        (const char *)0
    };

    for (i = 0; mac_cases[i] != (const char *)0; i++) {
        net_parse_mac(mac_cases[i], mac);
        net_fmt_mac(mac, buf, (int)sizeof(buf));
        int match = (strcmp(buf, mac_cases[i]) == 0);
        uart_printf("     MAC round-trip %-20s => %-20s %s\n",
                    mac_cases[i], buf, match ? "OK" : "FAIL");
        if (!match) ok = FAIL;
    }

    uart_printf("     => %s\n", ok == PASS ? "PASS" : "FAIL");
    return ok;
}

/* ============================================================
 * Entry point
 * ============================================================ */

void net_utils_test_run(void)
{
    uart_printf("\n");
    uart_printf("==================================================\n");
    uart_printf("  NET_UTILS TEST SUITE\n");
    uart_printf("  Module : net_utils.c\n");
    uart_printf("  Hardware required: NONE\n");
    uart_printf("==================================================\n");

    int results[7];
    results[0] = net_utils_test_htons();
    results[1] = net_utils_test_htonl();
    results[2] = net_utils_test_parse_ip();
    results[3] = net_utils_test_parse_mac();
    results[4] = net_utils_test_fmt_ip();
    results[5] = net_utils_test_fmt_mac();
    results[6] = net_utils_test_roundtrip();

    print_sep();
    uart_printf("SUMMARY:\n");

    static const char *names[7] = {
        "T1 htons/ntohs     ",
        "T2 htonl/ntohl     ",
        "T3 parse_ip        ",
        "T4 parse_mac       ",
        "T5 fmt_ip          ",
        "T6 fmt_mac         ",
        "T7 roundtrip       "
    };

    int pass_count = 0;
    int j;
    for (j = 0; j < 7; j++) {
        const char *status = (results[j] == PASS) ? "PASS" : "FAIL";
        if (results[j] == PASS) pass_count++;
        uart_printf("  %s : %s\n", names[j], status);
    }

    print_sep();
    uart_printf("RESULT: %d/7  —  net_utils %s\n",
                pass_count,
                (pass_count == 7) ? "READY" : "NOT READY");
    uart_printf("==================================================\n\n");
}
