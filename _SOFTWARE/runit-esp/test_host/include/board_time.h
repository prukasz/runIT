#pragma once
#include <stdint.h>
/* Host stand-in for core/time: the clock is whatever the test sets (host_mocks.h), or the PC's monotonic clock after
 * host_time_real(true). Both readings are the same clock here. */
uint64_t board_time_now_ms(void);
uint64_t board_time_uptime_ms(void);
