#pragma once

#include <stdint.h>

#include "sdkconfig.h"
#include "sys_stream_router.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Between sys_stream.c and sys_stream_router.c only: what the remote interface does. Not on the public include path. */

typedef struct {
  sys_stream_head_t head;
  sys_stream_target_t targets[CONFIG_SYS_STREAM_TARGETS];
} sys_stream_info_t;

/** @brief Copies every stream and its targets into `out`; returns how many. */
uint8_t sys_stream_list(sys_stream_info_t out[CONFIG_SYS_STREAM_MAX]);

/** @brief Removes a user stream: its SOURCE providers are closed. @return ERR_STREAM_SYSTEM, ERR_STREAM_NOT_FOUND. */
MUST_USE err_h sys_stream_remove(uint8_t id);

/**
 * @brief Adds a local provider as the stream's source: its rx hook feeds the stream, which then refuses
 * sys_stream_send (ERR_STREAM_DIRECTION). Bound already: nothing changes.
 * @return NULL; ERR_STREAM_DIRECTION (a system stream), ERR_STREAM_NOT_FOUND, ERR_STREAM_TARGETS_FULL, or the
 *         provider's open error (ERR_PROV_NOT_FOUND, ERR_PROV_IN_USE).
 */
MUST_USE err_h sys_stream_source(uint8_t id, uint8_t provider);

/**
 * @brief Adds a provider of this board (`board` its id or SYS_SESSION_ID_SELF) or of another peer as a sink: the
 * payloads go there as they are (to another peer as DATA, sys_session_data). Configuration: it stays when that peer is
 * gone. There already: nothing changes.
 * @return NULL; ERR_STREAM_BAD_ARG, ERR_STREAM_NOT_FOUND, ERR_STREAM_TARGETS_FULL.
 */
MUST_USE err_h sys_stream_sink(uint8_t id, uint8_t board, uint8_t provider);

/**
 * @brief Removes the SOURCE and SINK targets of `provider` (on any board; SYS_DATA_PROVIDER_ID_NONE: all of them); a
 * SOURCE gives its provider's rx hook back.
 * @return NULL; ERR_STREAM_NOT_FOUND.
 */
MUST_USE err_h sys_stream_unbind(uint8_t id, uint8_t provider);

/** @brief Subscribes a peer (SUBSCRIBE's src) until UNSUBSCRIBE or it is gone. @return ERR_STREAM_BAD_ARG, _NOT_FOUND,
 *  _TARGETS_FULL. */
MUST_USE err_h sys_stream_subscribe(uint8_t id, uint8_t peer);

/** @brief Ends a peer's subscription. @return NULL when ended or not subscribed; ERR_STREAM_NOT_FOUND. */
MUST_USE err_h sys_stream_unsubscribe(uint8_t id, uint8_t peer);

#ifdef __cplusplus
}
#endif
