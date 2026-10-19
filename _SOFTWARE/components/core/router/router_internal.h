#pragma once

#include "router.h"

/* Shared between the router's own files only. */

/** SYS SAFE_LEAVE: runs the registered leave (the devices resumed), then clears the note. */
MUST_USE err_h router_leave_safe_state(void);

/** Registers the sys router under ROUTER_KEY_SYS. Called by router_init. */
MUST_USE err_h router_sys_register(void);
