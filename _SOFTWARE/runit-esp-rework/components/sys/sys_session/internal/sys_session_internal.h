#pragma once

/*
Internal interface between sys_session.c, call/, router/, and board/. Not on the component's public include path.
*/

#include <stdbool.h>
#include <stdint.h>

#include "sdkconfig.h"
#include "sys_session.h"
#include "sys_session_router.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief The provider a frame for `dst` leaves on: the master's, else the open session with `dst`; false: no route. */
bool sys_session_route(uint8_t dst, uint8_t* provider);

/** @brief Sends one request of this board, [hdr REQUEST, seq][req], on `provider` (NOWAIT; URGENT by `flags`). */
MUST_USE err_h sys_session_send_request(uint8_t provider, uint8_t dst, uint16_t seq, cmd_in_t req, uint8_t flags);

/** @brief Copies every session out as PEERS sends it; returns how many. */
uint8_t sys_session_list(sys_session_info_t out[CONFIG_SYS_SESSION_MAX]);

/* The calls (call/): an answer for this board, the session on `provider` ended, the timer's tick. */
void sys_session_call_answer(const sys_session_hdr_t* hdr, const uint8_t* body, uint32_t len);
void sys_session_call_fail_via(uint8_t provider);
void sys_session_call_expire(uint32_t now_ms);

/* The board's identity (board/; the host tests give their own). */
uint8_t sys_session_board_load_id(void);
MUST_USE err_h sys_session_board_save_id(uint8_t id);
uint32_t sys_session_board_boot_id(void);
uint32_t sys_session_board_build_id(void);

#ifdef __cplusplus
}
#endif
