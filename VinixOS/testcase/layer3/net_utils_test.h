#ifndef NET_UTILS_TEST_H
#define NET_UTILS_TEST_H

/*
 * net_utils Test Suite — byte-order + address parse/format
 * Board  : BeagleBone Black (AM335x) — hoặc bất kỳ host nào
 * Module : net_utils.c
 *
 * Tất cả test case đều KHÔNG cần hardware / cable.
 * Chạy ngay sau boot trước khi init ethernet.
 *
 * Cách dùng:
 *   net_utils_test_run();   -- in main.c, trước cpsw_init()
 */

/* Entry point */
void net_utils_test_run(void);

/* Từng test riêng lẻ */
int net_utils_test_htons(void);
int net_utils_test_htonl(void);
int net_utils_test_parse_ip(void);
int net_utils_test_parse_mac(void);
int net_utils_test_fmt_ip(void);
int net_utils_test_fmt_mac(void);
int net_utils_test_roundtrip(void);

#endif /* NET_UTILS_TEST_H */
