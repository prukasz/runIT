#pragma once

/* The component's Kconfig defaults, and the error record's nodes (errors' Kconfig, which its host config leaves out). */
#ifndef CONFIG_ERRORS_RECORD_NODES
#define CONFIG_ERRORS_RECORD_NODES 6
#endif
#ifndef CONFIG_SYS_DEVICE_REMOTE_SUBSCRIPTIONS
#define CONFIG_SYS_DEVICE_REMOTE_SUBSCRIPTIONS 16
#endif
#ifndef CONFIG_SYS_DEVICE_REMOTE_QUEUE_SIZE
#define CONFIG_SYS_DEVICE_REMOTE_QUEUE_SIZE 1024
#endif
#ifndef CONFIG_SYS_DEVICE_REMOTE_TASK_PRIO
#define CONFIG_SYS_DEVICE_REMOTE_TASK_PRIO 4
#endif
#ifndef CONFIG_SYS_DEVICE_REMOTE_TASK_STACK
#define CONFIG_SYS_DEVICE_REMOTE_TASK_STACK 4096
#endif
