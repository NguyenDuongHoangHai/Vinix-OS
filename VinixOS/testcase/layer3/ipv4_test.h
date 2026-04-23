/* ============================================================
 * ipv4_test.h
 * ------------------------------------------------------------
 * IPv4 Protocol Test Suite
 *
 * Trách nhiệm:
 *   - Unit tests cho IPv4 protocol
 *   - Test IPv4 header processing
 *   - Test IPv4 routing and forwarding
 *   - Test IPv4 checksum calculation
 *
 * Dependencies:
 *   - ipv4.h: IPv4 protocol interface
 *   - uart.h: Test output
 * ============================================================ */

#ifndef IPV4_TEST_H
#define IPV4_TEST_H

#include "types.h"

/* ============================================================
 * Test Results Structure
 * ============================================================ */

typedef struct {
    uint32_t total_tests;
    uint32_t passed_tests;
    uint32_t failed_tests;
    uint32_t skipped_tests;
} ipv4_test_results_t;

/* ============================================================
 * Test Interface
 * ============================================================ */

/* Run all IPv4 tests */
/* Returns test results */
ipv4_test_results_t ipv4_run_all_tests(void);

/* Run individual test modules */
int test_ipv4_header_parsing(void);
int test_ipv4_header_validation(void);
int test_ipv4_checksum_calculation(void);
int test_ipv4_routing(void);
int test_ipv4_fragmentation(void);
int test_ipv4_error_handling(void);

/* Test utilities */
void ipv4_test_print_results(const ipv4_test_results_t *results);
void ipv4_test_assert(int condition, const char *test_name, const char *message);

/* Test macros */
#define IPV4_TEST_ASSERT(condition, message) \
    ipv4_test_assert(condition, #condition, message)

#define IPV4_TEST_PASS(test_name) \
    uart_printf("[IPV4_TEST] PASS: %s\n", test_name)

#define IPV4_TEST_FAIL(test_name, message) \
    uart_printf("[IPV4_TEST] FAIL: %s - %s\n", test_name, message)

#define IPV4_TEST_SKIP(test_name, reason) \
    uart_printf("[IPV4_TEST] SKIP: %s - %s\n", test_name, reason)

#endif /* IPV4_TEST_H */
