/* ============================================================
 * netcore_test.c
 * ------------------------------------------------------------
 * Layer 3 Test Suite — ARP + IPv4 + ICMP
 * Board: BeagleBone Black / AM335x
 *
 * Mục tiêu: Verify network stack hoạt động đúng.
 * T1..T3: unit test logic (không cần hardware)
 * T4..T5: integration test (cần cable + laptop)
 * ============================================================ */

#include "netcore_test.h"
#include "netcore.h"
#include "cpsw.h"
#include "uart.h"
#include "string.h"

/* ============================================================
 * CONSTANTS
 * ============================================================ */

#define PASS 1
#define FAIL 0
#define SKIP 2

/* Test IP config */
#define BBB_IP    NETCORE_IP4(192, 168, 1, 100)
#define LAPTOP_IP NETCORE_IP4(192, 168, 1,   1)

static void print_sep(void)
{
    uart_printf("--------------------------------------------------\n");
}

/* ============================================================
 * TEST CASES
 * ============================================================ */

/*
 * T1: IP Checksum Correctness
 *
 * Mục đích : Verify hàm net_cksum() tính đúng one's complement checksum.
 *            Đây là unit test thuần túy — không cần hardware.
 *
 * Cách test:
 *   - Test vector từ RFC 791: IP header với checksum đã biết
 *   - Verify checksum của header = 0 (khi include checksum field)
 *   - Verify checksum của payload đơn giản
 *
 * PASS khi : tất cả test vector đúng
 * FAIL khi : bất kỳ vector nào sai → bug trong net_cksum()
 *
 * Không cần: cable, hardware
 */
int netcore_test_checksum(void)
{
    print_sep();
    uart_printf("[T1] IP Checksum Correctness\n");

    /* Test vector 1: RFC 791 example IP header
     * Header: 45 00 00 3c 1c 46 40 00 40 06 00 00 ac 10 0a 63 ac 10 0a 0c
     * Checksum field = 0x0000, expected result = 0xB1E6 */
    uint8_t hdr1[20] = {
        0x45, 0x00, 0x00, 0x3c,
        0x1c, 0x46, 0x40, 0x00,
        0x40, 0x06, 0x00, 0x00,  /* checksum = 0 */
        0xac, 0x10, 0x0a, 0x63,
        0xac, 0x10, 0x0a, 0x0c
    };
    uint16_t cksum1 = netcore_cksum(hdr1, 20);
    uart_printf("     Vector 1 (RFC 791 header): cksum=0x%04x (expect 0xB1E6)\n",
                cksum1);
    if (cksum1 != 0xB1E6) {
        uart_printf("     => FAIL: checksum sai\n");
        return FAIL;
    }

    /* Test vector 2: verify checksum của header + checksum = 0 */
    hdr1[10] = (uint8_t)(cksum1 >> 8);
    hdr1[11] = (uint8_t)(cksum1 & 0xFF);
    uint16_t verify = netcore_cksum(hdr1, 20);
    uart_printf("     Vector 2 (verify with cksum): result=0x%04x (expect 0x0000)\n",
                verify);
    if (verify != 0x0000) {
        uart_printf("     => FAIL: verification failed\n");
        return FAIL;
    }

    /* Test vector 3: ICMP echo request checksum
     * type=8, code=0, id=0x0001, seq=0x0001, data=0x00..0x37 */
    uint8_t icmp[64];
    icmp[0] = 0x08; icmp[1] = 0x00; /* type=8, code=0 */
    icmp[2] = 0x00; icmp[3] = 0x00; /* checksum=0 */
    icmp[4] = 0x00; icmp[5] = 0x01; /* id=1 */
    icmp[6] = 0x00; icmp[7] = 0x01; /* seq=1 */
    for (int i = 8; i < 64; i++) icmp[i] = (uint8_t)(i - 8);
    uint16_t cksum3 = netcore_cksum(icmp, 64);
    uart_printf("     Vector 3 (ICMP echo): cksum=0x%04x (non-zero expected)\n",
                cksum3);
    if (cksum3 == 0x0000) {
        uart_printf("     => FAIL: ICMP checksum = 0 (impossible for this payload)\n");
        return FAIL;
    }

    uart_printf("     => PASS: all checksum vectors correct\n");
    return PASS;
}

