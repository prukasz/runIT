#pragma once
/* The simulator's sdkconfig: the Kconfig defaults of the firmware components it builds. */
/* An error node holds two pointers: on the 64-bit PC its header is 8 bytes longer than on the board (32 there). */
#define CONFIG_ERRORS_POOL_BLOCK_SIZE 40
#include "errors_host_config.h"
#include "router_host_config.h"

/* sys_device/Kconfig */
#define CONFIG_SYS_DEVICE_MAX      32
#define CONFIG_SYS_DEVICE_TYPES    16
#define CONFIG_SYS_DEVICE_MAX_DEPS 4
#define CONFIG_SYS_DEVICE_GUARDS   2

/* sys_lvgl/Kconfig */
#define CONFIG_SYS_LVGL_HEAP_DEFAULT      65536
#define CONFIG_SYS_LVGL_BUFFER_MAX        32768
#define CONFIG_SYS_LVGL_FLUSH_TIMEOUT_MS  200
#define CONFIG_SYS_LVGL_HEALTH_MS         1000
#define CONFIG_SYS_LVGL_TASK_STACK        8192
#define CONFIG_SYS_LVGL_TASK_PRIO         4
#define CONFIG_SYS_LVGL_POOL_RESERVE      8192
