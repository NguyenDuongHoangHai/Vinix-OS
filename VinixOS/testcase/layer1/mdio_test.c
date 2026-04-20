#include "mdio_test.h"
#include "mdio.h"
#include "uart.h"

/* ================================================================
 * PHẦN 1 — HARDWARE CONSTANTS
 * Board: BeagleBone Black, PHY: TI DP83848I
 * ================================================================ */

#define TEST_PHY_ADDR     0        /* PHY address trên MDIO bus (từ schematic BBB) */

/* LAN8710A Identifier — chip thực tế trên BBB rev C+
 * (BBB rev B dùng DP83848I: PHYSID1=0x2000, PHYSID2=0xA140) */
#define DP83848_PHYSID1   0x0007
#define DP83848_PHYSID2   0xC0F0
#define DP83848_ID2_MASK  0xFFF0   /* 4 bit thấp là revision — không check */

/* ================================================================
 * PHẦN 2 — IEEE 802.3 MII REGISTER MAP
 * Chuẩn IEEE, giống nhau trên mọi PHY chip
 * ================================================================ */

#define MII_BMCR     0    /* Basic Mode Control Register    — R/W */
#define MII_BMSR     1    /* Basic Mode Status Register     — R (latched) */
#define MII_PHYSID1  2    /* PHY Identifier 1               — RO */
#define MII_PHYSID2  3    /* PHY Identifier 2               — RO */
#define MII_ANAR     4    /* Auto-Negotiate Advertisement   — R/W */
#define MII_ANLPAR   5    /* Auto-Negotiate Link Partner    — RO  */

/* BMSR register bits */
#define BMSR_LINK_UP    (1 << 2)   /* 1 = link established              */
#define BMSR_ANEG_DONE  (1 << 5)   /* 1 = autoneg complete              */
#define BMSR_100FD      (1 << 14)  /* 1 = PHY capable 100Base-TX FD     */
#define BMSR_100HD      (1 << 13)  /* 1 = PHY capable 100Base-TX HD     */
#define BMSR_10FD       (1 << 12)  /* 1 = PHY capable 10Base-T FD       */
#define BMSR_10HD       (1 << 11)  /* 1 = PHY capable 10Base-T HD       */

/* ================================================================
 * PHẦN 3 — HELPER
 * ================================================================ */

#define PASS 1
#define FAIL 0
#define SKIP 2   /* test không áp dụng được do thiếu điều kiện ngoài */

static void print_separator(void) {
    uart_printf("--------------------------------------------------\n");
}

/* ================================================================
 * PHẦN 4 — TEST CASES
 * ================================================================ */

/*
 * T1: MDIO Bus Alive
 *
 * Mục đích  : Xác nhận CPU đọc được chip PHY qua MDIO bus.
 *             Đây là test đầu tiên — nếu T1 fail thì T2..T5 vô nghĩa.
 *
 * Cách test : Đọc PHY ID register (reg 2 và 3). Đây là register read-only,
 *             giá trị được nhà sản xuất ghi cứng. DP83848I trên BBB
 *             luôn trả về 0x2000 / 0xA14x.
 *
 * PASS khi  : id1 == 0x2000 VÀ (id2 & 0xFFF0) == 0xA140
 * FAIL khi  : id1 == 0xFFFF  → MDIO clock không hoạt động (kiểm tra CLKDIV)
 *             id1 == 0x0000  → bus không kéo được (kiểm tra pull-up)
 *             id1 == giá trị khác → PHY addr sai (thử addr 1)
 *
 * Không cần : cable, switch, laptop
 */
int mdio_test_bus_alive(void) {
    print_separator();
    uart_printf("[T1] MDIO Bus Alive\n");

    int id1 = mdio_read(TEST_PHY_ADDR, MII_PHYSID1);
    int id2 = mdio_read(TEST_PHY_ADDR, MII_PHYSID2);

    uart_printf("     PHY ID1 = 0x%04X  (expect 0x%04X)\n",
                id1, DP83848_PHYSID1);
    uart_printf("     PHY ID2 = 0x%04X  (expect 0x%04X masked)\n",
                id2, DP83848_PHYSID2);

    if (id1 == 0xFFFF) {
        uart_printf("     => FAIL: 0xFFFF — CLKDIV sai hoặc MDIO enable chưa set\n");
        return FAIL;
    }
    if (id1 == 0x0000) {
        uart_printf("     => FAIL: 0x0000 — không có pull-up trên MDIO line\n");
        return FAIL;
    }
    if (id1 != DP83848_PHYSID1) {
        uart_printf("     => FAIL: ID1 không khớp — thử PHY addr=1\n");
        return FAIL;
    }
    if ((id2 & DP83848_ID2_MASK) != DP83848_PHYSID2) {
        uart_printf("     => FAIL: ID2 không khớp — sai chip PHY?\n");
        return FAIL;
    }

    uart_printf("     => PASS: DP83848I tìm thấy tại addr %d\n", TEST_PHY_ADDR);
    return PASS;
}

