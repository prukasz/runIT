#pragma once

#include "errors.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief The app's side of the error policy (SYS_DEVICE.MD "Error policy"): the action runner, and the error sink: every reported chain runs
 * the device policy, then goes to the errors stream as a record. Boot step, after sys_connector_init(), before
 * anything reports an error worth a response.
 */
MUST_USE err_h runit_error_wiring_init(void);

#ifdef __cplusplus
}
#endif
