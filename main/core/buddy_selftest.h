/*
 * buddy_selftest.h — "Is everything actually working?"
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 *
 * A firmware this size has a lot of ways to be half-broken in a manner that is
 * invisible from the outside: the sensor dock not seated, a chime missing from
 * the filesystem image, the speech models not flashed, PSRAM not detected. In
 * every one of those cases the buddy still boots and still shows a face, which
 * makes the fault harder to spot, not easier.
 *
 * So it checks itself. The results go to the log at every boot and can be read
 * on screen from Settings, which turns "it is being weird" into a specific
 * line of text you can act on.
 *
 * Nothing here changes any state. It is safe to run at any time.
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** How a single check came out. */
typedef enum {
    BUDDY_CHECK_PASSED = 0,  /*!< Working as intended                          */
    BUDDY_CHECK_WARNING,     /*!< Usable, but something is missing or optional */
    BUDDY_CHECK_FAILED,      /*!< Broken; the feature will not work            */
} buddy_check_result_t;

/** Longest name and detail text a check may produce. */
#define BUDDY_CHECK_NAME_MAX_LEN    24
#define BUDDY_CHECK_DETAIL_MAX_LEN  64

/** The outcome of one check. */
typedef struct {
    char                 name[BUDDY_CHECK_NAME_MAX_LEN];
    char                 detail[BUDDY_CHECK_DETAIL_MAX_LEN];
    buddy_check_result_t result;
} buddy_check_t;

/** Upper bound on how many checks a report can hold. */
#define BUDDY_SELFTEST_MAX_CHECKS   16

/** The full picture. */
typedef struct {
    buddy_check_t checks[BUDDY_SELFTEST_MAX_CHECKS];
    uint8_t       check_count;
    uint8_t       passed_count;
    uint8_t       warning_count;
    uint8_t       failed_count;
} buddy_selftest_report_t;

/**
 * @brief Run every check and fill in a report.
 *
 * Takes a few milliseconds. Safe to call from any task.
 *
 * @param report  Receives the results. Must not be NULL.
 */
void buddy_selftest_run(buddy_selftest_report_t *report);

/**
 * @brief Run the checks and print the results to the log.
 *
 * @return true when nothing failed outright (warnings are tolerated).
 */
bool buddy_selftest_run_and_log(void);

/**
 * @brief Turn a report into readable multi-line text for the screen.
 *
 * @param report       A report from buddy_selftest_run().
 * @param buffer       Destination text buffer.
 * @param buffer_size  Size of @p buffer in bytes.
 */
void buddy_selftest_format(const buddy_selftest_report_t *report,
                           char *buffer, size_t buffer_size);

#ifdef __cplusplus
}
#endif
