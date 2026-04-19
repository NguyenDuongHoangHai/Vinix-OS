/* ============================================================
 * selftest.h — boot-time test harness.
 * ============================================================ */

#ifndef SELFTEST_H
#define SELFTEST_H

/* Every test returns 0 on pass, <0 on fail. */
typedef int (*selftest_fn)(void);

struct selftest {
    const char *name;
    selftest_fn run;
};

/* Walk the registered tests, log pass/fail for each, panic on first
 * failure. Called from main.c after all subsystems are up. */
void selftest_run_all(void);

#endif
