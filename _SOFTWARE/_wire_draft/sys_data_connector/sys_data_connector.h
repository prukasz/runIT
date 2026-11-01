#pragma once

#include "errors.h"
#include "sys_data_connector_config.h"
#include "sys_data_connector_errors.h"
#include "sys_data_provider.h"

/**
 * @file sys_data_connector.h
 * @brief Connectors (SYS_DATA_CONNECTOR.MD): named streams between the board's code and the outside (logs, errors,
 * telemetry, events, a VM stream, a bridge). Code knows a connector by its id, one byte: it sends to it and reads
 * from it. Its targets are bindings to providers (the payload as is; rx, tx or both) and subscribed peers (the app,
 * another board: [connector][payload] in a session's stream frame). Commands do not go through connectors
 * (sys_session.h).
 * @usage ERR_TRY(sys_data_connector_bind(id, provider, SYS_DATA_CONNECTOR_BIND_TX));
 *        ERR_TRY(sys_data_connector_send(id, payload, len));
 */

#ifdef __cplusplus
extern "C" {
#endif

/** @brief What a read took out, besides its bytes. READ sends it as is: packed, 14 bytes, no padding. */
typedef struct __attribute__((packed)) {
  uint32_t seq;                 /**< Messages come in so far, this one counted */
  uint16_t len;                 /**< Bytes of this one in the reader's buffer */
  uint16_t missed;              /**< FIFO: lost to a full ring since the last read; LATEST: overwritten unread */
  uint8_t cut;                  /**< 1 = longer than the reader's buffer: cut to it */
  sys_data_provider_id_t from;  /**< The provider it came in on */
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
 * @brief Binds a connector to a provider and opens it (the binding is the provider's one user), or changes that
 * binding's flags. Its frames go there as they are.
 * @param id        The connector.
 * @param provider  The provider.
 * @param flags     SYS_DATA_CONNECTOR_BIND_RX / _TX, at least one.
 * @return NULL when bound; ERR_CONN_NOT_FOUND, ERR_CONN_BAD_ARG, ERR_CONN_TARGETS_FULL, or the provider's open error
 * (ERR_PROV_NOT_FOUND, ERR_PROV_IN_USE).
 */
MUST_USE err_h sys_data_connector_bind(uint8_t id, sys_data_provider_id_t provider, uint8_t flags);

/**
 * @brief Subscribes a peer to a connector: each of its frames goes to the peer as a stream frame, [connector][payload]
 * (sys_session_stream). Again for the same peer: one subscription. Until UNSUBSCRIBE or the peer is gone.
 * @param id    The connector.
 * @param peer  A board or client id (SUBSCRIBE: the command's src).
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
 * @brief Makes a connector the mirror of a connector of another board (SYS_DATA_CONNECTOR.MD "Mirrors"): subscribes
 * this board there; what comes is kept by its rx and goes on to its tx targets. Again when the board is reached again.
 * @param id         This board's connector, with rx LATEST or FIFO.
 * @param board      The other board.
 * @param remote_id  Its connector there.
 * @return NULL when the SUBSCRIBE is sent; ERR_CONN_NOT_FOUND, ERR_CONN_NO_RX, ERR_CONN_BAD_ARG (this board), or the
 * call's error.
 */
MUST_USE err_h sys_data_connector_mirror(uint8_t id, uint8_t board, uint8_t remote_id);

/**
 * @brief Ends a connector's mirror: UNSUBSCRIBE there; the connector stays.
 * @param id  The connector.
 * @return NULL when ended or no mirror; ERR_CONN_NOT_FOUND.
 */
MUST_USE err_h sys_data_connector_unmirror(uint8_t id);

/**
 * @brief Queues a payload for every tx target of the connector (FLAG_TX_LATEST: it replaces the connector's frame not
 * sent yet). Never blocks, any task.
 * @param id    The connector.
 * @param data  The payload.
 * @param len   Bytes in `data`.
 * @return NULL when queued, or dropped when suspended or without a tx target; ERR_CONN_NOT_FOUND, ERR_CONN_TOO_LONG
 * (longer than CMD_DATA_MAX - 1, or than tx_size when coalescing), ERR_CONN_QUEUE_FULL.
 */
MUST_USE err_h sys_data_connector_send(uint8_t id, const void* data, uint32_t len);

/**
 * @brief The largest payload every tx target takes now: CMD_DATA_MAX - 1 for subscribers, the max_frame of each
 * bound provider that is up. Producers that size their frames (VM telemetry) ask per frame. No lock, any task.
 * @param id  The connector.
 * @return Bytes; 0 when it does not exist or no tx target is up.
 */
uint32_t sys_data_connector_max_payload(uint8_t id);

/**
 * @brief The count of messages come in so far: a reader that keeps the last one it saw knows a new one without
 * reading (the VM, each scan). No lock, any task.
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
 * @return NULL when set; ERR_CONN_NOT_FOUND, ERR_CONN_NO_RX.
 */
MUST_USE err_h sys_data_connector_set_notify(uint8_t id, sys_data_connector_notify_f fn, void* ctx);

/** @brief True when the caller runs on the connector task (the log hook skips its lines: they would loop). */
bool sys_data_connector_is_task(void);

#ifdef __cplusplus
}
#endif

_Static_assert(sizeof(sys_data_connector_msg_t) == 14, "READ's answer head");