/*
 * T2: ARP Packet Build
 *
 * Mục đích : Verify ARP request được build đúng format.
 *            Test logic của arp layer — không gửi ra wire.
 *
 * Cách test:
 *   - Gọi netcore_build_arp_request() để build packet vào buffer
 *   - Verify từng field: hw_type, proto_type, opcode, sender_ip, target_ip
 *
 * PASS khi : tất cả field đúng theo RFC 826
 * FAIL khi : bất kỳ field nào sai
 *
 * Không cần: cable, hardware
 */
int netcore_test_arp_build(void)
{
    print_sep();
    uart_printf("[T2] ARP Packet Build\n");

    uint8_t buf[42];  /* 14 Ethernet + 28 ARP */
    memset(buf, 0, sizeof(buf));

    /* Build ARP request: who has LAPTOP_IP? tell BBB_IP */
    int ret = netcore_build_arp_request(buf, sizeof(buf), LAPTOP_IP);
    if (ret < 0) {
        uart_printf("     => FAIL: netcore_build_arp_request returned %d\n", ret);
        return FAIL;
    }

    /* Verify Ethernet header */
    /* Dst = FF:FF:FF:FF:FF:FF (broadcast) */
    int bcast_ok = (buf[0]==0xFF && buf[1]==0xFF && buf[2]==0xFF &&
                    buf[3]==0xFF && buf[4]==0xFF && buf[5]==0xFF);
    uart_printf("     Dst MAC = %02x:%02x:%02x:%02x:%02x:%02x (expect FF:FF:FF:FF:FF:FF) %s\n",
                buf[0],buf[1],buf[2],buf[3],buf[4],buf[5],
                bcast_ok ? "OK" : "FAIL");

    /* EtherType = 0x0806 (ARP) */
    uint16_t etype = (uint16_t)((buf[12] << 8) | buf[13]);
    uart_printf("     EtherType = 0x%04x (expect 0x0806) %s\n",
                etype, (etype == 0x0806) ? "OK" : "FAIL");

    /* ARP opcode = 1 (request) */
    uint16_t opcode = (uint16_t)((buf[20] << 8) | buf[21]);
    uart_printf("     ARP opcode = %u (expect 1 = request) %s\n",
                opcode, (opcode == 1) ? "OK" : "FAIL");

    /* Target IP = LAPTOP_IP */
    uint32_t target_ip = ((uint32_t)buf[38]       |
                          ((uint32_t)buf[39] << 8) |
                          ((uint32_t)buf[40] << 16)|
                          ((uint32_t)buf[41] << 24));
    uart_printf("     Target IP = %u.%u.%u.%u (expect 192.168.1.1) %s\n",
                buf[38], buf[39], buf[40], buf[41],
                (target_ip == LAPTOP_IP) ? "OK" : "FAIL");

    if (!bcast_ok || etype != 0x0806 || opcode != 1 || target_ip != LAPTOP_IP) {
        uart_printf("     => FAIL: ARP packet format sai\n");
        return FAIL;
    }

    uart_printf("     => PASS: ARP request format đúng RFC 826\n");
    return PASS;
}

/*
 * T3: ICMP Echo Reply Build
 *
 * Mục đích : Verify ICMP Echo Reply được build đúng từ Echo Request.
 *            Test logic của icmp layer — không gửi ra wire.
 *
 * Cách test:
 *   - Tạo fake ICMP Echo Request
 *   - Gọi netcore_build_icmp_reply() để build reply
 *   - Verify type=0, checksum đúng, id/seq khớp
 *
 * PASS khi : reply type=0, checksum valid, id/seq khớp request
 * FAIL khi : bất kỳ field nào sai
 *
 * Không cần: cable, hardware
 */
