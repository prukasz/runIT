#pragma once

#include "errors.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * The board's profile (SYS_BOARD.MD): what the board is, written by the app and run at every boot. Two stored things:
 * the record SYS_ACTIONS_ID_BOARD (sys_actions.h: the frames that create its buses, ports and devices) and a JSON the
 * app reads back (its board descriptor). The firmware runs the record and checks nothing in either: the app does.
 */

/**
 * @brief Opens the profile's JSON (namespace "board" in CONFIG_SYS_ACTIONS_PARTITION). Boot, after sys_nvs_init.
 * @return The error of opening the file
 */
MUST_USE err_h sys_board_init(void);

/**
 * @brief Queues a RUN_ALL of the profile's record on the router: a failing frame is reported, the next ones run. No
 * record: nothing is queued. Boot, once the router runs and the board's own devices (its GPIO) exist.
 * @return NULL when queued or there is no record; the error of queueing it
 */
MUST_USE err_h sys_board_boot(void);

#ifdef __cplusplus
}
#endif
