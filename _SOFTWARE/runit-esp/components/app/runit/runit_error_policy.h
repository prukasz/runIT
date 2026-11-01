#pragma once

#include "errors.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief The app's side of the error policy (SYS_DEVICE.MD "Error policy"): the action runner, and the error sink: every reported chain runs
 * the device policy, then goes to the errors stream as a record. Boot step, after sys_data_connector_init(), before
 * anything reports an error worth a response.
 */
MUST_USE err_h runit_error_wiring_init(void);

/** @brief What an error about another board as a whole (a mirror of its stream lost frames) makes this board do. */
typedef enum {
  RUNIT_BOARD_POLICY_LOG = 0,      /**< Only the record on the errors stream (the default) */
  RUNIT_BOARD_POLICY_STOP_VM = 1,  /**< ... and the VM stops */
  RUNIT_BOARD_POLICY_SAFE = 2,     /**< ... and the safe state */
} runit_board_policy_e;

/**
 * @brief Sets the policy for the errors about a board (SYS_REMOTE.MD): the board config's. Any task.
 * @param board   0..SYS_SESSION_ID_LAST.
 * @param policy  runit_board_policy_e.
 * @return NULL when set; ERR_CMD_BAD_ARG.
 */
MUST_USE err_h runit_board_policy_set(uint8_t board, runit_board_policy_e policy);

#ifdef __cplusplus
}
#endif
