#pragma once

/*
The connector's own router (router/sys_connector_router.c) and nothing else: the commands that change the
connectors at run time, and their state for INFO. Not on the component's public include path.
*/

#include "sys_connector.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Creates user connector `connector`. A system one: ERR_CONN_EXISTS; the router's answer byte:
 * ERR_CONN_BAD_ARG; no free slot (CONFIG_SYS_CONNECTOR_MAX): ERR_CONN_FULL.
 */
MUST_USE err_h sys_connector_create(uint8_t connector);

/** @brief Removes a user connector and its bindings. System: ERR_CONN_PROTECTED. */
MUST_USE err_h sys_connector_remove(uint8_t connector);

/**
 * @brief Removes a binding (NULL when it was not bound). The last binding of a
 * system connector stays: ERR_CONN_PROTECTED (bind the new one first).
 */
MUST_USE err_h sys_connector_unbind(uint8_t connector, uint8_t provider_id);

/** @brief Suspended: sends are dropped (to free a slow link); false resumes. */
MUST_USE err_h sys_connector_set_suspended(uint8_t connector, bool suspended);

/* A connector's state: the connector, its flags and bindings. */
typedef struct {
  uint8_t provider;  /**< transport_e */
  uint32_t endpoint; /**< the provider's address (BLE: characteristic UUID; UART: 0) */
} sys_connector_binding_t;

typedef struct {
  uint8_t connector;  /**< the connector: its stream byte, the first byte of its frames */
  bool system;
  bool suspended;
  uint8_t binding_count;
  sys_connector_binding_t bindings[CONFIG_SYS_CONNECTOR_BINDINGS];
} sys_connector_info_t;

/** @brief Copies every connector out (the router's INFO); returns how many. */
uint8_t sys_connector_list(sys_connector_info_t out[CONFIG_SYS_CONNECTOR_MAX]);

#ifdef __cplusplus
}
#endif
