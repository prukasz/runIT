#pragma once

/* Between sys_board.c and its router. Not on the component's public include path. */

#include "nvs_store.h"

/** @brief The profile's JSON: the app's definition of the board; the firmware never reads it. */
nvs_store_json_t* sys_board_json(void);
