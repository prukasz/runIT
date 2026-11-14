#pragma once

#include "errors.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Initializes the watch client side (timers and session listeners). Called by sys_watch_init(). */
MUST_USE err_h sys_watch_client_init(void);

#ifdef __cplusplus
}
#endif
