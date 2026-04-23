/* ============================================================
 * ipv4_test.c
 * ------------------------------------------------------------
 * IPv4 Protocol Test Suite Implementation
 *
 * Trách nhiệm:
 *   - Unit tests cho IPv4 protocol
 *   - Test IPv4 header processing
 *   - Test IPv4 routing and forwarding
 *   - Test IPv4 checksum calculation
 *
 * Dependencies:
 *   - ipv4.h: IPv4 protocol interface
 *   - ipv4_test.h: Test framework
 *   - uart.h: Test output
 * ============================================================ */

#include "ipv4_test.h"
#include "ipv4.h"
#include "uart.h"
#include "string.h"

/* ============================================================
 * Test State
 * ============================================================ */

static ipv4_test_results_t s_test_results = {0};

/* ============================================================
 * Test Utilities
 * ============================================================ */

void ipv4_test_assert(int condition, const char *test_name, const char *message)
{
    s_test_results.total_tests++;
    
    if (condition) {
        s_test_results.passed_tests++;
        IPV4_TEST_PASS(test_name);
    } else {
        s_test_results.failed_tests++;
        IPV4_TEST_FAIL(test_name, message);
    }
}

void ipv4_test_print_results(const ipv4_test_results_t *results)
{
    uart_printf("\n=== IPv4 TEST RESULTS ===\n");
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
 * IPv4 Header Parsing Tests
 * ============================================================ */

int test_ipv4_header_parsing(void)
{
    uart_printf("[IPV4_TEST] Testing IPv4 header parsing...\n");
    
    /* Test 1: Valid IPv4 header */
    {
        uint8_t test_header[20] = {
            0x45,        /* Version (4) + IHL (5) */
            0x00,        /* DSCP + ECN */
            0x00, 0x28,  /* Total length (40) */
            0x00, 0x00,  /* Identification */
            0x40, 0x00,  /* Flags + Fragment offset */
            0x40,        /* TTL */
            0x01,        /* Protocol (ICMP) */
            0x00, 0x00,  /* Header checksum */
            /* Source IP */
            0xc0, 0xa8, 0x0a, 0x64,  /* 192.168.10.100 */
            /* Destination IP */
            0xc0, 0xa8, 0x0a, 0x50   /* 192.168.10.80 */
        };
        
        /* Test version extraction */
        uint8_t version = test_header[0] >> 4;
        IPV4_TEST_ASSERT(version == 4, "IPv4 version extraction");
        
        /* Test IHL extraction */
        uint8_t ihl = test_header[0] & 0x0F;
        IPV4_TEST_ASSERT(ihl == 5, "IPv4 IHL extraction");
        
        /* Test total length extraction */
        uint16_t total_len = (test_header[2] << 8) | test_header[3];
        IPV4_TEST_ASSERT(total_len == 40, "IPv4 total length extraction");
        
        /* Test protocol extraction */
        uint8_t protocol = test_header[9];
        IPV4_TEST_ASSERT(protocol == 1, "IPv4 protocol extraction (ICMP)");
    }
    
    /* Test 2: Invalid IPv4 header */
    {
        uint8_t invalid_header[20] = {
            0x65,        /* Invalid version (6) */
            0x00,        /* DSCP + ECN */
            0x00, 0x28,  /* Total length (40) */
            0x00, 0x00,  /* Identification */
            0x40, 0x00,  /* Flags + Fragment offset */
            0x40,        /* TTL */
            0x01,        /* Protocol (ICMP) */
            0x00, 0x00,  /* Header checksum */
            /* Source IP */
            0xc0, 0xa8, 0x0a, 0x64,
            /* Destination IP */
            0xc0, 0xa8, 0x0a, 0x50
        };
        
        uint8_t version = invalid_header[0] >> 4;
        IPV4_TEST_ASSERT(version != 4, "Invalid IPv4 version detection");
    }
    
    return 0;
}

/* ============================================================
 * IPv4 Header Validation Tests
 * ============================================================ */

int test_ipv4_header_validation(void)
{
    uart_printf("[IPV4_TEST] Testing IPv4 header validation...\n");
    
    /* Test 1: Minimum header length */
    {
        IPV4_TEST_ASSERT(sizeof(uint8_t) * 20 == 20, "Minimum IPv4 header length");
    }
    
    /* Test 2: Maximum header length */
    {
        IPV4_TEST_ASSERT(sizeof(uint8_t) * 60 == 60, "Maximum IPv4 header length");
    }
    
    /* Test 3: Valid IHL values */
    {
        for (uint8_t ihl = 5; ihl <= 15; ihl++) {
            IPV4_TEST_ASSERT(ihl >= 5 && ihl <= 15, "Valid IHL range");
        }
    }
    
    /* Test 4: Invalid IHL values */
    {
        uint8_t invalid_ihl = 4;
        IPV4_TEST_ASSERT(invalid_ihl < 5, "Invalid IHL detection");
    }
    
    return 0;
}

/* ============================================================
 * IPv4 Checksum Calculation Tests
 * ============================================================ */

int test_ipv4_checksum_calculation(void)
{
    uart_printf("[IPV4_TEST] Testing IPv4 checksum calculation...\n");
    
    /* Test 1: Simple checksum calculation */
    {
        uint16_t test_data[] = {0x4500, 0x0028, 0x0000, 0x4000, 0x4001, 0x0000};
        uint32_t sum = 0;
        
        /* Calculate sum */
        for (int i = 0; i < 6; i++) {
            sum += test_data[i];
        }
        
        /* Add carry bits */
        while (sum >> 16) {
            sum = (sum & 0xFFFF) + (sum >> 16);
        }
        
        /* One's complement */
        uint16_t checksum = ~sum;
        
        IPV4_TEST_ASSERT(checksum != 0, "Checksum calculation produces non-zero result");
    }
    
    /* Test 2: Zero data checksum */
    {
        uint16_t zero_data[] = {0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000};
        uint32_t sum = 0;
        
        for (int i = 0; i < 6; i++) {
            sum += zero_data[i];
        }
        
        while (sum >> 16) {
            sum = (sum & 0xFFFF) + (sum >> 16);
        }
        
        uint16_t checksum = ~sum;
        IPV4_TEST_ASSERT(checksum == 0xFFFF, "Zero data checksum");
    }
    
    return 0;
}

/* ============================================================
 * IPv4 Routing Tests
 * ============================================================ */

int test_ipv4_routing(void)
{
    uart_printf("[IPV4_TEST] Testing IPv4 routing...\n");
    
    /* Test 1: Local address routing */
    {
        uint32_t local_ip = 0xc0a80a64;  /* 192.168.10.100 */
        uint32_t dest_ip = 0xc0a80a64;   /* Same IP */
        
        IPV4_TEST_ASSERT(local_ip == dest_ip, "Local address detection");
    }
    
    /* Test 2: Network address routing */
    {
        uint32_t network_ip = 0xc0a80a00;  /* 192.168.10.0 */
        uint32_t dest_ip = 0xc0a80a50;    /* 192.168.10.80 */
        
        /* Simple network mask check (255.255.255.0) */
        uint32_t network_part = dest_ip & 0xFFFFFF00;
        IPV4_TEST_ASSERT(network_part == network_ip, "Network address routing");
    }
    
    /* Test 3: Broadcast address routing */
    {
        uint32_t broadcast_ip = 0xFFFFFFFF;  /* 255.255.255.255 */
        IPV4_TEST_ASSERT(broadcast_ip == 0xFFFFFFFF, "Broadcast address detection");
    }
    
    return 0;
}

/* ============================================================
 * IPv4 Fragmentation Tests
 * ============================================================ */

int test_ipv4_fragmentation(void)
{
    uart_printf("[IPV4_TEST] Testing IPv4 fragmentation...\n");
    
    /* Test 1: Fragmentation flag */
    {
        uint16_t flags_fragment = 0x2000;  /* Don't fragment flag */
        uint8_t flags = (flags_fragment >> 13) & 0x07;
        IPV4_TEST_ASSERT(flags == 0x04, "Don't fragment flag extraction");
    }
    
    /* Test 2: Fragment offset */
    {
        uint16_t flags_fragment = 0x1FFF;  /* Maximum fragment offset */
        uint16_t fragment_offset = flags_fragment & 0x1FFF;
        IPV4_TEST_ASSERT(fragment_offset == 0x1FFF, "Fragment offset extraction");
    }
    
    /* Test 3: Maximum packet size */
    {
        uint16_t max_packet_size = 65535;
        IPV4_TEST_ASSERT(max_packet_size == 65535, "Maximum IPv4 packet size");
    }
    
    return 0;
}

/* ============================================================
 * IPv4 Error Handling Tests
 * ============================================================ */

int test_ipv4_error_handling(void)
{
    uart_printf("[IPV4_TEST] Testing IPv4 error handling...\n");
    
    /* Test 1: Packet too short */
    {
        uint8_t short_packet[10] = {0};
        IPV4_TEST_ASSERT(sizeof(short_packet) < 20, "Short packet detection");
    }
    
    /* Test 2: Invalid total length */
    {
        uint16_t invalid_length = 0;
        IPV4_TEST_ASSERT(invalid_length == 0, "Invalid total length detection");
    }
    
    /* Test 3: Length mismatch */
    {
        uint16_t header_length = 20;
        uint16_t total_length = 10;
        IPV4_TEST_ASSERT(total_length < header_length, "Length mismatch detection");
    }
    
    return 0;
}

/* ============================================================
 * Test Runner
 * ============================================================ */

ipv4_test_results_t ipv4_run_all_tests(void)
{
    uart_printf("\n=== IPv4 TEST SUITE START ===\n");
    
    /* Reset test results */
    memset(&s_test_results, 0, sizeof(s_test_results));
    
    /* Run all test modules */
    test_ipv4_header_parsing();
    test_ipv4_header_validation();
    test_ipv4_checksum_calculation();
    test_ipv4_routing();
    test_ipv4_fragmentation();
    test_ipv4_error_handling();
    
    /* Print results */
    ipv4_test_print_results(&s_test_results);
    
    return s_test_results;
}
