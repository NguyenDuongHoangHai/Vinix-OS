/* ============================================================
 * arp_test.h
 * ------------------------------------------------------------
 * ARP Protocol Test Suite
 *
 * Trách nhiệm:
 *   - Unit tests cho ARP protocol
 *   - Test ARP request/reply functionality
 *   - Test ARP cache management
 *   - Test error handling
 *
 * Dependencies:
 *   - arp.h: ARP protocol interface
 *   - uart.h: Test output
 * ============================================================ */

#ifndef ARP_TEST_H
#define ARP_TEST_H

#include "types.h"

/* ============================================================
 * Test Results Structure
 * ============================================================ */

typedef struct {
    uint32_t total_tests;
    uint32_t passed_tests;
    uint32_t failed_tests;
    uint32_t skipped_tests;
} arp_test_results_t;

/* ============================================================
 * Test Interface
 * ============================================================ */

/* Run all ARP tests */
/* Returns test results */
arp_test_results_t arp_run_all_tests(void);

/* Run individual test modules */
int test_arp_packet_parsing(void);
int test_arp_request_reply(void);
int test_arp_cache_operations(void);
int test_arp_error_handling(void);
int test_arp_byte_order(void);

/* Test utilities */
void arp_test_print_results(const arp_test_results_t *results);
void arp_test_assert(int condition, const char *test_name, const char *message);

/* Test macros */
#define ARP_TEST_ASSERT(condition, message) \
    arp_test_assert(condition, #condition, message)

#define ARP_TEST_PASS(test_name) \
    uart_printf("[ARP_TEST] PASS: %s\n", test_name)

#define ARP_TEST_FAIL(test_name, message) \
    uart_printf("[ARP_TEST] FAIL: %s - %s\n", test_name, message)

#define ARP_TEST_SKIP(test_name, reason) \
    uart_printf("[ARP_TEST] SKIP: %s - %s\n", test_name, reason)

#endif /* ARP_TEST_H */
