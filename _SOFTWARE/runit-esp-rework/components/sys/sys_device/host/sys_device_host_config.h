#pragma once
/* The Kconfig defaults of sys_device/Kconfig for the host tests, but a short lock timeout: a test that waits for a busy
 * device waits this long. Override with -D (the variants in host_test.json). */
#ifndef CONFIG_SYS_DEVICE_TYPES
#define CONFIG_SYS_DEVICE_TYPES 16
#endif
#ifndef CONFIG_SYS_DEVICE_INTERCEPTORS
#define CONFIG_SYS_DEVICE_INTERCEPTORS 2
#endif
#ifndef CONFIG_SYS_DEVICE_LOCK_TIMEOUT_MS
#define CONFIG_SYS_DEVICE_LOCK_TIMEOUT_MS 200
#endif
#ifndef CONFIG_SYS_DEVICE_EVENT_HOLD_BYTES
#define CONFIG_SYS_DEVICE_EVENT_HOLD_BYTES 256
#endif
