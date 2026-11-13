#pragma once
/* The Kconfig defaults of sys_event/Kconfig for the host tests. Override with -D (the variants in host_test.json). */
#ifndef CONFIG_SYS_EVENT_MAX_SUBS
#define CONFIG_SYS_EVENT_MAX_SUBS 32
#endif
#ifndef CONFIG_SYS_EVENT_QUEUE_BYTES
#define CONFIG_SYS_EVENT_QUEUE_BYTES 1024
#endif
#ifndef CONFIG_SYS_EVENT_DATA_MAX
#define CONFIG_SYS_EVENT_DATA_MAX 64
#endif
#ifndef CONFIG_SYS_EVENT_TASK_PRIO
#define CONFIG_SYS_EVENT_TASK_PRIO 6
#endif
#ifndef CONFIG_SYS_EVENT_TASK_STACK
#define CONFIG_SYS_EVENT_TASK_STACK 4096
#endif
