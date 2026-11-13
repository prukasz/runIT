#pragma once
/* Host stand-in for core/utils: only what errors uses (the real one pulls in all of FreeRTOS). */
#include <assert.h>
#include <limits.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ARRAY_LEN(a) (sizeof(a) / sizeof((a)[0]))
#define MUST_USE     __attribute__((warn_unused_result))

#include "freertos_shim.h"

#define NO_WAIT  0  // a timeout of 0 ticks; forever is portMAX_DELAY
#define MSEC(ms) ((TickType_t)(ms))  // a tick is a millisecond here

/* The critical section (portMUX_TYPE, portENTER_CRITICAL...) is freertos_shim.h's, as on the board it is FreeRTOS's. */
