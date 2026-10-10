#pragma once

#include "router.h"

/* Shared between the router's own files only. */

/** Runs the registered safe-state handler (or logs). Caller's task. */
void router_enter_safe_state(router_safe_reason_e reason);

/** Registers the sys router under ROUTER_KEY_SYS. Called by router_init. */
MUST_USE err_h router_sys_register(void);
