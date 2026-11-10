#pragma once

#include "errors.h"
#include "sys_data_provider_config.h"
#include "sys_data_provider_errors.h"
#include "utils.h"
#include <stdbool.h>
#include <stdint.h>

/**
 * @file sys_data_provider.h
 * @brief Providers (SYS_DATA_PROVIDER.MD): the ways bytes go out of the board and come in (a BLE characteristic, a
 * UART port). Code knows a provider by its id: one user opens it, sends on it, gets what comes in and closes it. The
 * transport is its driver's (sys_data_provider_driver.h).
 * @usage ERR_TRY(sys_data_provider_open(id, on_rx, ctx));
 *        ERR_TRY(sys_data_provider_send(id, frame, len, 0));
 */

#ifdef __cplusplus
extern "C" {
#endif

#define SYS_DATA_PROVIDER_SEND_URGENT (1u << 0)  /**< Send flag: to the front of the driver's queue */
#define SYS_DATA_PROVIDER_SEND_NOWAIT (1u << 1)  /**< Send flag: only queue; a full queue is ERR_PROV_BUSY at once */

/**
 * @brief What came in, or a change: the user's, on the driver's RX task. Copy what you keep and return: never block,
 * and send only with SYS_DATA_PROVIDER_SEND_NOWAIT.
 * @param ctx   The user's, from sys_data_provider_open().
 * @param id    The provider.
 * @param data  One whole frame, valid for the call only; NULL: it went up or down.
 * @param len   Bytes in `data`; with `data` NULL 1: up, 0: down.
 */
typedef void (*sys_data_provider_rx_f)(void* ctx, uint8_t id, const uint8_t* data, uint32_t len);
  
/**
 * @brief Takes a provider's rx hook: its one owner (a session, an IN stream's binding, the command binding) gets what comes
 * in, and the provider can't be removed until it is closed. Sending needs no open: see sys_data_provider_send().
 * @param[in] id  The provider.
 * @param[in] rx  What comes in and what changes goes there; NULL: send only.
 * @param[in] ctx Given to `rx`.
 * @return NULL when open; ERR_PROV_NOT_FOUND, ERR_PROV_IN_USE (the hook has an owner), ERR_PROV_NO_RX (`rx` given, and this
 * provider delivers to no hook; NULL takes it all the same: its owner reads from its driver).
 */
MUST_USE err_h sys_data_provider_open(uint8_t id, sys_data_provider_rx_f rx, void* ctx);

/**
 * @brief Gives the rx hook back: what comes in is dropped.
 * @param[in] id The provider.
 * @return NULL when closed or not open; ERR_PROV_NOT_FOUND.
 */
MUST_USE err_h sys_data_provider_close(uint8_t id);

/**
 * @brief Sends one whole frame on a provider, by its id alone: into the driver's queue. Whoever knows the id may send;
 * bytes sent on the line of a session are the sender's fault (the convention: bind and send only what the line carries,
 * the app offers no provider a session has). Without NOWAIT it waits for
 * room there, at most the driver's own bound: not from a driver's RX task. With NOWAIT it only queues: any task, a
 * driver's RX task too (the session answers a refused request from there).
 * @param[in] id    The provider.
 * @param[in] data  The frame.
 * @param[in] len   Bytes in `data`, at most CMD_FRAME_MAX.
 * @param[in] flags SYS_DATA_PROVIDER_SEND_URGENT, SYS_DATA_PROVIDER_SEND_NOWAIT, or 0.
 * @return NULL when queued; ERR_PROV_NOT_FOUND, ERR_PROV_DOWN, ERR_PROV_TOO_LONG (past CMD_FRAME_MAX: not sent, never
 * cut), ERR_PROV_BUSY (the driver's queue full: at once with NOWAIT, else at its bound), or the driver's error wrapped
 * in ERR_PROV_SEND (a raw provider's link can't take it).
 */
MUST_USE err_h sys_data_provider_send(uint8_t id, const uint8_t* data, uint32_t len, uint8_t flags);

/**
 * @brief The provider's state (SYS_DATA_PROVIDER_STATE_*). Any task.
 * @param[in] id The provider.
 * @return Its state; 0 when it does not exist.
 */
uint8_t sys_data_provider_state(uint8_t id);

/**
 * @brief Binds a provider to the router (SYS_DATA_PROVIDER.MD "Command binding"): the binding is its one user, each
 * message that comes in is one command, [key][dir][code | id + op][args] with no header, run with src ROUTER_SRC_NONE,
 * and its answer goes back on the provider as [status][data] (router_status_e; on ERROR the error record). This board
 * only: nothing is forwarded. For a client outside runIT (a script, Node-RED) that needs no session.
 * @param[in] id The provider.
 * @return NULL when bound; ERR_PROV_NOT_FOUND, ERR_PROV_IN_USE (the rx hook has an owner: a session, an IN stream, this binding).
 */
MUST_USE err_h sys_data_provider_bind_commands(uint8_t id);

/**
 * @brief Ends a provider's command binding and closes it; an answer still on its way is dropped.
 * @param[in] id The provider.
 * @return NULL when unbound or not bound; the close error.
 */
MUST_USE err_h sys_data_provider_unbind_commands(uint8_t id);

/**
 * @brief Whether the provider is up (SYS_DATA_PROVIDER_STATE_UP): it delivers frames now. Any task.
 * @param[in] id The provider
 * @return true while up
 */
static inline bool sys_data_provider_is_up(uint8_t id) {
  return (sys_data_provider_state(id) & SYS_DATA_PROVIDER_STATE_UP) != 0;
}

/**
 * @brief Whether its driver carries a whole frame (SYS_DATA_PROVIDER_STATE_FRAMED); else it is raw. Any task.
 * @param[in] id The provider
 * @return true for a framed driver
 */
static inline bool sys_data_provider_is_framed(uint8_t id) {
  return (sys_data_provider_state(id) & SYS_DATA_PROVIDER_STATE_FRAMED) != 0;
}

/**
 * @brief Whether the provider's link is a cable (SYS_DATA_PROVIDER_STATE_WIRED): about a millisecond a round trip.
 * @param[in] id The provider
 * @return true for a cable
 */
static inline bool sys_data_provider_is_wired(uint8_t id) {
  return (sys_data_provider_state(id) & SYS_DATA_PROVIDER_STATE_WIRED) != 0;
}

#ifdef __cplusplus
}
#endif


