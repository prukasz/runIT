#pragma once

#include <stdint.h>
#include "sys_actions.h"
#include "sys_device.h"

/**
 * @brief Default static action slot IDs (1..CONFIG_SYS_ACTIONS_STATIC_SLOTS-1).
 */
#define SYS_ACTION_ID_BOOT        1
#define SYS_ACTION_ID_FREEZE      2
#define SYS_ACTION_ID_RESUME      3
#define SYS_ACTION_ID_SUSPEND     4
#define SYS_ACTION_ID_RESET       5
#define SYS_ACTION_ID_HARD_RESET  6

/**
 * @brief Registers default static action functions onto action IDs 2-6.
 * Boot-only, not mutex-protected, register everything before concurrent access starts.
 */
void sys_actions_register_static(void);

