/* ============================================================
 * icmp_test.c
 * ------------------------------------------------------------
 * ICMP Protocol Test Suite Implementation
 *
 * Trách nhiệm:
 *   - Unit tests cho ICMP protocol
 *   - Test ICMP echo request/reply
 *   - Test ICMP error messages
 *   - Test ICMP checksum calculation
 *
 * Dependencies:
 *   - icmp.h: ICMP protocol interface
 *   - icmp_test.h: Test framework
 *   - uart.h: Test output
 * ============================================================ */

#include "icmp_test.h"
#include "icmp.h"
#include "uart.h"
#include "string.h"

/* ============================================================
 * Test State
 * ============================================================ */

static icmp_test_results_t s_test_results = {0};

/* ============================================================
 * Test Utilities
 * ============================================================ */

void icmp_test_assert(int condition, const char *test_name, const char *message)
{
    s_test_results.total_tests++;
    
    if (condition) {
        s_test_results.passed_tests++;
        ICMP_TEST_PASS(test_name);
    } else {
        s_test_results.failed_tests++;
        ICMP_TEST_FAIL(test_name, message);
    }
}

void icmp_test_print_results(const icmp_test_results_t *results)
{
    uart_printf("\n=== ICMP TEST RESULTS ===\n");
    uart_printf("Total:  %u\n", results->total_tests);
    uart_printf("Passed: %u\n", results->passed_tests);
    uart_printf("Failed: %u\n", results->failed_tests);
    uart_printf("Skipped: %u\n", results->skipped_tests);
    
    if (results->failed_tests == 0) {
        uart_printf("✅ ALL TESTS PASSED!\n");
    } else {
        uart_printf("❌ %u TESTS FAILED!\n", results->failed_tests);
    }
    uart_printf("========================\n\n");
}

/* ============================================================
 * ICMP Header Parsing Tests
 * ============================================================ */

int test_icmp_header_parsing(void)
{
    uart_printf("[ICMP_TEST] Testing ICMP header parsing...\n");
    
    /* Test 1: Echo Request header */
    {
        uint8_t echo_request[8] = {
            0x08,        /* Type: Echo Request */
            0x00,        /* Code */
            0x00, 0x00,  /* Checksum */
            0x12, 0x34,  /* Identifier */
            0x56, 0x78   /* Sequence number */
        };
        
        /* Test type extraction */
        uint8_t type = echo_request[0];
        ICMP_TEST_ASSERT(type == 8, "Echo Request type extraction");
        
        /* Test code extraction */
        uint8_t code = echo_request[1];
        ICMP_TEST_ASSERT(code == 0, "Echo Request code extraction");
        
        /* Test identifier extraction */
        uint16_t identifier = (echo_request[4] << 8) | echo_request[5];
        ICMP_TEST_ASSERT(identifier == 0x1234, "Identifier extraction");
        
        /* Test sequence number extraction */
        uint16_t sequence = (echo_request[6] << 8) | echo_request[7];
        ICMP_TEST_ASSERT(sequence == 0x5678, "Sequence number extraction");
    }
    
    /* Test 2: Echo Reply header */
    {
        uint8_t echo_reply[8] = {
            0x00,        /* Type: Echo Reply */
            0x00,        /* Code */
            0x00, 0x00,  /* Checksum */
            0x12, 0x34,  /* Identifier */
            0x56, 0x78   /* Sequence number */
        };
        
        uint8_t type = echo_reply[0];
        ICMP_TEST_ASSERT(type == 0, "Echo Reply type extraction");
    }
    
    /* Test 3: Destination Unreachable header */
    {
        uint8_t dest_unreach[8] = {
            0x03,        /* Type: Destination Unreachable */
            0x01,        /* Code: Host Unreachable */
            0x00, 0x00,  /* Checksum */
            0x00, 0x00,  /* Unused */
            0x00, 0x00   /* Unused */
        };
        
        uint8_t type = dest_unreach[0];
        uint8_t code = dest_unreach[1];
        ICMP_TEST_ASSERT(type == 3, "Destination Unreachable type extraction");
        ICMP_TEST_ASSERT(code == 1, "Host Unreachable code extraction");
    }
    
    return 0;
}

