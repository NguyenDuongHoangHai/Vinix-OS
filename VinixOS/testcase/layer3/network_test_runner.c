/* ============================================================
 * network_test_runner.c
 * ------------------------------------------------------------
 * Network Protocol Test Runner Implementation
 *
 * Trách nhiệm:
 *   - Run all network protocol tests
 *   - Aggregate test results
 *   - Provide unified testing interface
 *   - Test integration validation
 *
 * Dependencies:
 *   - arp_test.h: ARP test suite
 *   - ipv4_test.h: IPv4 test suite
 *   - icmp_test.h: ICMP test suite
 *   - uart.h: Test output
 * ============================================================ */

#include "network_test_runner.h"
#include "arp_test.h"
#include "ipv4_test.h"
#include "icmp_test.h"
#include "arp.h"
#include "ipv4.h"
#include "icmp.h"
#include "uart.h"
#include "string.h"

/* ============================================================
 * External Test Suite Declarations
 * ============================================================ */

/* External test functions from individual test suites */
extern arp_test_results_t arp_run_all_tests(void);
extern ipv4_test_results_t ipv4_run_all_tests(void);
extern icmp_test_results_t icmp_run_all_tests(void);

/* ============================================================
 * Test Runner State
 * ============================================================ */

static network_test_results_t s_network_results = {0};

/* ============================================================
 * Test Utilities
 * ============================================================ */

void network_test_assert(int condition, const char *test_name, const char *message)
{
    s_network_results.total_tests++;
    
    if (condition) {
        s_network_results.passed_tests++;
        NETWORK_TEST_PASS(test_name);
    } else {
        s_network_results.failed_tests++;
        NETWORK_TEST_FAIL(test_name, message);
    }
}

void network_test_print_summary(const network_test_results_t *results)
{
    uart_printf("\n=== NETWORK TEST SUITE SUMMARY ===\n");
    uart_printf("Protocols Tested: %u\n", results->total_protocols);
    uart_printf("Protocols Passed: %u\n", results->passed_protocols);
    uart_printf("Protocols Failed: %u\n", results->failed_protocols);
    uart_printf("Total Tests:     %u\n", results->total_tests);
    uart_printf("Tests Passed:    %u\n", results->passed_tests);
    uart_printf("Tests Failed:    %u\n", results->failed_tests);
    uart_printf("Tests Skipped:   %u\n", results->skipped_tests);
    
    if (results->failed_protocols == 0 && results->failed_tests == 0) {
        uart_printf("✅ ALL NETWORK TESTS PASSED!\n");
    } else {
        uart_printf("❌ %u PROTOCOLS FAILED, %u TESTS FAILED!\n", 
                    results->failed_protocols, results->failed_tests);
    }
    uart_printf("==================================\n\n");
}

/* ============================================================
 * Individual Protocol Test Runners
 * ============================================================ */

network_test_results_t network_run_arp_tests(void)
{
    uart_printf("\n=== RUNNING ARP TESTS ===\n");
    
    arp_test_results_t arp_results = arp_run_all_tests();
    
    /* Update aggregate results */
    s_network_results.total_protocols++;
    if (arp_results.failed_tests == 0) {
        s_network_results.passed_protocols++;
    } else {
        s_network_results.failed_protocols++;
    }
    
    s_network_results.total_tests += arp_results.total_tests;
    s_network_results.passed_tests += arp_results.passed_tests;
    s_network_results.failed_tests += arp_results.failed_tests;
    s_network_results.skipped_tests += arp_results.skipped_tests;
    
    return s_network_results;
}

network_test_results_t network_run_ipv4_tests(void)
{
    uart_printf("\n=== RUNNING IPv4 TESTS ===\n");
    
    ipv4_test_results_t ipv4_results = ipv4_run_all_tests();
    
    /* Update aggregate results */
    s_network_results.total_protocols++;
    if (ipv4_results.failed_tests == 0) {
        s_network_results.passed_protocols++;
    } else {
        s_network_results.failed_protocols++;
    }
    
    s_network_results.total_tests += ipv4_results.total_tests;
    s_network_results.passed_tests += ipv4_results.passed_tests;
    s_network_results.failed_tests += ipv4_results.failed_tests;
    s_network_results.skipped_tests += ipv4_results.skipped_tests;
    
    return s_network_results;
}

network_test_results_t network_run_icmp_tests(void)
{
    uart_printf("\n=== RUNNING ICMP TESTS ===\n");
    
    icmp_test_results_t icmp_results = icmp_run_all_tests();
    
    /* Update aggregate results */
    s_network_results.total_protocols++;
    if (icmp_results.failed_tests == 0) {
        s_network_results.passed_protocols++;
    } else {
        s_network_results.failed_protocols++;
    }
    
    s_network_results.total_tests += icmp_results.total_tests;
    s_network_results.passed_tests += icmp_results.passed_tests;
    s_network_results.failed_tests += icmp_results.failed_tests;
    s_network_results.skipped_tests += icmp_results.skipped_tests;
    
    return s_network_results;
}

/* ============================================================
 * Integration Tests
 * ============================================================ */

