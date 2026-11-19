#pragma once

/*
Internal interface between sys_session.c, router/, and board/. Not on the component's public include path.
*/

#include <stdbool.h>
#include <stdint.h>

#include "sdkconfig.h"
#include "sys_session.h"
#include "sys_session_router.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Sends one request of this board, [hdr REQUEST, seq][req], on `provider` (NOWAIT; URGENT by `flags`). */
MUST_USE err_h sys_session_send_request(uint8_t provider, uint16_t seq, cmd_in_t req, uint8_t flags);

/** @brief Copies every session out as PEERS sends it; returns how many. */
uint8_t sys_session_list(sys_session_info_t out[CONFIG_SYS_SESSION_MAX]);

/* The board's identity (board/; the host tests give their own). */
uint8_t sys_session_board_load_id(void);
MUST_USE err_h sys_session_board_save_id(uint8_t id);
uint32_t sys_session_board_boot_id(void);
uint32_t sys_session_board_build_id(void);

#ifdef __cplusplus
}
#endif
