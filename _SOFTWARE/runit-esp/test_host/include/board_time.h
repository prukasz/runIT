#pragma once
#include <stdint.h>
/* Host stand-in for core/time: the clock is whatever the test sets (host_mocks.h). */
uint64_t board_time_now_ms(void);
