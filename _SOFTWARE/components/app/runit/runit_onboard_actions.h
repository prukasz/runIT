#pragma once

#include "errors.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Stores the firmware's own actions (RUNIT.MD "Onboard actions"): resume all and suspend all devices, under
 * CONFIG_RUNIT_ACTION_ID_RESUME / _SUSPEND. Boot step, after sys_actions_init(). Written only when the stored action
 * differs.
 */
MUST_USE err_h runit_onboard_actions_init(void);

#ifdef __cplusplus
}
#endif
