#pragma once

#include "cmd.h"
#include "errors.h"
#include "sys_data_provider.h"
#include "sys_stream_config.h"
#include "sys_stream_errors.h"

/**
 * @file sys_stream.h
 * @brief Streams (SYS_STREAM.MD): named streams between the board's code and the outside (logs, errors, telemetry,
 * events, a user stream, a bridge). A stream is an id, one byte, and a direction set at create: OUT, the board sends
 * to it; IN, what comes in on its providers. What goes in goes out to every target of its one list: providers (the
 * payload as is: an OUT stream's bindings, an IN stream's outputs), subscribed peers ([stream id][payload] in a PUSH frame of their session) and callbacks
 * (the board's code). A stream keeps nothing: what a consumer keeps is its own. Commands do not go through streams
 * (sys_session.h).
 * @usage ERR_TRY(sys_stream_bind(id, provider));
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
 * @brief Starts the stream task and listens for gone peers. Makes no stream: the app layer creates the board's own and
 * marks them (sys_stream_mark_system). Boot step, after the session.
 * @return NULL when started; the session's error.
 */
MUST_USE err_h sys_stream_init(void);

/**
 * @brief Creates a stream: the board's own at boot (then sys_stream_mark_system), the board's code and the remote
 * interface (CREATE).
 * @param cfg  What it is.
 * @return NULL when created; ERR_STREAM_BAD_ARG (id 0 or from 0x80, a bad direction or flag), ERR_STREAM_EXISTS,
 * ERR_STREAM_FULL (CONFIG_SYS_STREAM_MAX).
 */
MUST_USE err_h sys_stream_create(const sys_stream_cfg_t* cfg);

/**
 * @brief Every stream that exists now is the board's own (state SYSTEM): REMOVE refuses it. Boot, once the app layer
 * has made the board's streams.
 */
void sys_stream_mark_system(void);

/**
 * @brief Removes a user stream: its targets end, its bindings' providers are closed. ERR_STREAM_SYSTEM for a system
 * one.
 */
MUST_USE err_h sys_stream_remove(uint8_t id);

/**
 * @brief Binds a stream to a provider: an OUT stream sends its payloads there as they are (it only names the provider:
 * whoever binds answers for what goes there; a provider that is not there counts in the target's dropped), an IN stream
 * takes what comes in there and holds the provider's rx hook. Bound already: nothing changes.
 * @param id        The stream.
 * @param provider  The provider.
 * @return NULL when bound; ERR_STREAM_NOT_FOUND, ERR_STREAM_TARGETS_FULL; an IN stream also the provider's open error
 * (ERR_PROV_NOT_FOUND, ERR_PROV_IN_USE).
 */
MUST_USE err_h sys_stream_bind(uint8_t id, uint8_t provider);

/**
 * @brief Adds a way out to an IN stream: what comes in goes to this provider as is, so a provider's bytes reach another
 * one (a tunnel is two IN streams, one each way). It only names the provider, as an OUT stream's binding does.
 * @param id        The IN stream.
 * @param provider  The provider.
 * @return NULL when added or there already; ERR_STREAM_NOT_FOUND, ERR_STREAM_DIRECTION (an OUT stream),
 * ERR_STREAM_TARGETS_FULL.
 */
MUST_USE err_h sys_stream_output(uint8_t id, uint8_t provider);

/**
 * @brief Adds a provider of another board as a target, or a client's receiver: each message goes there in a data frame,
 * and that board sends it on the provider without a header. It is configuration: it stays when that peer is gone, and
 * what can't reach it counts in the target's dropped. The message must fit: at most CMD_DATA_MAX - 3.
 * @param id        The stream, OUT or IN.
 * @param board     The board or client: not this board (its providers are bindings and outputs).
 * @param provider  The provider's id on that board.
 * @return NULL when added or there already; ERR_STREAM_NOT_FOUND, ERR_STREAM_BAD_ARG, ERR_STREAM_TARGETS_FULL.
 */
MUST_USE err_h sys_stream_remote(uint8_t id, uint8_t board, uint8_t provider);

/**
 * @brief Removes a binding, an output or a remote target (on any board); an IN stream's binding gives its provider's rx hook back (NULL when it was not bound).
 * @param id        The stream.
 * @param provider  The provider.
 * @return NULL; ERR_STREAM_NOT_FOUND.
 */
MUST_USE err_h sys_stream_unbind(uint8_t id, uint8_t provider);

/**
 * @brief Removes every binding, output and remote target of the stream, as sys_stream_unbind() does.
 * @param id  The stream.
 * @return NULL; ERR_STREAM_NOT_FOUND.
 */
MUST_USE err_h sys_stream_unbind_all(uint8_t id);

/**
 * @brief Subscribes a peer to a stream: each of its messages goes to the peer as a PUSH frame, [stream id][payload]
 * (sys_session_push). Again for the same peer: one subscription. Until UNSUBSCRIBE or the peer is gone.
 * @param id    The stream.
 * @param peer  A peer id (SUBSCRIBE: the command's src).
 * @return NULL when subscribed; ERR_STREAM_NOT_FOUND, ERR_STREAM_BAD_ARG (SYS_SESSION_ID_SELF, NONE or BROADCAST),
 * ERR_STREAM_TARGETS_FULL.
 */
MUST_USE err_h sys_stream_subscribe(uint8_t id, uint8_t peer);

/**
 * @brief Ends a peer's subscription.
 * @param id    The stream.
 * @param peer  The peer.
 * @return NULL when ended or not subscribed; ERR_STREAM_NOT_FOUND.
 */
MUST_USE err_h sys_stream_unsubscribe(uint8_t id, uint8_t peer);

/**
 * @brief Adds a callback of the board's code as a target: it gets each message on the stream task (the VM's input
 * slots, a C module). The same fn and ctx again: one target.
 * @param id   The stream.
 * @param fn   The callback.
 * @param ctx  Given to `fn`.
 * @return NULL when added; ERR_STREAM_NOT_FOUND, ERR_STREAM_BAD_ARG (fn NULL), ERR_STREAM_TARGETS_FULL.
 */
MUST_USE err_h sys_stream_listen(uint8_t id, sys_stream_callback_f fn, void* ctx);

/**
 * @brief Removes a callback added with sys_stream_listen(). It may still get a message being delivered now.
 * @return NULL when removed or not there; ERR_STREAM_NOT_FOUND.
 */
MUST_USE err_h sys_stream_unlisten(uint8_t id, sys_stream_callback_f fn, void* ctx);

/**
 * @brief Queues a payload for every target of an OUT stream. Never blocks, any task.
 * @param id    The stream.
 * @param data  The payload.
 * @param len   Bytes in `data`.
 * @return NULL when queued, or dropped without a target; ERR_STREAM_NOT_FOUND, ERR_STREAM_DIRECTION (an IN stream),
 * ERR_STREAM_TOO_LONG (longer than SYS_STREAM_PAYLOAD_MAX), ERR_STREAM_QUEUE_FULL.
 */
MUST_USE err_h sys_stream_send(uint8_t id, const void* data, uint32_t len);

/** @brief True when the caller runs on the stream task (the log hook skips its lines: they would loop). */
bool sys_stream_is_task(void);

#ifdef __cplusplus
}
#endif
