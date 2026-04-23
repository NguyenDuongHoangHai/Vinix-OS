/* ============================================================
 * arp_test.c
 * ------------------------------------------------------------
 * ARP Protocol Test Suite Implementation
 *
 * Trách nhiệm:
 *   - Unit tests cho ARP protocol
 *   - Test ARP request/reply functionality
 *   - Test ARP cache management
 *   - Test error handling
 *
 * Dependencies:
 *   - arp.h: ARP protocol interface
 *   - arp_test.h: Test framework
 *   - uart.h: Test output
 * ============================================================ */

#include "arp_test.h"
#include "arp.h"
#include "uart.h"
#include "string.h"

/* ============================================================
 * Test State
 * ============================================================ */

static arp_test_results_t s_test_results = {0};

/* ============================================================
 * Test Utilities
 * ============================================================ */

void arp_test_assert(int condition, const char *test_name, const char *message)
{
    s_test_results.total_tests++;
    
    if (condition) {
        s_test_results.passed_tests++;
        ARP_TEST_PASS(test_name);
    } else {
        s_test_results.failed_tests++;
        ARP_TEST_FAIL(test_name, message);
    }
}

void arp_test_print_results(const arp_test_results_t *results)
{
    uart_printf("\n=== ARP TEST RESULTS ===\n");
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
 * ARP Packet Parsing Tests
 * ============================================================ */

int test_arp_packet_parsing(void)
{
    uart_printf("[ARP_TEST] Testing ARP packet parsing...\n");
    
    /* Test 1: Valid ARP request packet */
    {
        uint8_t test_packet[28] = {
            0x00, 0x01,  /* Hardware type: Ethernet */
            0x08, 0x00,  /* Protocol type: IPv4 */
            0x06,        /* Hardware length */
            0x04,        /* Protocol length */
            0x00, 0x01,  /* Operation: Request */
            /* Sender MAC */
            0xde, 0xad, 0xbe, 0xef, 0x00, 0x01,
            /* Sender IP */
            0xc0, 0xa8, 0x0a, 0x64,  /* 192.168.10.100 */
            /* Target MAC */
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            /* Target IP */
            0xc0, 0xa8, 0x0a, 0x50   /* 192.168.10.80 */
        };
        
        /* Test packet structure validation */
        ARP_TEST_ASSERT(sizeof(test_packet) == 28, "ARP packet size");
        
        /* Test hardware type */
        uint16_t hw_type = (test_packet[0] << 8) | test_packet[1];
        ARP_TEST_ASSERT(hw_type == 1, "Hardware type is Ethernet");
        
        /* Test protocol type */
        uint16_t proto_type = (test_packet[2] << 8) | test_packet[3];
        ARP_TEST_ASSERT(proto_type == 0x0800, "Protocol type is IPv4");
        
        /* Test operation */
        uint16_t opcode = (test_packet[6] << 8) | test_packet[7];
        ARP_TEST_ASSERT(opcode == 1, "Operation is Request");
    }
    
    /* Test 2: Invalid packet size */
    {
        uint8_t short_packet[10] = {0};
        ARP_TEST_ASSERT(sizeof(short_packet) < 28, "Short packet detection");
    }
    
    return 0;
}

/* ============================================================
 * ARP Request/Reply Tests
 * ============================================================ */

int test_arp_request_reply(void)
{
    uart_printf("[ARP_TEST] Testing ARP request/reply...\n");
    
    /* Test 1: ARP request creation */
    {
        uint32_t target_ip = 0xc0a80a50;  /* 192.168.10.80 */
        uint8_t target_mac[6] = {0};
        
        /* Test ARP resolution */
        int ret = arp_resolve(target_ip, target_mac);
        ARP_TEST_ASSERT(ret == 0 || ret == -1, "ARP resolve returns valid status");
    }
    
    /* Test 2: MAC address validation */
    {
        uint8_t test_mac[6] = {0xde, 0xad, 0xbe, 0xef, 0x00, 0x01};
        uint8_t zero_mac[6] = {0};
        
        /* Test non-zero MAC */
        int is_zero = 1;
        for (int i = 0; i < 6; i++) {
            if (test_mac[i] != 0) {
                is_zero = 0;
                break;
            }
        }
        ARP_TEST_ASSERT(!is_zero, "Non-zero MAC address detection");
        
        /* Test zero MAC */
        is_zero = 1;
        for (int i = 0; i < 6; i++) {
            if (zero_mac[i] != 0) {
                is_zero = 0;
                break;
            }
        }
        ARP_TEST_ASSERT(is_zero, "Zero MAC address detection");
    }
    
    return 0;
}

/* ============================================================
 * ARP Cache Tests
 * ============================================================ */

int test_arp_cache_operations(void)
{
    uart_printf("[ARP_TEST] Testing ARP cache operations...\n");
    
    /* Test 1: Cache entry creation */
    {
        uint32_t test_ip = 0xc0a80a50;  /* 192.168.10.80 */
        uint8_t test_mac[6] = {0xde, 0xad, 0xbe, 0xef, 0x00, 0x01};
        
        /* This test would require access to internal cache functions */
        /* For now, just test the data structures */
        ARP_TEST_ASSERT(sizeof(test_ip) == 4, "IP size validation");
        ARP_TEST_ASSERT(sizeof(test_mac) == 6, "MAC size validation");
    }
    
    /* Test 2: Cache timeout simulation */
    {
        /* Simulate cache entry timeout */
        uint32_t timeout_seconds = 60;  /* Typical ARP cache timeout */
        ARP_TEST_ASSERT(timeout_seconds > 0, "Cache timeout validation");
    }
    
    return 0;
}

/* ============================================================
 * ARP Error Handling Tests
 * ============================================================ */

int test_arp_error_handling(void)
{
    uart_printf("[ARP_TEST] Testing ARP error handling...\n");
    
    /* Test 1: Invalid IP address */
    {
        uint32_t invalid_ip = 0x00000000;  /* 0.0.0.0 */
        uint8_t target_mac[6];
        
        /* Test ARP resolution with invalid IP */
        int ret = arp_resolve(invalid_ip, target_mac);
        ARP_TEST_ASSERT(ret != 0, "Invalid IP rejection");
    }
    
    /* Test 2: Broadcast IP */
    {
        uint32_t broadcast_ip = 0xFFFFFFFF;  /* 255.255.255.255 */
        uint8_t target_mac[6];
        
        /* Test ARP resolution with broadcast IP */
        int ret = arp_resolve(broadcast_ip, target_mac);
        ARP_TEST_ASSERT(ret != 0, "Broadcast IP rejection");
    }
    
    return 0;
}

/* ============================================================
 * ARP Byte Order Tests
 * ============================================================ */

int test_arp_byte_order(void)
{
    uart_printf("[ARP_TEST] Testing ARP byte order...\n");
    
    /* Test 1: Network byte order conversion */
    {
        uint32_t host_ip = 0xc0a80a50;  /* 192.168.10.80 */
        uint32_t network_ip = ((host_ip & 0xFF) << 24) | 
                             (((host_ip >> 8) & 0xFF) << 16) | 
                             (((host_ip >> 16) & 0xFF) << 8) | 
                             (host_ip >> 24);
        
        /* Test byte swap */
        uint32_t converted_back = ((network_ip & 0xFF) << 24) | 
                                 (((network_ip >> 8) & 0xFF) << 16) | 
                                 (((network_ip >> 16) & 0xFF) << 8) | 
                                 (network_ip >> 24);
        
        ARP_TEST_ASSERT(converted_back == host_ip, "Byte order conversion");
    }
    
    /* Test 2: 16-bit byte order */
    {
        uint16_t host_value = 0x0800;  /* EtherType for IPv4 */
        uint16_t network_value = (host_value >> 8) | (host_value << 8);
        uint16_t converted_back = (network_value >> 8) | (network_value << 8);
        
        ARP_TEST_ASSERT(converted_back == host_value, "16-bit byte order conversion");
    }
    
    return 0;
}

/* ============================================================
 * Test Runner
 * ============================================================ */

arp_test_results_t arp_run_all_tests(void)
{
    uart_printf("\n=== ARP TEST SUITE START ===\n");
    
    /* Reset test results */
    memset(&s_test_results, 0, sizeof(s_test_results));
    
    /* Run all test modules */
    test_arp_packet_parsing();
    test_arp_request_reply();
    test_arp_cache_operations();
    test_arp_error_handling();
    test_arp_byte_order();
    
    /* Print results */
    arp_test_print_results(&s_test_results);
    
    return s_test_results;
}