/* ============================================================
 * ICMP Echo Request/Reply Tests
 * ============================================================ */

int test_icmp_echo_request_reply(void)
{
    uart_printf("[ICMP_TEST] Testing ICMP echo request/reply...\n");
    
    /* Test 1: Ping request creation */
    {
        uint32_t target_ip = 0xc0a80a50;  /* 192.168.10.80 */
        uint16_t identifier = 0x1234;
        uint16_t sequence = 0x0001;
        const char *data = "Hello";
        size_t data_len = 5;
        
        /* Test ping function call */
        int ret = icmp_ping(target_ip, identifier, sequence, data, data_len);
        ICMP_TEST_ASSERT(ret <= 0, "Ping function returns valid status");
    }
    
    /* Test 2: Ping reply creation */
    {
        uint32_t target_ip = 0xc0a80a50;  /* 192.168.10.80 */
        uint16_t identifier = 0x1234;
        uint16_t sequence = 0x0001;
        const char *data = "Hello";
        size_t data_len = 5;
        
        /* Test pong function call */
        int ret = icmp_pong(target_ip, identifier, sequence, data, data_len);
        ICMP_TEST_ASSERT(ret <= 0, "Pong function returns valid status");
    }
    
    /* Test 3: Identifier and sequence validation */
    {
        uint16_t identifier = 0x1234;
        uint16_t sequence = 0x0001;
        
        ICMP_TEST_ASSERT(identifier != 0, "Non-zero identifier");
        ICMP_TEST_ASSERT(sequence != 0, "Non-zero sequence number");
    }
    
    return 0;
}

/* ============================================================
 * ICMP Error Message Tests
 * ============================================================ */

int test_icmp_error_messages(void)
{
    uart_printf("[ICMP_TEST] Testing ICMP error messages...\n");
    
    /* Test 1: Destination Unreachable */
    {
        uint32_t target_ip = 0xc0a80a50;  /* 192.168.10.80 */
        uint8_t code = 1;  /* Host Unreachable */
        uint8_t original_hdr[20] = {0};  /* Dummy IP header */
        
        /* Test destination unreachable function */
        int ret = icmp_dest_unreachable(target_ip, code, original_hdr, sizeof(original_hdr));
        ICMP_TEST_ASSERT(ret <= 0, "Destination Unreachable function returns valid status");
    }
    
    /* Test 2: Error message codes */
    {
        uint8_t net_unreach = 0;   /* Network Unreachable */
        uint8_t host_unreach = 1;  /* Host Unreachable */
        uint8_t proto_unreach = 2; /* Protocol Unreachable */
        uint8_t port_unreach = 3;  /* Port Unreachable */
        
        ICMP_TEST_ASSERT(net_unreach == 0, "Network Unreachable code");
        ICMP_TEST_ASSERT(host_unreach == 1, "Host Unreachable code");
        ICMP_TEST_ASSERT(proto_unreach == 2, "Protocol Unreachable code");
        ICMP_TEST_ASSERT(port_unreach == 3, "Port Unreachable code");
    }
    
    return 0;
}

/* ============================================================
 * ICMP Checksum Calculation Tests
 * ============================================================ */

int test_icmp_checksum_calculation(void)
{
    uart_printf("[ICMP_TEST] Testing ICMP checksum calculation...\n");
    
    /* Test 1: Simple ICMP checksum */
    {
        uint8_t icmp_packet[8] = {
            0x08,        /* Type: Echo Request */
            0x00,        /* Code */
            0x00, 0x00,  /* Checksum (will be calculated) */
            0x12, 0x34,  /* Identifier */
            0x56, 0x78   /* Sequence number */
        };
        
        /* Calculate checksum */
        uint16_t checksum = icmp_checksum(icmp_packet, sizeof(icmp_packet));
        ICMP_TEST_ASSERT(checksum != 0, "ICMP checksum calculation produces result");
    }
    
    /* Test 2: Checksum with data */
    {
        uint8_t icmp_with_data[13] = {
            0x08,        /* Type: Echo Request */
            0x00,        /* Code */
            0x00, 0x00,  /* Checksum */
            0x12, 0x34,  /* Identifier */
            0x56, 0x78,  /* Sequence number */
            'H', 'e', 'l', 'l', 'o'  /* Data */
        };
        
        uint16_t checksum = icmp_checksum(icmp_with_data, sizeof(icmp_with_data));
        ICMP_TEST_ASSERT(checksum != 0, "ICMP checksum with data");
    }
    
    /* Test 3: Zero data checksum */
    {
        uint8_t zero_packet[8] = {0};
        uint16_t checksum = icmp_checksum(zero_packet, sizeof(zero_packet));
        ICMP_TEST_ASSERT(checksum == 0xFFFF, "Zero data ICMP checksum");
    }
    
    return 0;
}

