#pragma once

/*
The board's calls to other boards (SYS_SESSION.MD "The board's own calls"), apart from sys_session.c: their slots,
their lock and the tasks that must not wait. sys_session.c gives them the answers, the sessions that end and the time.
Not on the component's public include path.
*/

#include <stdint.h>

#include "sys_session_frame.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief An answer frame for this board: it ends the call of its seq to its src (a late one is dropped). */
void sys_session_call_answer(const sys_session_hdr_t* hdr, const uint8_t* body, uint32_t len);

/** @brief Session `at` ended: the calls that went through it end with ERR_SESSION_PEER_DOWN. */
void sys_session_call_fail_via(int32_t at);

/** @brief The calls whose time is up end with ERR_SESSION_TIMEOUT. The session's timer, every tick. */
void sys_session_call_expire(uint32_t now_ms);

#ifdef __cplusplus
}
#endif
