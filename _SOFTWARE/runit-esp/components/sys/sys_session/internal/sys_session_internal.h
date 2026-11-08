#pragma once

/*
The session's own files and nothing else: its router (PEERS), the hello (hello/) and the calls (call/). Not on the
component's public include path. Each function takes the sessions' lock itself.
*/

#include "sdkconfig.h"
#include "sys_data_provider.h"
#include "sys_session.h"
#include "sys_session_frame.h"
#include "sys_session_router.h"
#include "sys_session_slot.h"

#ifdef __cplusplus
extern "C" {
#endif

#define HDR               ((uint32_t)sizeof(sys_session_hdr_t))                 // the header in front of every frame
#define FRAME(kind)       ((uint8_t)((kind) << 6))                              // the frame byte of a kind
#define FRAME_KIND(frame) ((uint8_t)(((frame) & SYS_SESSION_FRAME_KIND) >> 6))  // the kind of a frame byte

/** @brief The provider's send flags for a frame's flags: NOWAIT always, URGENT when the frame says so. */
static inline uint8_t sys_session_send_flags(uint8_t frame_flags) {
  return SYS_DATA_PROVIDER_SEND_NOWAIT | ((frame_flags & ROUTER_FLAG_URGENT) ? SYS_DATA_PROVIDER_SEND_URGENT : 0);
}

/* ----------------------------------------------------------------------------
 * Sessions
 * ------------------------------------------------------------------------- */

/** @brief The session a frame for `dst` leaves on, -1: no way (a slave: its master's; the master: that peer's). */
int32_t sys_session_route(uint8_t dst);

/** @brief A copy of session `at`. */
session_slot_t sys_session_slot(int32_t at);

/** @brief This board's timeout of session `at`: what its hello tells the peer. */
uint32_t sys_session_timeout(int32_t at);

/**
 * @brief Opens session `at` with what its peer's hello said, unless another open session has its id.
 * @param was_open  Out: whether it was open already.
 * @return true when open.
 */
bool sys_session_open(int32_t at, const sys_session_hello_state_t* peer, bool* was_open);

/** @brief Whether session `at` may open: the board has no master, or it is that master's session. */
bool sys_session_admits(int32_t at);

/**
 * @brief Makes session `at` (a CLIENT way a board's hello came on) this board's master's, and ends every other open
 * session: a board with a master has no other session. Its end makes it a CLIENT way again (sys_session_end).
 * @return false when the board has another master.
 */
bool sys_session_take_master(int32_t at);

/** @brief Ends session `at`: its peer is gone; a CLIENT way made MASTER is a CLIENT way again. */
void sys_session_end(int32_t at);

/** @brief Copies every session out as PEERS sends it; returns how many. */
uint8_t sys_session_list(sys_session_info_t out[CONFIG_SYS_SESSION_MAX]);

/* ----------------------------------------------------------------------------
 * Frames out
 * ------------------------------------------------------------------------- */

/** @brief A whole frame on session `at`'s provider. */
err_h sys_session_send_raw(int32_t at, const uint8_t* frame, uint32_t len, uint8_t flags);

/** @brief A control frame on session `at`: `op`, then `data`. */
err_h sys_session_send_control(int32_t at, uint8_t op, const uint8_t* data, uint32_t len);

/** @brief A broadcast frame: a slave's to its master, the master's to every slave; the first error comes back. */
err_h sys_session_send_broadcast(const uint8_t* frame, uint32_t len, uint8_t flags);

/* ----------------------------------------------------------------------------
 * Listeners
 * ------------------------------------------------------------------------- */

/** @brief Tells the reached listeners that `peer` can be reached. */
void sys_session_reached(uint8_t peer);

#ifdef __cplusplus
}
#endif
