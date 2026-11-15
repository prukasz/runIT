#pragma once
/* The Kconfig of sys_session for the host tests, with short timeouts: a test that waits for a session's silence or a
 * call's timeout waits this long. Override with -D. */
#ifndef CONFIG_SYS_SESSION_MAX
#define CONFIG_SYS_SESSION_MAX 3
#endif
#ifndef CONFIG_SYS_SESSION_CALLS
#define CONFIG_SYS_SESSION_CALLS 8
#endif
#ifndef CONFIG_SYS_SESSION_CALL_TIMEOUT_MS
#define CONFIG_SYS_SESSION_CALL_TIMEOUT_MS 150
#endif
#ifndef CONFIG_SYS_SESSION_BOARD_ID_DEFAULT
#define CONFIG_SYS_SESSION_BOARD_ID_DEFAULT 5
#endif
