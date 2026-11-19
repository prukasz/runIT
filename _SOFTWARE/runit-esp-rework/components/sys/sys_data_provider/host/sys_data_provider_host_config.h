#pragma once
/* The Kconfig of sys_data_provider for the host tests: few slots, so a test fills them. Override with -D. */
#ifndef CONFIG_SYS_DATA_PROVIDER_MAX
#define CONFIG_SYS_DATA_PROVIDER_MAX 3
#endif
#ifndef CONFIG_SYS_DATA_PROVIDER_SIZE
#define CONFIG_SYS_DATA_PROVIDER_SIZE 64
#endif
#ifndef CONFIG_SYS_DATA_PROVIDER_TYPES
#define CONFIG_SYS_DATA_PROVIDER_TYPES 8
#endif
