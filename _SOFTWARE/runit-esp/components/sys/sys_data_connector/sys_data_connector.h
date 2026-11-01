#pragma once

#include "errors.h"
#include "sys_data_connector_config.h"
#include "sys_data_connector_errors.h"
#include "sys_data_provider.h"

/**
 * @file sys_data_connector.h
 * @brief Connectors (SYS_DATA_CONNECTOR.MD): named streams between the board's code and the outside (logs, errors,
 * telemetry, events, a user stream, a bridge). Code knows a connector by its id, one byte: it sends to it and reads
 * from it. Its targets are bindings to providers (the payload as is; rx, tx or both) and subscribed peers (the app,
 * another board: [connector][payload] in a session's stream frame). Commands do not go through connectors
 * (sys_session.h).
 * @usage ERR_TRY(sys_data_connector_bind(id, provider, SYS_DATA_CONNECTOR_BIND_TX));
 *        ERR_TRY(sys_data_connector_send(id, payload, len));
 */

#ifdef __cplusplus
extern "C" {
#endif

/** @brief What a connector is, set at create (CREATE takes it) and never changed. */
typedef struct __attribute__((packed)) {
  uint8_t id;        /**< Its byte: 0x01..0xFF */
  uint8_t rx;        /**< sys_data_connector_rx_e: what it keeps of what comes in */
  uint8_t flags;     /**< SYS_DATA_CONNECTOR_FLAG_* (URGENT, FORWARD) */
  uint16_t rx_size;  /**< LATEST: the largest message kept; FIFO: the ring's bytes; NONE: 0. From the pool */
} sys_data_connector_cfg_t;

/** @brief What a read took out, besides its bytes. READ sends it as is: packed, 14 bytes, no padding. */
typedef struct __attribute__((packed)) {
  uint32_t seq;                 /**< Messages come in so far, this one counted */
  uint16_t len;                 /**< Bytes of this one in the reader's buffer */
  uint16_t missed;              /**< FIFO: lost to a full ring since the last read; LATEST: overwritten unread */
  uint8_t cut;                  /**< 1 = longer than the reader's buffer or the connector's: cut to it */
  sys_data_provider_id_t from;  /**< The provider it came in on; a mirror's: link 0, channel the board */
} sys_data_connector_msg_t;

/**
 * @brief Something came in, or a provider of the connector changed: the listener's, on the connector task, after the
 * message is in the connector's buffer. It may read, send and take a lock; never wait long (the task sends every
 * stream).
 * @param ctx  The listener's, from sys_data_connector_set_notify().
 * @param id   The connector.
 * @param seq  sys_data_connector_rx_seq() now; unchanged when only a provider changed.
 */
typedef void (*sys_data_connector_notify_f)(void* ctx, uint8_t id, uint32_t seq);

/**
 * @brief Creates the system connectors (SYS_DATA_CONNECTOR_TELEMETRY, _LOGS, _ERRORS, _EVENTS, _EVENTS_ALL), starts
 * the connector task and listens for gone peers (sys_session_on_gone). Boot step, after the session; the board config
 * binds them afterwards.
 * @return NULL when started; the task's, a system connector's or the session's error.
 */
MUST_USE err_h sys_data_connector_init(void);

/**
 * @brief Creates a user connector: for the board's code (a mirror of another board's stream, a stream in) and for the
 * remote interface (CREATE).
 * @param cfg  What it is. rx_size is 0 with RX_NONE and at least 1 otherwise (LATEST: at most CMD_DATA_MAX; FIFO: more
 * than a message header).
 * @return NULL when created; ERR_CONN_BAD_ARG (0x00, a size or a flag as above), ERR_CONN_EXISTS (its byte is taken,
 * a system one's or 0x05, the watches', too), ERR_CONN_FULL (CONFIG_SYS_DATA_CONNECTOR_MAX), ERR_CONN_POOL_FULL.
 */
MUST_USE err_h sys_data_connector_create(const sys_data_connector_cfg_t* cfg);

/**
 * @brief Removes a user connector: its subscriptions end, its bindings' providers are closed, its buffers go back to
 * the pool. ERR_CONN_SYSTEM for a system one.
 */
MUST_USE err_h sys_data_connector_remove(uint8_t id);

/**
 * @brief Binds a connector to a provider and opens it (the binding is the provider's one user), or changes that
 * binding's flags. Its frames go there as they are.
 * @param id        The connector.
 * @param provider  The provider.
 * @param flags     SYS_DATA_CONNECTOR_BIND_RX / _TX, at least one. RX needs a connector that keeps what comes in
 * or forwards it.
 * @return NULL when bound; ERR_CONN_NOT_FOUND, ERR_CONN_BAD_ARG, ERR_CONN_NO_RX (RX on a connector with RX_NONE and no
 * FORWARD), ERR_CONN_TARGETS_FULL, or the provider's open error (ERR_PROV_NOT_FOUND, ERR_PROV_IN_USE).
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
 * subscribes there (SUBSCRIBE, sent again whenever that board is reached), and each frame that comes goes to `fn`, is
 * kept by the connector's rx (LATEST, FIFO; its `from` is link 0, channel the board), then goes to every target of
 * `id`. One mirror per connector; any number of this board's users share its stream, which crosses the link once.
 * Never blocks, any task.
 * @param id         The local connector.
 * @param board      The board.
 * @param remote_id  Its connector there.
 * @param fn         Told of each frame, or NULL.
 * @param ctx        Given to `fn`.
 * @return NULL when kept; ERR_CONN_NOT_FOUND, ERR_CONN_BAD_ARG (a system connector, one that is a mirror already, a
 * stream that has a mirror already, 0 or 0x05 as remote_id).
 */
MUST_USE err_h sys_data_connector_mirror(uint8_t id, uint8_t board, uint8_t remote_id, sys_data_connector_mirror_f fn,
                                         void* ctx);

/** @brief Ends a mirror: UNSUBSCRIBE there; the connector stays, as an ordinary one. NULL when it was none. */
MUST_USE err_h sys_data_connector_unmirror(uint8_t id);

/**
 * @brief Queues a payload for every tx target of the connector. Never blocks, any task.
 * @param id    The connector.
 * @param data  The payload.
 * @param len   Bytes in `data`.
 * @return NULL when queued, or dropped when suspended or without a tx target; ERR_CONN_NOT_FOUND, ERR_CONN_TOO_LONG
 * (longer than CMD_DATA_MAX - 1), ERR_CONN_QUEUE_FULL.
 */
MUST_USE err_h sys_data_connector_send(uint8_t id, const void* data, uint32_t len);

/**
 * @brief The largest payload every tx target takes now: CMD_DATA_MAX - 1, less when a tx binding whose provider is up
 * takes less (an unframed one). Producers that size their frames (VM telemetry) ask per frame. Any task.
 * @param id  The connector.
 * @return Bytes; 0 when it does not exist.
 */
uint32_t sys_data_connector_max_payload(uint8_t id);

/**
 * @brief The count of messages come in so far: a reader that keeps the last one it saw knows a new one without
 * reading (the VM, each scan). A short lock, any task.
 * @param id  The connector.
 * @return The count; 0 when none came in, it does not exist or keeps nothing.
 */
uint32_t sys_data_connector_rx_seq(uint8_t id);

/**
 * @brief Reads what came in. LATEST: copies the last message and leaves it (any number of readers, each keeps its
 * seq). FIFO: takes the oldest out (one reader). Never blocks, any task.
 * @param id   The connector.
 * @param buf  Where the bytes go.
 * @param cap  Bytes `buf` holds.
 * @param msg  Out: seq, len, missed, from, cut.
 * @return NULL, with msg->len 0 when there is nothing (FIFO empty, LATEST before the first); ERR_CONN_NOT_FOUND,
 * ERR_CONN_NO_RX.
 */
MUST_USE err_h sys_data_connector_read(uint8_t id, void* buf, uint32_t cap, sys_data_connector_msg_t* msg);

/**
 * @brief Sets the connector's one listener, replacing the one before. For the firmware's own code; the VM polls
 * sys_data_connector_rx_seq().
 * @param id   The connector.
 * @param fn   The listener; NULL: none.
 * @param ctx  Given to `fn`.
 * @return NULL when set; ERR_CONN_NOT_FOUND, ERR_CONN_NO_RX (the connector keeps nothing).
 */
MUST_USE err_h sys_data_connector_set_notify(uint8_t id, sys_data_connector_notify_f fn, void* ctx);

/** @brief True when the caller runs on the connector task (the log hook skips its lines: they would loop). */
bool sys_data_connector_is_task(void);

#ifdef __cplusplus
}
#endif

_Static_assert(sizeof(sys_data_connector_msg_t) == 14, "READ answer head");
