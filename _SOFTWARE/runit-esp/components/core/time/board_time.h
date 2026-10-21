#pragma once

#include <stdbool.h>
#include <stdint.h>

/*
 * The board's clock: one place that sets and reads the time.
 *
 * There is no battery clock: the time is a client's gift (board_time_set), kept over a software restart and lost with the power. It is
 * the system clock (settimeofday), so code that reads the C library's time sees the same one. A clock that was never set counts from
 * 1970 at the start, so anything before 2020 is "not set".
 */

/** @brief 2020-01-01 UTC in ms: a clock reading before it was never set. */
#define BOARD_TIME_SET_AFTER_MS 1577836800000ull

/** @brief True once a client has set the clock. */
bool board_time_is_set(void);

/** @brief The clock in ms: UTC since 1970 once set; before that, the time since the start (not a date). */
uint64_t board_time_now_ms(void);

/** @brief UTC in ms since 1970, or 0 while the clock was never set. */
uint64_t board_time_unix_ms(void);

/** @brief Sets the clock to UTC `unix_ms`. False when the system refuses it. */
bool board_time_set(uint64_t unix_ms);

/** @brief Time since the start in ms. Monotonic: setting the clock does not move it. Safe in an ISR. */
uint64_t board_time_uptime_ms(void);

/**
 * @brief The clock at the moment `uptime_ms` (a board_time_uptime_ms() reading taken earlier), in the units of board_time_now_ms().
 * For a stamp taken where the clock itself can't be read (an ISR); call it from a task.
 */
uint64_t board_time_at_uptime_ms(uint64_t uptime_ms);