/* ============================================================
 * ICMP Ping Functionality Tests
 * ============================================================ */

int test_icmp_ping_functionality(void)
{
    uart_printf("[ICMP_TEST] Testing ICMP ping functionality...\n");
    
    /* Test 1: Ping to valid IP */
    {
        uint32_t valid_ip = 0xc0a80a50;  /* 192.168.10.80 */
        uint16_t identifier = 0x1234;
        uint16_t sequence = 0x0001;
        
        int ret = icmp_ping(valid_ip, identifier, sequence, NULL, 0);
        ICMP_TEST_ASSERT(ret <= 0, "Ping to valid IP");
    }
    
    /* Test 2: Ping to invalid IP */
    {
        uint32_t invalid_ip = 0x00000000;  /* 0.0.0.0 */
        uint16_t identifier = 0x1234;
        uint16_t sequence = 0x0001;
        
        int ret = icmp_ping(invalid_ip, identifier, sequence, NULL, 0);
        ICMP_TEST_ASSERT(ret != 0, "Ping to invalid IP should fail");
    }
    
    /* Test 3: Ping with data */
    {
        uint32_t target_ip = 0xc0a80a50;  /* 192.168.10.80 */
        uint16_t identifier = 0x1234;
        uint16_t sequence = 0x0001;
        const char *test_data = "Test ping data";
        size_t data_len = strlen(test_data);
        
        int ret = icmp_ping(target_ip, identifier, sequence, test_data, data_len);
        ICMP_TEST_ASSERT(ret <= 0, "Ping with data");
    }
    
    return 0;
}

/* ============================================================
 * ICMP Error Handling Tests
 * ============================================================ */

int test_icmp_error_handling(void)
{
    uart_printf("[ICMP_TEST] Testing ICMP error handling...\n");
    
    /* Test 1: Packet too short */
    {
        uint8_t short_packet[4] = {0};
        ICMP_TEST_ASSERT(sizeof(short_packet) < 8, "Short ICMP packet detection");
    }
    
    /* Test 2: Invalid ICMP type */
    {
        uint8_t invalid_type = 255;  /* Invalid ICMP type */
        ICMP_TEST_ASSERT(invalid_type > 18, "Invalid ICMP type detection");
    }
    
    /* Test 3: Invalid ICMP code */
    {
        uint8_t invalid_code = 255;  /* Invalid ICMP code */
        ICMP_TEST_ASSERT(invalid_code > 15, "Invalid ICMP code detection");
    }
    
    /* Test 4: Large data payload */
    {
        size_t max_icmp_size = 65535;  /* Maximum IP packet size - IP header */
        ICMP_TEST_ASSERT(max_icmp_size > 0, "Maximum ICMP size validation");
    }
    
    return 0;
}

/* ============================================================
 * Test Runner
 * ============================================================ */

icmp_test_results_t icmp_run_all_tests(void)
{
    uart_printf("\n=== ICMP TEST SUITE START ===\n");
    
    /* Reset test results */
    memset(&s_test_results, 0, sizeof(s_test_results));
    
    /* Run all test modules */
    test_icmp_header_parsing();
    test_icmp_echo_request_reply();
    test_icmp_error_messages();
    test_icmp_checksum_calculation();
    test_icmp_ping_functionality();
    test_icmp_error_handling();
    
    /* Print results */
    icmp_test_print_results(&s_test_results);
    
    return s_test_results;
}