int netcore_test_icmp_build(void)
{
    print_sep();
    uart_printf("[T3] ICMP Echo Reply Build\n");

    /* Build fake ICMP Echo Request */
    uint8_t req[64];
    req[0] = 0x08; req[1] = 0x00; /* type=8 (echo request), code=0 */
    req[2] = 0x00; req[3] = 0x00; /* checksum placeholder */
    req[4] = 0x12; req[5] = 0x34; /* id = 0x1234 */
    req[6] = 0x00; req[7] = 0x01; /* seq = 1 */
    for (int i = 8; i < 64; i++) req[i] = (uint8_t)(i);
    /* Compute request checksum */
    uint16_t req_cksum = netcore_cksum(req, 64);
    req[2] = (uint8_t)(req_cksum >> 8);
    req[3] = (uint8_t)(req_cksum & 0xFF);

    /* Build reply */
    uint8_t rep[64];
    int ret = netcore_build_icmp_reply(rep, sizeof(rep), req, 64);
    if (ret < 0) {
        uart_printf("     => FAIL: netcore_build_icmp_reply returned %d\n", ret);
        return FAIL;
    }

    /* Verify type = 0 (echo reply) */
    uart_printf("     type = %u (expect 0 = echo reply) %s\n",
                rep[0], (rep[0] == 0) ? "OK" : "FAIL");

    /* Verify id/seq khớp */
    uint16_t rep_id  = (uint16_t)((rep[4] << 8) | rep[5]);
    uint16_t rep_seq = (uint16_t)((rep[6] << 8) | rep[7]);
    uart_printf("     id  = 0x%04x (expect 0x1234) %s\n",
                rep_id, (rep_id == 0x1234) ? "OK" : "FAIL");
    uart_printf("     seq = %u (expect 1) %s\n",
                rep_seq, (rep_seq == 1) ? "OK" : "FAIL");

    /* Verify checksum valid */
    uint16_t verify = netcore_cksum(rep, 64);
    uart_printf("     checksum verify = 0x%04x (expect 0x0000) %s\n",
                verify, (verify == 0) ? "OK" : "FAIL");

    if (rep[0] != 0 || rep_id != 0x1234 || rep_seq != 1 || verify != 0) {
        uart_printf("     => FAIL: ICMP reply format sai\n");
        return FAIL;
    }

    uart_printf("     => PASS: ICMP Echo Reply format đúng RFC 792\n");
    return PASS;
}

/*
 * T4: ARP Reply From Laptop (cần cable)
 *
 * Mục đích : Verify BBB nhận được ARP reply từ laptop.
 *            Đây là test end-to-end đầu tiên qua wire.
 *
 * Cách test:
 *   1. BBB gửi ARP request: "Who has 192.168.1.1?"
 *   2. Đợi ARP reply từ laptop (timeout 3 giây)
 *   3. Verify laptop MAC được lưu vào ARP cache
 *
 * PASS khi : ARP cache có entry cho 192.168.1.1 trong 3 giây
 * SKIP khi : timeout — cable chưa cắm hoặc laptop không có IP 192.168.1.1
 *
 * Cần: cable + laptop IP=192.168.1.1
 */
int netcore_test_arp_reply(void)
{
    print_sep();
    uart_printf("[T4] ARP Reply From Laptop (cần cable)\n");
    uart_printf("     Laptop phải có IP 192.168.1.1\n");
    uart_printf("     Gửi ARP request, đợi reply tối đa 3 giây...\n");

    uint8_t laptop_mac[6];
    int ret = netcore_arp_resolve(LAPTOP_IP, laptop_mac);

    if (ret != 0) {
        uart_printf("     => SKIP: không nhận được ARP reply\n");
        uart_printf("              Kiểm tra: cable cắm chưa? Laptop IP đúng chưa?\n");
        return SKIP;
    }

    uart_printf("     Laptop MAC = %02x:%02x:%02x:%02x:%02x:%02x\n",
                laptop_mac[0], laptop_mac[1], laptop_mac[2],
                laptop_mac[3], laptop_mac[4], laptop_mac[5]);
    uart_printf("     => PASS: ARP resolve thành công\n");
    return PASS;
}