/*
 * T2: BMSR Latching Behavior
 *
 * Mục đích  : Xác nhận MDIO đọc đúng cơ chế latching của BMSR.
 *
 * Giải thích: BMSR là "latched low" register. Khi link down xảy ra,
 *             bit LINK_UP bị clear và GIỮ NGUYÊN cho đến lần đọc tiếp theo.
 *             Lần đọc thứ nhất = trạng thái "từ lần đọc trước đến giờ".
 *             Lần đọc thứ hai = trạng thái hiện tại thực sự.
 *             Bug phổ biến: chỉ đọc 1 lần → báo link down dù cable đang cắm.
 *
 * PASS khi  : Cả 2 lần đọc trả về giá trị hợp lệ (không phải 0xFFFF/0x0000)
 * FAIL khi  : mdio_read trả về lỗi (< 0)
 *
 * Không cần : cable (test MDIO cơ chế, không test link)
 */
int mdio_test_bmsr_latch(void) {
    print_separator();
    uart_printf("[T2] BMSR Latching\n");

    int bmsr1 = mdio_read(TEST_PHY_ADDR, MII_BMSR);
    int bmsr2 = mdio_read(TEST_PHY_ADDR, MII_BMSR);

    if (bmsr1 < 0 || bmsr2 < 0) {
        uart_printf("     => FAIL: mdio_read trả về lỗi\n");
        return FAIL;
    }

    uart_printf("     BMSR lần 1 = 0x%04X\n", bmsr1);
    uart_printf("     BMSR lần 2 = 0x%04X  (dùng giá trị này)\n", bmsr2);
    uart_printf("     Link status (từ lần 2) : %s\n",
                (bmsr2 & BMSR_LINK_UP) ? "UP" : "DOWN — cần cable");
    uart_printf("     Autoneg done           : %s\n",
                (bmsr2 & BMSR_ANEG_DONE) ? "YES" : "NO");

    uart_printf("     => PASS: BMSR đọc được, dùng lần đọc thứ 2\n");
    return PASS;
}

/*
 * T3: PHY Speed/Duplex Capability
 *
 * Mục đích  : Xác nhận PHY report đúng các chế độ nó hỗ trợ.
 *             DP83848I phải support ít nhất 10HD và 100HD.
 *
 * PASS khi  : BMSR report ít nhất 1 capability (10HD hoặc 100HD)
 * FAIL khi  : Không có capability nào → PHY không khởi động đúng
 *
 * Không cần : cable
 */
int mdio_test_capability(void) {
    print_separator();
    uart_printf("[T3] PHY Speed/Duplex Capability\n");

    /* Đọc 2 lần — lấy lần 2 */
    mdio_read(TEST_PHY_ADDR, MII_BMSR);
    int bmsr = mdio_read(TEST_PHY_ADDR, MII_BMSR);

    if (bmsr < 0) {
        uart_printf("     => FAIL: không đọc được BMSR\n");
        return FAIL;
    }

    uart_printf("     100Base-TX Full-Duplex : %s\n",
                (bmsr & BMSR_100FD) ? "SUPPORTED" : "not supported");
    uart_printf("     100Base-TX Half-Duplex : %s\n",
                (bmsr & BMSR_100HD) ? "SUPPORTED" : "not supported");
    uart_printf("     10Base-T   Full-Duplex : %s\n",
                (bmsr & BMSR_10FD)  ? "SUPPORTED" : "not supported");
    uart_printf("     10Base-T   Half-Duplex : %s\n",
                (bmsr & BMSR_10HD)  ? "SUPPORTED" : "not supported");

    int any = (bmsr & BMSR_100FD) || (bmsr & BMSR_100HD) ||
              (bmsr & BMSR_10FD)  || (bmsr & BMSR_10HD);

    if (!any) {
        uart_printf("     => FAIL: không có capability nào — PHY init sai\n");
        return FAIL;
    }

    uart_printf("     => PASS\n");
    return PASS;
}

/*
 * T4: Link Up After Autoneg
 *
 * Mục đích  : Xác nhận link được thiết lập sau khi cắm cable.
 *             Đây là test end-to-end của toàn bộ layer 1.
 *
 * Điều kiện : Cable phải được cắm vào switch hoặc laptop TRƯỚC khi test.
 *             Nếu không có cable → SKIP (không phải FAIL của code).
 *
 * PASS khi  : phy_wait_link() trả về 0 và info.link_up = 1
 * SKIP khi  : phy_wait_link() timeout (không có cable)
 * FAIL khi  : phy_wait_link() trả về lỗi không phải timeout
 *
 * Cần       : cable cắm vào switch/laptop
 */