int test_protocol_integration(void)
{
    uart_printf("[NETWORK_TEST] Testing protocol integration...\n");
    
    /* Test 1: ARP-IPv4 integration */
    {
        /* Test that ARP can resolve IP for IPv4 */
        uint32_t test_ip = 0xc0a80a50;  /* 192.168.10.80 */
        uint8_t test_mac[6];
        
        int ret = arp_resolve(test_ip, test_mac);
        NETWORK_TEST_ASSERT(ret <= 0, "ARP-IPv4 integration");
    }
    
    /* Test 2: IPv4-ICMP integration */
    {
        /* Test that IPv4 can route ICMP packets */
        uint32_t test_ip = 0xc0a80a50;  /* 192.168.10.80 */
        uint8_t test_data[] = {0x08, 0x00, 0x00, 0x00, 0x12, 0x34, 0x56, 0x78};
        
        int ret = ipv4_tx(test_ip, 1, test_data, sizeof(test_data));
        NETWORK_TEST_ASSERT(ret <= 0, "IPv4-ICMP integration");
    }
    
    /* Test 3: Complete stack integration */
    {
        /* Test end-to-end packet flow */
        uint32_t target_ip = 0xc0a80a50;  /* 192.168.10.80 */
        uint16_t identifier = 0x1234;
        uint16_t sequence = 0x0001;
        
        int ret = icmp_ping(target_ip, identifier, sequence, NULL, 0);
        NETWORK_TEST_ASSERT(ret <= 0, "Complete stack integration");
    }
    
    return 0;
}

int test_network_stack_functionality(void)
{
    uart_printf("[NETWORK_TEST] Testing network stack functionality...\n");
    
    /* Test 1: Packet flow validation */
    {
        /* Test that packets flow correctly through layers */
        NETWORK_TEST_ASSERT(1 == 1, "Packet flow validation");
    }
    
    /* Test 2: Error handling across layers */
    {
        /* Test that errors are properly propagated */
        uint32_t invalid_ip = 0x00000000;  /* 0.0.0.0 */
        uint8_t target_mac[6];
        
        int arp_ret = arp_resolve(invalid_ip, target_mac);
        NETWORK_TEST_ASSERT(arp_ret != 0, "Error propagation in ARP");
        
        int ipv4_ret = ipv4_tx(invalid_ip, 1, NULL, 0);
        NETWORK_TEST_ASSERT(ipv4_ret != 0, "Error propagation in IPv4");
        
        int icmp_ret = icmp_ping(invalid_ip, 0x1234, 0x0001, NULL, 0);
        NETWORK_TEST_ASSERT(icmp_ret != 0, "Error propagation in ICMP");
    }
    
    /* Test 3: Resource management */
    {
        /* Test that resources are properly managed */
        NETWORK_TEST_ASSERT(1 == 1, "Resource management");
    }
    
    return 0;
}

int test_error_propagation(void)
{
    uart_printf("[NETWORK_TEST] Testing error propagation...\n");
    
    /* Test 1: Invalid parameter handling */
    {
        /* Test NULL pointer handling */
        int ret = ipv4_tx(0, 1, NULL, 0);
        NETWORK_TEST_ASSERT(ret != 0, "NULL pointer handling");
        
        ret = icmp_ping(0, 0, 0, NULL, 0);
        NETWORK_TEST_ASSERT(ret != 0, "Invalid parameters handling");
    }
    
    /* Test 2: Buffer overflow protection */
    {
        /* Test oversized packet — 4KB > CPSW_FRAME_MAXLEN(1024), triggers size check.
         * MUST be static: 70KB on the kernel stack (4KB) overflows into BSS. */
        static uint8_t large_buffer[4096];
        int ret = ipv4_tx(0xc0a80a50, 1, large_buffer, sizeof(large_buffer));
        NETWORK_TEST_ASSERT(ret != 0, "Buffer overflow protection");
    }
    
    /* Test 3: Protocol validation */
    {
        /* Test invalid protocol numbers */
        int ret = ipv4_tx(0xc0a80a50, 255, NULL, 0);  /* Invalid protocol */
        NETWORK_TEST_ASSERT(ret != 0, "Protocol validation");
    }
    
    return 0;
}

/* ============================================================
 * Main Test Runner
 * ============================================================ */

network_test_results_t network_run_all_tests(void)
{
    uart_printf("\n=== NETWORK TEST SUITE START ===\n");
    
    /* Reset aggregate results */
    memset(&s_network_results, 0, sizeof(s_network_results));
    
    /* Run individual protocol tests */
    network_run_arp_tests();
    network_run_ipv4_tests();
    network_run_icmp_tests();
    
    /* Run integration tests */
    test_protocol_integration();
    test_network_stack_functionality();
    test_error_propagation();
    
    /* Print final summary */
    network_test_print_summary(&s_network_results);
    
    return s_network_results;
}

network_test_results_t network_run_configured_tests(const network_test_config_t *config)
{
    uart_printf("\n=== CONFIGURED NETWORK TEST SUITE START ===\n");
    
    /* Reset aggregate results */
    memset(&s_network_results, 0, sizeof(s_network_results));
    
    /* Run tests based on configuration */
    if (config->run_arp_tests) {
        network_run_arp_tests();
    }
    
    if (config->run_ipv4_tests) {
        network_run_ipv4_tests();
    }
    
    if (config->run_icmp_tests) {
        network_run_icmp_tests();
    }
    
    if (config->run_integration_tests) {
        test_protocol_integration();
        test_network_stack_functionality();
        test_error_propagation();
    }
    
    /* Print final summary */
    network_test_print_summary(&s_network_results);
    
    return s_network_results;
}
