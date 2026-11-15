#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "cmd.h"
#include "data_matrix_errors.h"
#include "errors.h"

/**
 * @file data_matrix.h
 * @brief The data matrix (DATA_MATRIX.MD): where every byte that is not a command goes, set from one place. A provider
 * has a **role** (what it carries); a **route** says that what comes out of one **end** goes into another. The logs to a
 * BLE characteristic, a slave's telemetry to the app, a bridge between two providers, a watch's entries to the session
 * that asked for them are routes of one table, written in one format, listed by one command and replayed at boot. The
 * module is a table and the delivery along it: it knows no protocol (the session's hello, calls and requests stay in
 * sys_session, the commands in the router) and keeps no state of its own.
 *
 * A provider is made a session, a raw port or a command port by its role; there is no other way to give it one.
 *
 *   ends          P(provider)          the raw bytes of a provider
 *                 T(provider, tag)     tagged data: on a provider with a session a PUSH [tag][data], on a raw one the
 *                                      data as they are (the tag is not sent). As a source, `provider` may be
 *                                      DATA_MATRIX_ANY: that tag from every session
 *                 S(stream)            a stream: what the board's code sends with data_matrix_send(stream, ...)
 *                 C(callback)          the board's code: data_matrix_listen(); never on the wire
 *
 *   route         from -> to
 *                         to P   to T   to S   to C
 *                 from P   yes    yes    yes    yes     a RAW provider's rx hook
 *                 from T   yes    yes    yes    yes     a SESSION provider's PUSH with that tag
 *                 from S   yes    yes     -     yes     what the code sent
 *                 Every end on a provider needs its role: P the role RAW, T as a source SESSION, T as a target SESSION
 *                 or RAW. A provider with no role is not attached: nothing is done with its data.
 *
 * Several routes may leave one end, and several may arrive at one: the data are copied to each, NOWAIT, never blocked
 * on. What a target does not take (a busy or down provider, a session not open) is lost without a word, as the streams'
 * always were.
 * @usage ERR_TRY(data_matrix_set_role(&(data_matrix_role_args_t){.provider = BLE_LOGS, .role = DATA_MATRIX_ROLE_RAW}));
 *        ERR_TRY(data_matrix_route(&(data_matrix_route_t){DATA_MATRIX_END_STREAM(LOG_STREAM_LOGS),
 *                                                          DATA_MATRIX_END_PROVIDER(BLE_LOGS)}));
 *        ERR_TRY(data_matrix_send(LOG_STREAM_LOGS, line, len));
 */

