#pragma once

#include "cmd.h"
#include "errors.h"
#include "sys_data_provider.h"
#include "sys_stream_config.h"
#include "sys_stream_errors.h"

/**
 * @file sys_stream.h
 * @brief Streams (SYS_STREAM.MD): named streams between the board's code and the outside (logs, errors, telemetry,
 * events, a user stream, a tunnel). A stream is an id, one byte, and a single list of targets: SOURCE (takes a
 * provider's rx hook), SINK (a provider on this board or another peer where messages go as is), PEER (subscribed peer:
 * [stream id][payload] in a PUSH frame of its session), and CALLBACK (the board's code). A stream with a SOURCE reads
 * from it and refuses sys_stream_send(); a stream without a SOURCE takes sys_stream_send(). A stream keeps nothing:
 * what a consumer keeps is its own. Commands do not go through streams (sys_session.h).
 * @usage ERR_TRY(sys_stream_sink(id, SYS_SESSION_ID_SELF, provider));
 *        ERR_TRY(sys_stream_send(id, payload, len));
 */

#ifdef __cplusplus
extern "C" {
#endif

#define SYS_STREAM_PAYLOAD_MAX (CMD_DATA_MAX - 1u)  // the largest payload: a subscriber's frame is [stream id][payload]

/** @brief What a stream is, set at create (CREATE takes it) and never changed: defined in sys_stream_router.h. */
typedef struct sys_stream_cfg sys_stream_cfg_t;

/**
 * @brief A message of the stream, on the stream task: copy what you keep and return; never wait (the task carries
 * every stream).
 * @param ctx   The callback's, from sys_stream_listen().
 * @param id    The stream.
 * @param data  The payload, valid for the call only.
 * @param len   Bytes in `data`.
 */
typedef void (*sys_stream_callback_f)(void* ctx, uint8_t id, const uint8_t* data, uint32_t len);

/**
 * @brief Starts the stream task and listens for gone peers. Makes no stream: main creates the board's own and
 * marks them (sys_stream_mark_system). Boot step, after the session.
 * @return NULL when started; the session's error.
 */
MUST_USE err_h sys_stream_init(void);

/**
 * @brief Creates a stream: the board's own at boot (then sys_stream_mark_system), the board's code and the remote
 * interface (CREATE).
 * @param[in] cfg What it is (`id`, `flags`).
 * @return NULL when created; ERR_STREAM_BAD_ARG (id 0 or from 0x80, unknown flag), ERR_STREAM_EXISTS,
 *         ERR_STREAM_FULL (CONFIG_SYS_STREAM_MAX).
 */
MUST_USE err_h sys_stream_create(const sys_stream_cfg_t* cfg);

/**
 * @brief Every stream that exists now is the board's own (state SYSTEM): REMOVE refuses it and SOURCE is rejected on
 * it. Boot, once main has made the board's streams.
 */
void sys_stream_mark_system(void);

/**
 * @brief Removes a user stream: its targets end, its SOURCE providers are closed.
 * @param[in] id The stream
 * @return ERR_STREAM_SYSTEM for a system stream, ERR_STREAM_NOT_FOUND
 */
MUST_USE err_h sys_stream_remove(uint8_t id);

/**
 * @brief Adds a local provider as a stream's source: opens its rx hook and queues incoming frames into the stream
 * (which then refuses sys_stream_send with ERR_STREAM_DIRECTION). Bound already: nothing changes. Not allowed on a
 * system stream.
 * @param[in] id       The stream.
 * @param[in] provider The provider.
 * @return NULL when bound; ERR_STREAM_NOT_FOUND, ERR_STREAM_DIRECTION (a system stream), ERR_STREAM_TARGETS_FULL; or
 *         the provider's open error (ERR_PROV_NOT_FOUND, ERR_PROV_IN_USE).
 */
MUST_USE err_h sys_stream_source(uint8_t id, uint8_t provider);

/**
 * @brief Adds a provider (on this board or another peer) as a sink: payloads go there as they are (without a stream
 * header). When `board` is this board's id or SYS_SESSION_ID_SELF, sends directly via sys_data_provider_send(); for
 * another peer, sends a DATA frame via sys_session_data() (requires len <= CMD_DATA_MAX - 3). SINK is configuration: it
 * stays when a remote peer is gone, and failed sends count in `dropped`. Already present: nothing changes.
 * @param[in] id       The stream.
 * @param[in] board    Target board or client id (this board's id or SYS_SESSION_ID_SELF for local).
 * @param[in] provider The provider's id on that board.
 * @return NULL when added or there already; ERR_STREAM_NOT_FOUND, ERR_STREAM_BAD_ARG, ERR_STREAM_TARGETS_FULL.
 */
MUST_USE err_h sys_stream_sink(uint8_t id, uint8_t board, uint8_t provider);

/**
 * @brief Removes every SOURCE and SINK target of `provider` (on any board); a SOURCE gives its provider's rx hook back.
 * @param[in] id       The stream.
 * @param[in] provider The provider.
 * @return NULL; ERR_STREAM_NOT_FOUND.
 */
MUST_USE err_h sys_stream_unbind(uint8_t id, uint8_t provider);

/**
 * @brief Removes every SOURCE and SINK target of the stream, closing all SOURCE providers.
 * @param[in] id The stream.
 * @return NULL; ERR_STREAM_NOT_FOUND.
 */
MUST_USE err_h sys_stream_unbind_all(uint8_t id);

/**
 * @brief Subscribes a peer to a stream: each of its messages goes to the peer as a PUSH frame, [stream id][payload]
 * (sys_session_push). Again for the same peer: one subscription. Until UNSUBSCRIBE or the peer is gone.
 * @param[in] id   The stream.
 * @param[in] peer A peer id (SUBSCRIBE: the command's src).
 * @return NULL when subscribed; ERR_STREAM_NOT_FOUND, ERR_STREAM_BAD_ARG (SYS_SESSION_ID_SELF, NONE or BROADCAST),
 *         ERR_STREAM_TARGETS_FULL.
 */
MUST_USE err_h sys_stream_subscribe(uint8_t id, uint8_t peer);

/**
 * @brief Ends a peer's subscription.
 * @param[in] id   The stream.
 * @param[in] peer The peer.
 * @return NULL when ended or not subscribed; ERR_STREAM_NOT_FOUND.
 */
MUST_USE err_h sys_stream_unsubscribe(uint8_t id, uint8_t peer);

/**
 * @brief Adds a callback of the board's code as a target: it gets each message on the stream task (the VM's input
 * slots, a C module). The same fn and ctx again: one target.
 * @param[in] id  The stream.
 * @param[in] fn  The callback.
 * @param[in] ctx Given to `fn`.
 * @return NULL when added; ERR_STREAM_NOT_FOUND, ERR_STREAM_BAD_ARG (fn NULL), ERR_STREAM_TARGETS_FULL.
 */
MUST_USE err_h sys_stream_listen(uint8_t id, sys_stream_callback_f fn, void* ctx);

/**
 * @brief Removes a callback added with sys_stream_listen(). It may still get a message being delivered now.
 * @param[in] id  The stream
 * @param[in] fn  The callback
 * @param[in] ctx The callback's context
 * @return NULL when removed or not there; ERR_STREAM_NOT_FOUND.
 */
MUST_USE err_h sys_stream_unlisten(uint8_t id, sys_stream_callback_f fn, void* ctx);

/**
 * @brief Queues a payload for every target of a stream with no SOURCE. Never blocks, any task.
 * @param[in] id   The stream.
 * @param[in] data The payload.
 * @param[in] len  Bytes in `data`.
 * @return NULL when queued, or dropped without a target; ERR_STREAM_NOT_FOUND, ERR_STREAM_DIRECTION (has a SOURCE),
 *         ERR_STREAM_TOO_LONG (longer than SYS_STREAM_PAYLOAD_MAX), ERR_STREAM_QUEUE_FULL.
 */
MUST_USE err_h sys_stream_send(uint8_t id, const void* data, uint32_t len);

/**
 * @brief Whether the caller runs on the stream task (the log hook skips its lines: they would loop).
 * @return true on the stream task
 */
bool sys_stream_is_task(void);

#ifdef __cplusplus
}
#endif
