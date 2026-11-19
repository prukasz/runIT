#pragma once

/* Between sys_board.c and its router. Not on the component's public include path. */

#include "sys_nvs.h"

/** @brief The profile's JSON: the app's definition of the board; the firmware never reads it. */
sys_nvs_json_t* sys_board_json(void);
