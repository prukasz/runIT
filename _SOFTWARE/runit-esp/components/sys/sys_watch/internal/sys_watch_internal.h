#pragma once

/* Between sys_watch.c, sys_watch_client.c and router/. Not on the component's public include path. */

#include "data_matrix.h"
#include "sdkconfig.h"
#include "sys_watch.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief An entry's head after the packet's header: [owner][id][status], then the read's answer or error record. */
#define SYS_WATCH_ENTRY_HEAD 3u

/**
 * @brief Adds the watch `(owner, head->id)`, or replaces it: what the one before had under way is dropped.
 * @param[in] head  id 1..255, a trigger, period >= 1.
 * @param[in] read  The command it runs: [key][CMD_DIR_READ][...], 2..SYS_WATCH_READ_MAX bytes.
 * @param[in] owner The peer its entries go to: a board or a client.
 * @return NULL; ERR_WATCH_BAD_ARG, ERR_WATCH_NOT_READ, ERR_WATCH_FULL.
 */
MUST_USE err_h sys_watch_add(const sys_watch_head_t* head, cmd_in_t read, uint8_t owner);

/** @brief Removes the watch `(owner, id)`. @return NULL; ERR_WATCH_NOT_FOUND. */
MUST_USE err_h sys_watch_remove(uint8_t owner, uint8_t id);

/** @brief Copies every watch out as LIST sends it; returns how many. */
uint8_t sys_watch_list(sys_watch_info_t out[CONFIG_SYS_WATCH_MAX]);

/** @brief WATCH, DATA_MATRIX_PORT(MODULE, MODULE_ID_WATCH): the entries out to its links (sys_watch.c), and in from
 * the boards (sys_watch_client.c). */
extern data_matrix_port_t sys_watch_port;

/** @brief Starts the cached reads: WATCH, their idle timer and session listeners. Called by sys_watch_init(). */
MUST_USE err_h sys_watch_client_init(void);

#ifdef __cplusplus
}
#endif
