#ifndef NETCORE_TEST_H
#define NETCORE_TEST_H

/*
 * Layer 3 — Network Core Test Suite (ARP + IPv4 + ICMP)
 * Board  : BeagleBone Black (AM335x)
 * Driver : netcore.c
 *
 * Prerequisite: Layer 1 READY + Layer 2 READY
 *
 * Cách dùng:
 *   1. Gọi netcore_layer3_test() từ main.c sau netcore_init()
 *   2. T1..T3 không cần cable (test logic nội bộ)
 *   3. T4..T5 cần cable + laptop có IP 192.168.1.1
 */

/* Entry point — gọi từ main.c */
void netcore_layer3_test(void);

/* Từng test riêng lẻ */
int netcore_test_checksum(void);
int netcore_test_arp_build(void);
int netcore_test_icmp_build(void);
int netcore_test_arp_reply(void);
int netcore_test_ping_reply(void);

#endif /* NETCORE_TEST_H */
