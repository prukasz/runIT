#pragma once

#include "errors.h"
#include "sys_data_connector_config.h"
#include "sys_data_connector_errors.h"
#include "sys_data_provider.h"

/**
 * @file sys_data_connector.h
 * @brief Connectors (SYS_DATA_CONNECTOR.MD): named streams from the board's code to the outside (logs, errors,
 * telemetry, events, a user stream). Code knows a connector by its id, one byte, and sends to it. Its targets are
 * bindings to providers (the payload as is) and subscribed peers (the app: [connector][payload] in a session's stream
 * frame). Commands do not go through connectors (sys_session.h).
 * @usage ERR_TRY(sys_data_connector_bind(id, provider, SYS_DATA_CONNECTOR_BIND_TX));
 *        ERR_TRY(sys_data_connector_send(id, payload, len));
 */

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Creates the system connectors (SYS_DATA_CONNECTOR_TELEMETRY, _LOGS, _ERRORS, _EVENTS), starts the connector
 * task and listens for gone peers (sys_session_on_gone). Boot step, after the session; the board config binds them
 * afterwards.
 * @return NULL when started; the task's, a system connector's or the session's error.
 */
MUST_USE err_h sys_data_connector_init(void);

/**
 * @brief Creates user connector `id`: for the board's code that mirrors a stream of another board, and for the remote
 * interface. 0x00: ERR_CONN_BAD_ARG; a system one, 0x05 (the watches'): ERR_CONN_EXISTS; no free slot
 * (CONFIG_SYS_DATA_CONNECTOR_MAX): ERR_CONN_FULL.
 * @param id     The new connector.
 * @param flags  SYS_DATA_CONNECTOR_FLAG_URGENT, or 0.
 * @return NULL when created.
 */
MUST_USE err_h sys_data_connector_create(uint8_t id, uint8_t flags);

/** @brief Removes a user connector, its subscriptions and its bindings (their providers closed). ERR_CONN_SYSTEM. */
MUST_USE err_h sys_data_connector_remove(uint8_t id);

/**
 * @brief Binds a connector to a provider and opens it (the binding is the provider's one user), or changes that
 * binding's flags. Its frames go there as they are.
 * @param id        The connector.
 * @param provider  The provider.
 * @param flags     SYS_DATA_CONNECTOR_BIND_TX.
 * @return NULL when bound; ERR_CONN_NOT_FOUND, ERR_CONN_BAD_ARG, ERR_CONN_TARGETS_FULL, or the provider's open error
 * (ERR_PROV_NOT_FOUND, ERR_PROV_IN_USE).
 */
MUST_USE err_h sys_data_connector_bind(uint8_t id, sys_data_provider_id_t provider, uint8_t flags);

/**
 * @brief Subscribes a peer to a connector: each of its frames goes to the peer as a stream frame, [connector][payload]
 * (sys_session_stream). Again for the same peer: one subscription. Until UNSUBSCRIBE or the peer is gone.
 * @param id    The connector.
 * @param peer  A peer id (SUBSCRIBE: the command's src).
 * @return NULL when subscribed; ERR_CONN_NOT_FOUND, ERR_CONN_BAD_ARG (SYS_SESSION_ID_SELF, NONE or BROADCAST),
 * ERR_CONN_TARGETS_FULL.
 */
MUST_USE err_h sys_data_connector_subscribe(uint8_t id, uint8_t peer);

/**
 * @brief Ends a peer's subscription.
 * @param id    The connector.
 * @param peer  The peer.
 * @return NULL when ended or not subscribed; ERR_CONN_NOT_FOUND.
 */
MUST_USE err_h sys_data_connector_unsubscribe(uint8_t id, uint8_t peer);

/**
 * @brief A frame of a mirrored connector, on the connector task, before it goes to the mirror's own targets: copy what
 * you keep and return; keep it short.
 * @param ctx      From sys_data_connector_mirror().
 * @param board    The board the frame came from.
 * @param payload  The frame less the connector byte, valid for the call only.
 * @param len      Bytes in `payload`.
 * @param missed   Frames lost on the way since the one before (the gap in the stream's seq).
 */
typedef void (*sys_data_connector_mirror_f)(void* ctx, uint8_t board, const uint8_t* payload, uint32_t len,
                                            uint16_t missed);

/**
 * @brief Makes connector `id` (created before) the mirror of connector `remote_id` on another board: this board
 * subscribes there (SUBSCRIBE, sent again whenever that board is reached), and each frame that comes goes to `fn`, then
 * to every target of `id`. One mirror per connector; any number of this board's users share its stream, which crosses
 * the link once. Never blocks, any task.
 * @param id         The local connector.
 * @param board      The board.
 * @param remote_id  Its connector there.
 * @param fn         Told of each frame, or NULL.
 * @param ctx        Given to `fn`.
 * @return NULL when kept; ERR_CONN_NOT_FOUND, ERR_CONN_BAD_ARG (a system connector, one that is a mirror already, 0 or
 * 0x05 as remote_id).
 */
MUST_USE err_h sys_data_connector_mirror(uint8_t id, uint8_t board, uint8_t remote_id, sys_data_connector_mirror_f fn,
                                         void* ctx);

/** @brief Ends a mirror: UNSUBSCRIBE there; the connector stays, as an ordinary one. NULL when it was none. */
MUST_USE err_h sys_data_connector_unmirror(uint8_t id);

/**
 * @brief Queues a payload for every target of the connector. Never blocks, any task.
 * @param id    The connector.
 * @param data  The payload.
 * @param len   Bytes in `data`.
 * @return NULL when queued, or dropped when suspended or without a target; ERR_CONN_NOT_FOUND, ERR_CONN_TOO_LONG
 * (longer than CMD_DATA_MAX - 1), ERR_CONN_QUEUE_FULL.
 */
MUST_USE err_h sys_data_connector_send(uint8_t id, const void* data, uint32_t len);

/**
 * @brief The largest payload every target takes now: CMD_DATA_MAX - 1, less when a bound provider that is up takes
 * less (an unframed one). Producers that size their frames (VM telemetry) ask per frame. Any task.
 * @param id  The connector.
 * @return Bytes; 0 when it does not exist.
 */
uint32_t sys_data_connector_max_payload(uint8_t id);

/** @brief True when the caller runs on the connector task (the log hook skips its lines: they would loop). */
bool sys_data_connector_is_task(void);

#ifdef __cplusplus
}
#endif
