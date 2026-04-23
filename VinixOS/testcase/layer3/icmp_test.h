/* ============================================================
 * icmp_test.h
 * ------------------------------------------------------------
 * ICMP Protocol Test Suite
 *
 * Trách nhiệm:
 *   - Unit tests cho ICMP protocol
 *   - Test ICMP echo request/reply
 *   - Test ICMP error messages
 *   - Test ICMP checksum calculation
 *
 * Dependencies:
 *   - icmp.h: ICMP protocol interface
 *   - uart.h: Test output
 * ============================================================ */

#ifndef ICMP_TEST_H
#define ICMP_TEST_H

#include "types.h"

/* ============================================================
 * Test Results Structure
 * ============================================================ */

typedef struct {
    uint32_t total_tests;
    uint32_t passed_tests;
    uint32_t failed_tests;
    uint32_t skipped_tests;
} icmp_test_results_t;

/* ============================================================
 * Test Interface
 * ============================================================ */

/* Run all ICMP tests */
/* Returns test results */
icmp_test_results_t icmp_run_all_tests(void);

/* Run individual test modules */
int test_icmp_header_parsing(void);
int test_icmp_echo_request_reply(void);
int test_icmp_error_messages(void);
int test_icmp_checksum_calculation(void);
int test_icmp_ping_functionality(void);
int test_icmp_error_handling(void);

/* Test utilities */
void icmp_test_print_results(const icmp_test_results_t *results);
void icmp_test_assert(int condition, const char *test_name, const char *message);

/* Test macros */
#define ICMP_TEST_ASSERT(condition, message) \
    icmp_test_assert(condition, #condition, message)

#define ICMP_TEST_PASS(test_name) \
    uart_printf("[ICMP_TEST] PASS: %s\n", test_name)

#define ICMP_TEST_FAIL(test_name, message) \
    uart_printf("[ICMP_TEST] FAIL: %s - %s\n", test_name, message)

#define ICMP_TEST_SKIP(test_name, reason) \
    uart_printf("[ICMP_TEST] SKIP: %s - %s\n", test_name, reason)

#endif /* ICMP_TEST_H */
