#pragma once

#include <stdint.h>
#include "sys_actions.h"
#include "sys_device.h"

#include <sdkconfig.h>

/**
 * @brief Default static action slot IDs (1..CONFIG_SYS_ACTIONS_STATIC_SLOTS-1).
 */

/**
 * @brief Registers default static action functions onto action IDs 2-6.
 * Boot-only, not mutex-protected, register everything before concurrent access starts.
 */
void sys_actions_register_static(void);

