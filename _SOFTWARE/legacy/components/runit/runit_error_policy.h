#pragma once
#include "sys_error.h"

/**
 * @brief Register the error policy, fault hooks, action executor and VM
 * route. First boot step after SE_init().
 */
SE_MUST_USE err_h runit_error_wiring_init(void);
