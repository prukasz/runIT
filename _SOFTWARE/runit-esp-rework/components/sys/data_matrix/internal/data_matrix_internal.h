#pragma once

#include <stdint.h>

#include "data_matrix.h"
#include "sdkconfig.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Between data_matrix.c, data_matrix_commands.c and router/: not on the public include path. */

/** @brief The role of `provider`; DATA_MATRIX_ROLE_NONE when it has none. */
#define DATA_MATRIX_ROLE_NONE 0xFFu
uint8_t data_matrix_role_of(uint8_t provider);

/** @brief The rx hook of a COMMANDS provider (data_matrix_commands.c): one message is one command for the router. */
void data_matrix_commands_rx(void* ctx, uint8_t id, const uint8_t* data, uint32_t len);

/** @brief Copies every role out, then every route (LIST); returns how many of each. */
void data_matrix_list(data_matrix_role_args_t roles[CONFIG_DATA_MATRIX_ROLES], uint8_t* role_count,
                      data_matrix_route_t routes[CONFIG_DATA_MATRIX_ROUTES], uint8_t* route_count);

#ifdef __cplusplus
}
#endif
