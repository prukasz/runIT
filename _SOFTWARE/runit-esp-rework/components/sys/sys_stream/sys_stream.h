#pragma once

#include "cmd.h"
#include "errors.h"
#include "sys_stream_errors.h"

/**
 * @file sys_stream.h
 * @brief Streams (SYS_STREAM.MD): named streams between the board's code and the outside (logs, errors, telemetry,
 * events, a user stream, a tunnel). A stream is an id, one byte, and a single list of targets: SOURCE (takes a
 * provider's rx hook), SINK (a provider on this board or another peer where messages go as is), PEER (subscribed peer:
 * [stream id][payload] in a PUSH frame of its session), and CALLBACK (the board's code). A stream with a SOURCE reads
 * from it and refuses sys_stream_send(); a stream without a SOURCE takes sys_stream_send(). A stream keeps nothing:
 * what a consumer keeps is its own. Commands do not go through streams (sys_session.h). Sources, sinks and
 * subscriptions are the remote interface's (sys_stream_router); the board's code sends and listens.
 * @usage ERR_TRY(sys_stream_listen(id, on_message, ctx));
 *        ERR_TRY(sys_stream_send(id, payload, len));
 */

#ifdef __cplusplus
extern "C" {
#endif

/* ----------------------------------------------------------------------------
 * Wire format (never renumber; published to the app by templates/generate_enums.py)
 * ------------------------------------------------------------------------- */

/* The stream id byte, the first of a PUSH frame's body (SYS_SESSION.MD "Push frames"): 0x00 is invalid; 0x01..0x0F the
   board's own streams (created at boot by sys_log and app/runit; ERRORS and EVENTS urgent); 0x10..0x7F user streams; from
   SYS_SESSION_CHANNEL_FIRST (0x80) the modules' channels, which no stream takes. */
//#ref-defines stream @alias Streams @prefix SYS_STREAM_
#define SYS_STREAM_TELEMETRY 0x02u  //@alias Telemetry @description Telemetry: the VM's subscribed values
#define SYS_STREAM_LOGS      0x03u  //@alias Logs @description The log lines
#define SYS_STREAM_ERRORS    0x04u  //@alias Errors @description The error records (urgent)
#define SYS_STREAM_EVENTS    0x06u  //@alias Events @description The event notifications (urgent)

/* The ranges of the stream id byte. */
//#ref-defines stream_range @alias Stream ids @prefix SYS_STREAM_ID_
#define SYS_STREAM_ID_USER_FIRST 0x10u  //@alias First user stream @description User streams are 0x10..0x7F
#define SYS_STREAM_ID_LAST       0x7Fu  //@alias Last stream @description From 0x80 the byte is a module's channel

/* A stream's flags, set at create. URGENT: its frames go to the front of every provider's queue. */
//#ref-defines stream_flag @alias Stream flags @prefix SYS_STREAM_FLAG_
#define SYS_STREAM_FLAG_URGENT (1u << 0)  //@alias Urgent @description Its frames go first on every way out

/** @brief A stream's target: one list of the four kinds. */
//#ref-enum @alias Stream target
typedef enum {
  SYS_STREAM_TARGET_SOURCE = 0,    //@alias Source @description A provider the stream reads from (takes its rx hook)
  SYS_STREAM_TARGET_SINK = 1,      //@alias Sink @description A provider of this board (SYS_SESSION_ID_SELF) or another peer where messages go as is
  SYS_STREAM_TARGET_PEER = 2,      //@alias Peer @description A subscriber: [stream id][payload] in a PUSH frame of its session
  SYS_STREAM_TARGET_CALLBACK = 3,  //@alias Callback @description The board's own code (a listener), on the stream task
} sys_stream_target_e;

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
 * @brief Starts the stream task and listens for gone peers. Makes no stream: main creates the board's own. Boot step,
 * after the session.
 * @return NULL when started; the session's error.
 */
MUST_USE err_h sys_stream_init(void);

/**
 * @brief Creates a stream: the board's own at boot (ids below SYS_STREAM_ID_USER_FIRST: never removed, never take a
 * SOURCE), the board's code and the remote interface (CREATE, user ids only).
 * @param[in] cfg What it is (`id`, `flags`).
 * @return NULL when created; ERR_STREAM_BAD_ARG (id 0 or from 0x80, unknown flag), ERR_STREAM_EXISTS,
 *         ERR_STREAM_FULL (CONFIG_SYS_STREAM_MAX).
 */
MUST_USE err_h sys_stream_create(const sys_stream_cfg_t* cfg);

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