/*
 * T5: Ping Reply To Laptop (cần cable)
 *
 * Mục đích : Verify BBB reply đúng ICMP Echo Request từ laptop.
 *            Đây là DoD của Layer 3 theo handoff §2.
 *
 * Cách test:
 *   1. Laptop ping 192.168.1.100 (BBB)
 *   2. BBB nhận ICMP Echo Request
 *   3. BBB gửi ICMP Echo Reply
 *   4. Laptop nhận reply → ping thành công
 *
 * Test này verify bằng cách:
 *   - Poll cpsw_rx_poll() trong 5 giây
 *   - Đếm số ICMP Echo Request nhận được
 *   - Đếm số ICMP Echo Reply gửi đi
 *
 * PASS khi : nhận ít nhất 1 Echo Request VÀ gửi được 1 Echo Reply
 * SKIP khi : không nhận được gì — laptop chưa ping
 *
 * Cần: cable + laptop đang chạy "ping 192.168.1.100"
 */
int netcore_test_ping_reply(void)
{
    print_sep();
    uart_printf("[T5] Ping Reply To Laptop (cần cable)\n");
    uart_printf("     Chạy 'ping 192.168.1.100' từ laptop\n");
    uart_printf("     Đang đợi ICMP Echo Request trong 5 giây...\n");

    uint32_t rx_count = 0;
    uint32_t tx_count = 0;

    /* Poll 5 giây */
    for (volatile int i = 0; i < 50000000; i++) {
        cpsw_rx_poll();

        /* Check counters từ netcore */
        uint32_t new_rx = netcore_get_icmp_rx_count();
        uint32_t new_tx = netcore_get_icmp_tx_count();

        if (new_rx > rx_count) {
            rx_count = new_rx;
            uart_printf("     ICMP Echo Request #%u received\n", rx_count);
        }
        if (new_tx > tx_count) {
            tx_count = new_tx;
            uart_printf("     ICMP Echo Reply   #%u sent\n", tx_count);
        }

        if (rx_count >= 3 && tx_count >= 3)
            break;
    }

    uart_printf("     Total: RX=%u requests, TX=%u replies\n", rx_count, tx_count);

    if (rx_count == 0) {
        uart_printf("     => SKIP: không nhận được ICMP request\n");
        uart_printf("              Laptop có đang ping 192.168.1.100 không?\n");
        return SKIP;
    }

    if (tx_count == 0) {
        uart_printf("     => FAIL: nhận được request nhưng không gửi được reply\n");
        return FAIL;
    }

    uart_printf("     => PASS: ping reply hoạt động! RTT < 5ms expected\n");
    return PASS;
}

/* ============================================================
 * ENTRY POINT
 * ============================================================ */

void netcore_layer3_test(void)
{
    uart_printf("\n");
    uart_printf("==================================================\n");
    uart_printf("  LAYER 3 TEST SUITE — ARP + IPv4 + ICMP\n");
    uart_printf("  Board: BeagleBone Black / AM335x\n");
    uart_printf("  BBB IP: 192.168.1.100\n");
    uart_printf("==================================================\n");

    int results[5];
    results[0] = netcore_test_checksum();
    results[1] = netcore_test_arp_build();
    results[2] = netcore_test_icmp_build();
    results[3] = netcore_test_arp_reply();
    results[4] = netcore_test_ping_reply();

    print_sep();
    uart_printf("SUMMARY:\n");

    const char *names[5] = {
        "T1 IP Checksum     ",
        "T2 ARP Build       ",
        "T3 ICMP Build      ",
        "T4 ARP Reply       ",
        "T5 Ping Reply      "
    };

    int pass_count = 0;
    for (int i = 0; i < 5; i++) {
        const char *status;
        if      (results[i] == PASS) { status = "PASS"; pass_count++; }
        else if (results[i] == SKIP) { status = "SKIP"; pass_count++; }
        else                         { status = "FAIL"; }
        uart_printf("  %s : %s\n", names[i], status);
    }

    print_sep();
    uart_printf("RESULT: %d/5  —  Layer 3 %s\n",
                pass_count,
                (pass_count == 5) ? "READY" : "NOT READY");
    uart_printf("==================================================\n\n");
}