#ifdef __cplusplus
extern "C" {
#endif

/* ----------------------------------------------------------------------------
 * Wire format (never renumber; published to the app by templates/generate_enums.py)
 * ------------------------------------------------------------------------- */

/** @brief What a provider carries: set with ROLE, until it is cleared. A provider with no role is not attached: what
 * comes in on it is dropped and no route may have it. */
//#ref-enum @alias Provider role
typedef enum {
  DATA_MATRIX_ROLE_RAW = 0,       //@alias Raw @description Its bytes are sent and taken as they are, no frame and no tag; routes from and to P, to T
  DATA_MATRIX_ROLE_SESSION = 1,   //@alias Session @description A session runs on it (sys_session): REQUEST, ANSWER and CONTROL are the session's, PUSH is T
  DATA_MATRIX_ROLE_COMMANDS = 2,  //@alias Commands @description Each message in is one command for the router, its answer goes back on the provider
} data_matrix_role_e;

/** @brief What an end is. */
//#ref-enum @alias End kind
typedef enum {
  DATA_MATRIX_END_P = 0,  //@alias Provider @description a = the provider; b unused
  DATA_MATRIX_END_T = 1,  //@alias Tagged @description a = the provider, b = the tag (a stream id, or a module's channel from 0x80)
  DATA_MATRIX_END_S = 2,  //@alias Stream @description a = the stream id (1..0x7F); b unused
  DATA_MATRIX_END_C = 3,  //@alias Callback @description a = what data_matrix_listen() gave; never made by a command
} data_matrix_end_kind_e;

/* The provider byte of a T source that means every session; the streams' ids. */
//#ref-defines data_matrix_const @alias Data matrix @prefix DATA_MATRIX_
#define DATA_MATRIX_ANY         0xFFu  //@alias Every session @description A T source's provider: that tag from the session of any provider
#define DATA_MATRIX_STREAM_LAST 0x7Fu  //@alias Last stream @description Streams are 0x01..0x7F; from 0x80 a tag is a module's channel
#define DATA_MATRIX_URGENT      0x01u  //@alias Urgent @description A stream's flag: its frames go to the front of every provider's queue

/** @brief An end: 3 bytes. */
typedef struct __attribute__((packed)) {
  uint8_t kind;  /**< data_matrix_end_kind_e */
  uint8_t a;
  uint8_t b;
} data_matrix_end_t;

/** @brief A route: what leaves `from` enters `to`. 6 bytes. */
typedef struct __attribute__((packed)) {
  data_matrix_end_t from;
  data_matrix_end_t to;
} data_matrix_route_t;

#define DATA_MATRIX_END_PROVIDER(provider)    ((data_matrix_end_t){DATA_MATRIX_END_P, (provider), 0})
#define DATA_MATRIX_END_TAGGED(provider, tag) ((data_matrix_end_t){DATA_MATRIX_END_T, (provider), (tag)})
#define DATA_MATRIX_END_STREAM(stream)        ((data_matrix_end_t){DATA_MATRIX_END_S, (stream), 0})

/** @brief ROLE's frame: a provider and what it carries. `deadman_ms` and `flags` are the SESSION's (sys_session.h), 0
 * for the other roles. */
typedef struct __attribute__((packed)) {
  uint8_t provider;
  uint8_t role;         /**< data_matrix_role_e */
  uint16_t deadman_ms;  /**< SESSION: 0 no dead-man, else the silence that ends it */
  uint8_t flags;        /**< SESSION: SYS_SESSION_SAFETY; else 0 */
} data_matrix_role_args_t;

_Static_assert(sizeof(data_matrix_end_t) == 3 && sizeof(data_matrix_route_t) == 6 &&
                   sizeof(data_matrix_role_args_t) == 5,
               "the matrix's frames");
_Static_assert(DATA_MATRIX_URGENT == CMD_SEND_URGENT, "a stream's urgent flag is the providers'");

/* The commands (router/data_matrix_router.h, root key MODULE_ID_DATA_MATRIX 0x30), all W but LIST:
 *   ROLE       data_matrix_role_args_t        a provider is RAW, a SESSION or a command port (it has none until then);
 *                                             the same again: nothing changes; another role: an error, CLEAR_ROLE first
 *   CLEAR_ROLE {u8 provider}                  the provider's routes go with its role; a session ends
 *   ROUTE      data_matrix_route_t            there already: nothing changes
 *   UNROUTE    data_matrix_route_t            not there: nothing changes
 *   CLEAR      data_matrix_end_t              every route that has the end, on either side
 *   LIST   R   -> u8 roles, data_matrix_role_args_t x roles, u8 routes, data_matrix_route_t x routes
 * They are frames of the board's record: ROLE first, then the ROUTEs, replayed at every start; the same again is done,
 * so a record that sets a role runs at every start without an error.
 */

/* ----------------------------------------------------------------------------
 * The streams a module has (the standard: DATA_MATRIX.MD "Streams of a module")
 * ------------------------------------------------------------------------- */

/** @brief A stream of a module: its id and flags (DATA_MATRIX_URGENT). */
typedef struct {
  uint8_t id;
  uint8_t flags;
} data_matrix_stream_t;

/*
 * A module that sends on streams declares them in a table of its own, like its events and errors:
 *   #define LOG_STREAMS(X, P)                                       \
 *     X(P, LOGS,   0x03, 0,                  "the log lines, text")  \
 *     X(P, ERRORS, 0x04, DATA_MATRIX_URGENT, "the error records")
 *   DATA_MATRIX_STREAMS_DECLARE(LOG)   // the header: LOG_STREAM_LOGS = 0x03, LOG_STREAM_ERRORS = 0x04, LOG_STREAMS_COUNT
 *   DATA_MATRIX_STREAMS_DEFINE(LOG)    // one .c: the table LOG_streams, and the claim of each id
 * and gives the matrix its flags at boot: data_matrix_streams(LOG_streams, LOG_STREAMS_COUNT). Two modules with one id
 * do not link: each id is a symbol of its own. The id is written with two hex digits.
 */
#define DM_STREAM_ENUM_(P, NAME, id, flags, desc) P##_STREAM_##NAME = (id),
#define DM_STREAM_ONE_(P, NAME, id, flags, desc)  +1
#define DM_STREAM_ROW_(P, NAME, id, flags, desc)  {(id), (flags)},
#define DM_STREAM_CLAIM_(P, NAME, id, flags, desc) const uint8_t data_matrix_stream_claim_##id = 0;

#define DATA_MATRIX_STREAMS_DECLARE(P)                                \
  enum { P##_STREAMS(DM_STREAM_ENUM_, P) };                           \
  enum { P##_STREAMS_COUNT = 0 P##_STREAMS(DM_STREAM_ONE_, P) };      \
  extern const data_matrix_stream_t P##_streams[P##_STREAMS_COUNT];

#define DATA_MATRIX_STREAMS_DEFINE(P)                                 \
  const data_matrix_stream_t P##_streams[P##_STREAMS_COUNT] = {P##_STREAMS(DM_STREAM_ROW_, P)}; \
  P##_STREAMS(DM_STREAM_CLAIM_, P)

/* ----------------------------------------------------------------------------
 * The board's code
 * ------------------------------------------------------------------------- */

/**
 * @brief A callback end: `fn(ctx, from, data, len)` gets what a route brings to it, on the task that delivers (the
 * provider's rx task for P and T, the matrix's task for S). It copies what it keeps and returns; never blocks. `from` is
 * what a consumer that answers needs (a watch's peer, the remote devices'); one that only takes the data ignores it.
 * @param ctx  Given to listen
 * @param from The end the data came out of: P(provider), T(provider, tag) or S(stream)
 * @param data Valid for the call only
 * @param len  Bytes in `data`
 */
typedef void (*data_matrix_callback_f)(void* ctx, const data_matrix_end_t* from, const uint8_t* data, uint32_t len);

/**
 * @brief Starts the matrix: its task and queue (the streams' sends), and the taking of the sessions' PUSH frames. Boot,
 * after sys_session_init.
 * @return NULL when started.
 */
MUST_USE err_h data_matrix_init(void);

/**
 * @brief Tells the matrix the flags of a module's streams (its table: DATA_MATRIX_STREAMS_DEFINE). Boot, in the module's
 * init.
 * @param[in] streams The module's table
 * @param[in] count   Its rows
 * @return NULL; ERR_DATA_MATRIX_BAD_ARG (an id outside 1..DATA_MATRIX_STREAM_LAST, an unknown flag)
 */
MUST_USE err_h data_matrix_streams(const data_matrix_stream_t* streams, uint32_t count);

/**
 * @brief Makes a callback an end: route to `out` to bring it data. The same fn and ctx again: the same end.
 * @param[in]  fn  The callback
 * @param[in]  ctx Given to `fn`
 * @param[out] out The end (kind C)
 * @return NULL; ERR_DATA_MATRIX_FULL
 */
MUST_USE err_h data_matrix_listen(data_matrix_callback_f fn, void* ctx, data_matrix_end_t* out);

/**
 * @brief Sends data on a stream, for the board's code (the logs, the errors, the events): never blocks, any task; copied
 * into the queue and delivered to every route from S(stream) by the matrix's task. A stream with no route: dropped, NULL.
 * @param[in] stream 1..DATA_MATRIX_STREAM_LAST
 * @param[in] data   The payload, at most CMD_DATA_MAX - 1
 * @param[in] len    Its bytes
 * @return NULL; ERR_DATA_MATRIX_BAD_ARG, ERR_DATA_MATRIX_TOO_LONG, ERR_DATA_MATRIX_QUEUE_FULL (callers on log and error
 *         paths drop silently)
 */
MUST_USE err_h data_matrix_send(uint8_t stream, const void* data, uint32_t len);

/**
 * @brief Sends data to an end with no route: for the board's code that answers a given peer (a watch's entry to the
 * session that asked for it). T on a provider with a session is a PUSH, on a raw one the data as they are; P raw bytes
 * (RAW only); S the stream's routes. NOWAIT, any task; what the end does not take is lost.
 * @param[in] to    P, T or S
 * @param[in] data  The payload, at most CMD_DATA_MAX - 1
 * @param[in] len   Its bytes
 * @param[in] flags DATA_MATRIX_URGENT (to the front of the provider's queue), or 0
 * @return NULL; ERR_DATA_MATRIX_PAIR (a kind or a role that takes no data), ERR_DATA_MATRIX_TOO_LONG, or the provider's
 *         ERR_PROV_BUSY / _DOWN, the session's ERR_SESSION_NOT_OPEN
 */
MUST_USE err_h data_matrix_send_to(const data_matrix_end_t* to, const void* data, uint32_t len, uint8_t flags);

/** @brief Whether the caller is the matrix's task (a log line made inside a delivery is not sent again). */
bool data_matrix_is_task(void);

/**
 * @brief Gives a provider a role (ROLE): SESSION is sys_session_attach, RAW and COMMANDS take the provider's rx hook.
 * @param[in] args The provider, its role, a session's dead-man and flags
 * @return NULL; ERR_DATA_MATRIX_ROLE (the provider has another role), ERR_DATA_MATRIX_BAD_ARG, ERR_DATA_MATRIX_FULL,
 *         ERR_PROV_NOT_FOUND / _IN_USE, and the role's own errors (ERR_SESSION_NOT_FRAMED for a SESSION on a raw provider)
 */
MUST_USE err_h data_matrix_set_role(const data_matrix_role_args_t* args);

/** @brief Takes the role away (CLEAR_ROLE), with the provider's routes; a session on it ends. The provider is not attached again. */
MUST_USE err_h data_matrix_clear_role(uint8_t provider);

/**
 * @brief Adds a route; the same again: nothing changes.
 * @return NULL; ERR_DATA_MATRIX_PAIR (not an allowed pair above, or a provider without the role it needs),
 *         ERR_DATA_MATRIX_FULL
 */
MUST_USE err_h data_matrix_route(const data_matrix_route_t* route);

/** @brief Removes a route; one that is not there: nothing changes. */
MUST_USE err_h data_matrix_unroute(const data_matrix_route_t* route);

/** @brief Removes every route that has `end` on either side (CLEAR). */
MUST_USE err_h data_matrix_clear(const data_matrix_end_t* end);

#ifdef __cplusplus
}
#endif
