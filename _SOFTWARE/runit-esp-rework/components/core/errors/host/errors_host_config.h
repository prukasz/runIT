#pragma once
/* The Kconfig defaults of errors/Kconfig for the host tests. Override with -D (the variants in host_test.json). */
#ifndef CONFIG_ERRORS_INFO
#define CONFIG_ERRORS_INFO 1
#endif
#ifndef CONFIG_ERRORS_POOL_BLOCKS
#define CONFIG_ERRORS_POOL_BLOCKS 32
#endif
#ifndef CONFIG_ERRORS_POOL_BLOCK_SIZE
#define CONFIG_ERRORS_POOL_BLOCK_SIZE 32
#endif
#ifndef CONFIG_ERRORS_LOG_LINE_LEN
#define CONFIG_ERRORS_LOG_LINE_LEN 256
#endif
#ifndef CONFIG_ERRORS_RECORD_REQ_MAX
#define CONFIG_ERRORS_RECORD_REQ_MAX 32
#endif
#ifndef CONFIG_ERRORS_MAX_MODULES
#define CONFIG_ERRORS_MAX_MODULES 32
#endif
