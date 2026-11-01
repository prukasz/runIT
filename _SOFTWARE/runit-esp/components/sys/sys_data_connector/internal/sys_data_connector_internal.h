#pragma once

/*
The connector's own router (router/sys_data_connector_router.c) and nothing else: the commands that change the
connectors at run time, and their state for LIST. Not on the component's public include path.
*/

#include "sdkconfig.h"
#include "sys_data_connector.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Removes a binding and closes its provider (NULL when it was not bound); provider link 0: every binding. */
MUST_USE err_h sys_data_connector_unbind(uint8_t id, sys_data_provider_id_t provider);

/** @brief Suspended: sends are dropped (to free a slow link; what comes in is still kept); false resumes. */
MUST_USE err_h sys_data_connector_set_suspended(uint8_t id, bool suspended);

/** @brief A binding as LIST sends it. */
typedef struct __attribute__((packed)) {
  sys_data_provider_id_t provider;
  uint8_t flags;     /**< SYS_DATA_CONNECTOR_BIND_* */
  uint16_t dropped;  /**< Frames not sent there: busy, down, too long; stops at 0xFFFF */
} sys_data_connector_binding_t;

/** @brief A subscriber as LIST sends it. */
typedef struct __attribute__((packed)) {
  uint8_t peer;
  uint16_t dropped;  /**< Frames not sent to it: busy, down, no route */
} sys_data_connector_subscriber_t;

/** @brief A connector's head as LIST sends it: what it is, its state; its subscribers, then its bindings follow. */
typedef struct __attribute__((packed)) {
  sys_data_connector_cfg_t cfg;
  uint8_t state;  /**< SYS_DATA_CONNECTOR_STATE_* */
  uint8_t subscribers;
  uint8_t bindings;
} sys_data_connector_head_t;

/** @brief A connector, copied out for LIST. */
typedef struct {
  sys_data_connector_head_t head;
  sys_data_connector_subscriber_t subs[CONFIG_SYS_DATA_CONNECTOR_SUBSCRIBERS];
  sys_data_connector_binding_t binds[CONFIG_SYS_DATA_CONNECTOR_BINDINGS];
} sys_data_connector_info_t;

/** @brief Copies every connector out (LIST); returns how many. */
uint8_t sys_data_connector_list(sys_data_connector_info_t out[CONFIG_SYS_DATA_CONNECTOR_MAX]);

#ifdef __cplusplus
}
#endif
