#ifndef MDIO_TEST_H
#define MDIO_TEST_H

/*
 * Layer 1 — MDIO/PHY Test Suite
 * Board  : BeagleBone Black (AM335x)
 * PHY    : TI DP83848I
 * UART   : 115200 baud qua USB-UART dongle cắm vào J1 header
 *
 * Cách dùng:
 *   1. Gọi mdio_layer1_test() từ main.c sau khi mdio_init()
 *   2. Mở picocom/PuTTY trước khi cấp nguồn board
 *   3. Đọc kết quả PASS/FAIL từ serial terminal
 */

/* Kết quả từng test */
typedef struct {
    int t1_bus_alive;       /* MDIO bus đọc được PHY không          */
    int t2_bmsr_latch;      /* BMSR latching hoạt động đúng không   */
    int t3_capability;      /* PHY report đúng speed/duplex không   */
    int t4_link_up;         /* Link up sau autoneg (cần cable)      */
    int t5_write_readback;  /* MDIO write rồi đọc lại có khớp không */
    int total_pass;         /* Tổng số test PASS                    */
} layer1_test_result_t;

/* Entry point — gọi từ main.c */
void mdio_layer1_test(void);

/* Từng test riêng lẻ — dùng khi debug 1 vấn đề cụ thể */
int mdio_test_bus_alive(void);
int mdio_test_bmsr_latch(void);
int mdio_test_capability(void);
int mdio_test_link_up(void);
int mdio_test_write_readback(void);

#endif /* MDIO_TEST_H */
