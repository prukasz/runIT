#pragma once

#include "errors.h"

/* The watch client's own start (sys_watch_client.c), called by sys_watch_init(). Not on the component's include path. */

MUST_USE err_h sys_watch_client_init(void);