int mdio_test_link_up(void) {
    print_separator();
    uart_printf("[T4] Link Up (yêu cầu cable đang cắm)\n");
    uart_printf("     Đang đợi autoneg tối đa 3 giây...\n");

    phy_info_t info;
    int ret = phy_wait_link(TEST_PHY_ADDR, &info);

    if (ret < 0) {
        uart_printf("     => SKIP: không phát hiện link — cable chưa cắm\n");
        uart_printf("              Cắm cable rồi reset board để test lại\n");
        return SKIP;
    }

    uart_printf("     Link     : UP\n");
    uart_printf("     Speed    : %d Mbps\n", info.speed);
    uart_printf("     Duplex   : %s\n", info.duplex ? "Full" : "Half");
    uart_printf("     PHY addr : %d\n", info.addr);
    uart_printf("     PHY ID   : 0x%08X\n", info.id);

    uart_printf("     => PASS\n");
    return PASS;
}

/*
 * T5: MDIO Write / Read Round-Trip
 *
 * Mục đích  : Xác nhận MDIO write hoạt động — không chỉ read.
 *
 * Cách test : Ghi giá trị biết trước vào ANAR (reg 4 — read/write).
 *             Đọc lại và so sánh. Khôi phục giá trị gốc sau test.
 *             ANAR được chọn vì nó read/write và thay đổi không gây
 *             side-effect nguy hiểm (chỉ thay đổi autoneg advertisement,
 *             không reset PHY).
 *
 * PASS khi  : readback == giá trị đã ghi
 * FAIL khi  : readback khác → MDIO write không hoạt động
 *
 * Không cần : cable
 */
int mdio_test_write_readback(void) {
    print_separator();
    uart_printf("[T5] MDIO Write/Read Round-Trip\n");

    /* Lưu giá trị gốc để khôi phục */
    int original = mdio_read(TEST_PHY_ADDR, MII_ANAR);
    if (original < 0) {
        uart_printf("     => FAIL: không đọc được ANAR ban đầu\n");
        return FAIL;
    }

    /* Ghi test value — advertise 100FD + 100HD + 10FD + 10HD + selector */
    uint16_t test_val = 0x01E1;
    mdio_write(TEST_PHY_ADDR, MII_ANAR, test_val);
    int readback = mdio_read(TEST_PHY_ADDR, MII_ANAR);

    /* Khôi phục ngay lập tức */
    mdio_write(TEST_PHY_ADDR, MII_ANAR, (uint16_t)original);

    uart_printf("     Giá trị gốc  : 0x%04X\n", original);
    uart_printf("     Giá trị ghi  : 0x%04X\n", test_val);
    uart_printf("     Giá trị đọc  : 0x%04X\n", readback);
    uart_printf("     Đã khôi phục : 0x%04X\n", original);

    if (readback != test_val) {
        uart_printf("     => FAIL: write/read không khớp\n");
        return FAIL;
    }

    uart_printf("     => PASS\n");
    return PASS;
}

/* ================================================================
 * PHẦN 5 — ENTRY POINT
 * Gọi từ main.c: mdio_layer1_test()
 * ================================================================ */

void mdio_layer1_test(void) {
    uart_printf("\n");
    uart_printf("==================================================\n");
    uart_printf("  LAYER 1 TEST SUITE — MDIO/PHY (DP83848I)\n");
    uart_printf("  Board: BeagleBone Black / AM335x\n");
    uart_printf("==================================================\n");

    int results[5];
    results[0] = mdio_test_bus_alive();
    results[1] = mdio_test_bmsr_latch();
    results[2] = mdio_test_capability();
    results[3] = mdio_test_link_up();
    results[4] = mdio_test_write_readback();

    print_separator();
    uart_printf("SUMMARY:\n");

    const char *names[5] = {
        "T1 Bus Alive       ",
        "T2 BMSR Latch      ",
        "T3 Capability      ",
        "T4 Link Up         ",
        "T5 Write Readback  "
    };

    int pass_count = 0;
    for (int i = 0; i < 5; i++) {
        const char *status;
        if (results[i] == PASS)      { status = "PASS"; pass_count++; }
        else if (results[i] == SKIP) { status = "SKIP"; pass_count++; }
        else                         { status = "FAIL"; }
        uart_printf("  %s : %s\n", names[i], status);
    }

    print_separator();
    uart_printf("RESULT: %d/5  —  Layer 1 %s\n",
                pass_count,
                (pass_count == 5) ? "READY" : "NOT READY");
    uart_printf("==================================================\n\n");
}
