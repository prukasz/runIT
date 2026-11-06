#pragma once

#include "errors.h"
#include "sys_actions.h"
#include "sys_nvs.h"

/* ----------------------------------------------------------------------------
 * The board's profile (SYS_BOARD.MD): what the board is, written by the app and
 * replayed at every boot. The firmware checks nothing in it: the app does.
 * ------------------------------------------------------------------------- */

#define SYS_BOARD_ACTIONS_CLASS 0x04u
#define SYS_BOARD_RECORD_ID     SYS_ACTIONS_ID(SYS_BOARD_ACTIONS_CLASS, 0)

/** @brief Opens the profile's JSON. Boot, after the step "nvs". */
MUST_USE err_h sys_board_init(void);

/** @brief The profile's JSON: the app's definition of the board, the firmware never reads it. */
sys_nvs_json_t* sys_board_json(void);

/**
 * @brief Queues a RUN_ALL of the profile's record on the router, ahead of whatever is queued after it. No record: a
 * board with no profile, nothing is queued. Boot, after the router.
 * @return NULL when queued or there is no record; the failures of the frames are reported when they run.
 */
MUST_USE err_h sys_board_boot(void);
