#pragma once
/* Kconfig defaults for sys_stream host tests. Override with -D. */
#ifndef CONFIG_SYS_STREAM_MAX
#define CONFIG_SYS_STREAM_MAX 8
#endif
#ifndef CONFIG_SYS_STREAM_TARGETS
#define CONFIG_SYS_STREAM_TARGETS 6
#endif
#ifndef CONFIG_SYS_STREAM_QUEUE_SIZE
#define CONFIG_SYS_STREAM_QUEUE_SIZE 2048
#endif
#ifndef CONFIG_SYS_STREAM_TASK_PRIO
#define CONFIG_SYS_STREAM_TASK_PRIO 4
#endif
#ifndef CONFIG_SYS_STREAM_TASK_STACK
#define CONFIG_SYS_STREAM_TASK_STACK 4096
#endif
