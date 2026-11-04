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

/** @brief A provider's id: its name, kept and sent as is (records, error records, the app). */
typedef struct __attribute__((packed)) {
  uint8_t link;      /**< sys_data_provider_link_e */
  uint32_t channel;  /**< Its address on that link: BLE the characteristic's UUID, UART the port */
} sys_data_provider_id_t;

/** @brief Whether two ids name the same provider. */
static inline bool sys_data_provider_same_id(sys_data_provider_id_t a, sys_data_provider_id_t b) {
  return a.link == b.link && a.channel == b.channel;
}

#define SYS_DATA_PROVIDER_SEND_URGENT (1u << 0)  /**< Send flag: to the front of the driver's queue */
#define SYS_DATA_PROVIDER_SEND_NOWAIT (1u << 1)  /**< Send flag: only queue; a full queue is ERR_PROV_BUSY at once */

/**
 * @brief What came in, or a change: the user's, on the driver's RX task. Copy what you keep and return: never block,
 * and send only with SYS_DATA_PROVIDER_SEND_NOWAIT.
 * @param ctx   The user's, from sys_data_provider_open().
 * @param id    The provider.
 * @param data  One whole frame, valid for the call only; NULL: it went up or down.
 * @param len   Bytes in `data`; with `data` NULL its max_frame now (0: down).
 */
typedef void (*sys_data_provider_rx_f)(void* ctx, sys_data_provider_id_t id, const uint8_t* data, uint32_t len);
  
/**
 * @brief Takes a provider for one user (a session, a stream's binding, the command binding): it alone sends on it and gets what comes
 * in, and the provider can't be removed until it is closed.
 * @param id   The provider.
 * @param rx   What comes in and what changes goes there; NULL: send only.
 * @param ctx  Given to `rx`.
 * @return NULL when open; ERR_PROV_NOT_FOUND, ERR_PROV_IN_USE (another user has it).
 */
MUST_USE err_h sys_data_provider_open(sys_data_provider_id_t id, sys_data_provider_rx_f rx, void* ctx);

/**
 * @brief Gives a provider back: it is free, and what comes in is dropped.
 * @param id  The provider.
 * @return NULL when closed or not open; ERR_PROV_NOT_FOUND.
 */
MUST_USE err_h sys_data_provider_close(sys_data_provider_id_t id);

/**
 * @brief Sends one whole frame on a provider the caller opened: into the driver's queue. Without NOWAIT it waits for
 * room there, at most the driver's own bound: not from a driver's RX task. With NOWAIT it only queues: any task, a
 * driver's RX task too (the session answers a refused request from there).
 * @param id     The provider.
 * @param data   The frame.
 * @param len    Bytes in `data`, at most sys_data_provider_max_frame().
 * @param flags  SYS_DATA_PROVIDER_SEND_URGENT, SYS_DATA_PROVIDER_SEND_NOWAIT, or 0.
 * @return NULL when queued; ERR_PROV_NOT_FOUND, ERR_PROV_DOWN, ERR_PROV_TOO_LONG (not sent, never cut), ERR_PROV_BUSY
 * (the driver's queue full: at once with NOWAIT, else at its bound), or the driver's error wrapped in ERR_PROV_SEND.
 */
MUST_USE err_h sys_data_provider_send(sys_data_provider_id_t id, const uint8_t* data, uint32_t len, uint8_t flags);

/**
 * @brief The largest whole frame the provider delivers now, not its link's packet: the driver splits a frame into
 * packets (SYS_DATA_PROVIDER.MD "Frame size"). The registry's spinlock only, never the driver: any task.
 * @param id  The provider.
 * @return Bytes: CMD_FRAME_MAX for a framed one that is up; 0 when it is down or does not exist.
 */
uint32_t sys_data_provider_max_frame(sys_data_provider_id_t id);

/**
 * @brief The provider's state (SYS_DATA_PROVIDER_STATE_*). Any task.
 * @param id  The provider.
 * @return Its state; 0 when it does not exist.
 */
uint8_t sys_data_provider_state(sys_data_provider_id_t id);

/**
 * @brief Binds a provider to the router (SYS_DATA_PROVIDER.MD "Command binding"): the binding is its one user, each
 * message that comes in is one command, [key][dir][code | id + op][args] with no header, run with src ROUTER_SRC_NONE,
 * and its answer goes back on the provider as [status][data] (router_status_e; on ERROR the error record). This board
 * only: nothing is forwarded. For a client outside runIT (a script, Node-RED) that needs no session.
 * @param id  The provider.
 * @return NULL when bound; ERR_PROV_NOT_FOUND, ERR_PROV_IN_USE (another user has it: a session, a stream, this binding).
 */
MUST_USE err_h sys_data_provider_bind_commands(sys_data_provider_id_t id);

/**
 * @brief Ends a provider's command binding and closes it; an answer still on its way is dropped.
 * @param id  The provider.
 * @return NULL when unbound or not bound; the close error.
 */
MUST_USE err_h sys_data_provider_unbind_commands(sys_data_provider_id_t id);

/** @brief Whether the provider is up: it delivers frames now (its max frame is not 0). Any task. */
static inline bool sys_data_provider_is_up(sys_data_provider_id_t id) {
  return sys_data_provider_max_frame(id) > 0;
}

/** @brief Whether the provider's link is a cable (SYS_DATA_PROVIDER_STATE_WIRED): about a millisecond a round trip. */
static inline bool sys_data_provider_is_wired(sys_data_provider_id_t id) {
  return (sys_data_provider_state(id) & SYS_DATA_PROVIDER_STATE_WIRED) != 0;
}

#ifdef __cplusplus
}
#endif


