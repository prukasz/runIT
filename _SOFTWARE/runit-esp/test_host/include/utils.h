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

#define NO_WAIT 0  // a timeout of 0 ticks; forever is portMAX_DELAY

/* A critical section is one process-wide mutex: tasks are threads here, so what a spinlock protects on the board is
 * protected for real (an ISR is not modelled). Not recursive, like a spinlock. */
typedef int portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED 0
void host_critical_enter(void);
void host_critical_exit(void);
#define portENTER_CRITICAL_SAFE(m)   do {                                 (void)(m);                         host_critical_enter();           } while (0)
#define portEXIT_CRITICAL_SAFE(m)   do {                                (void)(m);                        host_critical_exit();           } while (0)
