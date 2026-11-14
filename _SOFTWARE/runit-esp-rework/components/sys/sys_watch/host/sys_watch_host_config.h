#pragma once
/* Kconfig defaults for sys_watch host tests. Override with -D. */
#ifndef CONFIG_SYS_WATCH_MAX
#define CONFIG_SYS_WATCH_MAX 4
#endif
#ifndef CONFIG_SYS_WATCH_CLIENT_MAX
#define CONFIG_SYS_WATCH_CLIENT_MAX 4
#endif
#ifndef CONFIG_SYS_WATCH_READ_PERIOD_MS
#define CONFIG_SYS_WATCH_READ_PERIOD_MS 20
#endif
#ifndef CONFIG_SYS_WATCH_IDLE_MIN_MS
#define CONFIG_SYS_WATCH_IDLE_MIN_MS 200
#endif
