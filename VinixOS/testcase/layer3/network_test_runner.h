/* ============================================================
 * network_test_runner.h
 * ------------------------------------------------------------
 * Network Protocol Test Runner
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

#ifndef NETWORK_TEST_RUNNER_H
#define NETWORK_TEST_RUNNER_H

#include "types.h"

/* ============================================================
 * Aggregate Test Results
 * ============================================================ */

typedef struct {
    uint32_t total_protocols;
    uint32_t passed_protocols;
    uint32_t failed_protocols;
    
    uint32_t total_tests;
    uint32_t passed_tests;
    uint32_t failed_tests;
    uint32_t skipped_tests;
} network_test_results_t;

/* ============================================================
 * Test Runner Interface
 * ============================================================ */

/* Run all network protocol tests */
/* Returns aggregate test results */
network_test_results_t network_run_all_tests(void);

/* Run individual protocol tests */
network_test_results_t network_run_arp_tests(void);
network_test_results_t network_run_ipv4_tests(void);
network_test_results_t network_run_icmp_tests(void);

/* Integration tests */
int test_protocol_integration(void);
int test_network_stack_functionality(void);
int test_error_propagation(void);

/* Test utilities */
void network_test_print_summary(const network_test_results_t *results);
void network_test_assert(int condition, const char *test_name, const char *message);

/* Test configuration */
typedef struct {
    int run_arp_tests;
    int run_ipv4_tests;
    int run_icmp_tests;
    int run_integration_tests;
    int verbose_output;
} network_test_config_t;

/* Configure and run tests with custom settings */
network_test_results_t network_run_configured_tests(const network_test_config_t *config);

/* Test macros */
#define NETWORK_TEST_ASSERT(condition, message) \
    network_test_assert(condition, #condition, message)

#define NETWORK_TEST_PASS(test_name) \
    uart_printf("[NETWORK_TEST] PASS: %s\n", test_name)

#define NETWORK_TEST_FAIL(test_name, message) \
    uart_printf("[NETWORK_TEST] FAIL: %s - %s\n", test_name, message)

#define NETWORK_TEST_SKIP(test_name, reason) \
    uart_printf("[NETWORK_TEST] SKIP: %s - %s\n", test_name, reason)

#endif /* NETWORK_TEST_RUNNER_H */
