#pragma once
/* The Kconfig defaults of timer/Kconfig for the host tests. Override with -D (the variants in host_test.json). */
#ifndef CONFIG_BOARD_TIMER_TASK_PRIO
#define CONFIG_BOARD_TIMER_TASK_PRIO 7
#endif
#ifndef CONFIG_BOARD_TIMER_TASK_STACK
#define CONFIG_BOARD_TIMER_TASK_STACK 4096
#endif
