#ifndef CPSW_TEST_H
#define CPSW_TEST_H

/*
 * Layer 2 — CPSW MAC + CPDMA Test Suite
 * Board  : BeagleBone Black (AM335x)
 * Driver : cpsw.c
 *
 * Prerequisite: Layer 1 READY (mdio_layer1_test() PASS)
 *
 * Cách dùng:
 *   1. Gọi cpsw_layer2_test() từ main.c sau cpsw_init()
 *   2. T1..T4 không cần cable (test nội bộ)
 *   3. T5 cần cable cắm vào laptop/switch
 */

/* Entry point — gọi từ main.c */
void cpsw_layer2_test(void);

/* Từng test riêng lẻ */
int cpsw_test_clock(void);
int cpsw_test_ss_reset(void);
int cpsw_test_cpdma_reset(void);
int cpsw_test_mac_loopback(void);
int cpsw_test_rx_stats(void);

#endif /* CPSW_TEST_H */
