#pragma once

#include "sys_data_connector.h"

/**
 * @file sys_data_connector_internal.h
 * @brief The connector itself and what its router (router/sys_data_connector_router.c) does with it: create, remove,
 * unbind, suspend, list. Only the router includes it; not on the component's public include path.
 */

#ifdef __cplusplus
extern "C" {
#endif

/** @brief What a connector is, set at create (CREATE takes it) and never changed. */
typedef struct __attribute__((packed)) {
  uint8_t id;        /**< Its byte: 0x01..0xFF */
  uint8_t rx;        /**< sys_data_connector_rx_e: what it keeps of what its rx bindings take */
  uint8_t flags;     /**< SYS_DATA_CONNECTOR_FLAG_* (URGENT, FORWARD, TX_LATEST) */
  uint16_t rx_size;  /**< LATEST: the largest message kept; FIFO: the ring's bytes; NONE: 0. From the pool */
  uint16_t tx_size;  /**< TX_LATEST: the largest frame kept waiting; else 0. From the pool */
} sys_data_connector_cfg_t;

/** @brief One binding: a provider, the way it carries, the frames lost on it. */
typedef struct __attribute__((packed)) {
  sys_data_provider_id_t provider;
  uint8_t flags;     /**< SYS_DATA_CONNECTOR_BIND_* */
  uint16_t dropped;  /**< Frames not sent on it (provider busy or down, frame too long); stops at 0xFFFF */
} sys_data_connector_binding_t;

/** @brief One subscriber: a peer and the frames lost on the way to it. */
typedef struct __attribute__((packed)) {
  uint8_t peer;      /**< A board or client id */
  uint16_t dropped;  /**< Frames not sent to it (no route, first hop busy or down); stops at 0xFFFF */
} sys_data_connector_subscriber_t;

/**
 * @brief A connector as LIST sends it: this head, then `subscriber_count` subscribers, then `binding_count`
 * bindings.
 */
typedef struct __attribute__((packed)) {
  sys_data_connector_cfg_t cfg;
  uint8_t state;  /**< SYS_DATA_CONNECTOR_STATE_* */
  uint8_t subscriber_count;
  uint8_t binding_count;
} sys_data_connector_head_t;

/** @brief A connector: what it is, its state, its targets. The registry's slot holds one beside what it received. */
typedef struct {
  sys_data_connector_head_t head;
  sys_data_connector_subscriber_t subscribers[CONFIG_SYS_DATA_CONNECTOR_SUBSCRIBERS];
  sys_data_connector_binding_t bindings[CONFIG_SYS_DATA_CONNECTOR_BINDINGS];
} sys_data_connector_t;

/**
 * @brief Creates a user connector.
 * @param cfg  What it is.
 * @return NULL when created; ERR_CONN_EXISTS (its byte is taken, a system one's or WATCHES too), ERR_CONN_BAD_ARG
 * (0x00, rx_size 0 with rx, tx_size 0 with TX_LATEST or set without, a size past CMD_FRAME_MAX), ERR_CONN_FULL
 * (CONFIG_SYS_DATA_CONNECTOR_MAX), ERR_CONN_POOL_FULL.
 */
MUST_USE err_h sys_data_connector_create(const sys_data_connector_cfg_t* cfg);

/**
 * @brief Removes a user connector: closes its providers, ends its subscriptions, gives its buffers back to the pool.
 * @param id  The connector.
 * @return NULL when removed; ERR_CONN_NOT_FOUND, ERR_CONN_SYSTEM (a system one).
 */
MUST_USE err_h sys_data_connector_remove(uint8_t id);

/**
 * @brief Removes a binding and closes its provider.
 * @param id        The connector.
 * @param provider  The provider.
 * @return NULL when removed or not bound; ERR_CONN_NOT_FOUND.
 */
MUST_USE err_h sys_data_connector_unbind(uint8_t id, sys_data_provider_id_t provider);

/**
 * @brief Suspends a connector (its sends are dropped, to free a slow link; what comes in is still kept) or resumes it.
 * @param id         The connector.
 * @param suspended  true suspends, false resumes.
 * @return NULL when set; ERR_CONN_NOT_FOUND.
 */
MUST_USE err_h sys_data_connector_set_suspended(uint8_t id, bool suspended);

/**
 * @brief Copies every connector out under one lock (LIST).
 * @param out  CONFIG_SYS_DATA_CONNECTOR_MAX connectors.
 * @return How many were copied.
 */
uint8_t sys_data_connector_list(sys_data_connector_t out[CONFIG_SYS_DATA_CONNECTOR_MAX]);

#ifdef __cplusplus
}
#endif
