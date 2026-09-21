#pragma once
#include <string.h>
#include "sys_ble.h"
#include "utils.h"

/* NimBLE stack APIs */
#include "host/ble_gap.h"
#include "host/ble_gatt.h"
#include "host/ble_hs.h"
#include "host/ble_uuid.h"
#include "host/util/util.h"
#include "nimble/ble.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "nvs_flash.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#include "sys_buffers.h"
#include <sdkconfig.h>

typedef struct sys_ble_char_node {
  sys_ble_char_cfg_t cfg;
  uint16_t val_handle;
  bool     is_subscribed;

  sys_buff_t rx_buff; //size from config, no buff if 0
  sys_buff_t tx_buff; //size from config, no buff if 0

  sys_data_connector_t* linked_connectors[CONFIG_SYS_BLE_MAX_LINKED_CONNECTORS];
  uint8_t               linked_connector_count;

  bool pending_add;     // true from creation until the owning service is next successfully (re)compiled
  bool pending_remove;  // set by sys_ble_char_remove() instead of freeing, when the owning service is already live

  struct sys_ble_char_node* next;
} sys_ble_char_node_t;

typedef struct sys_ble_svc_node {
  sys_ble_svc_cfg_t cfg;
  sys_ble_char_node_t* chars;
  bool registered;
  bool dirty;                             // true when a characteristic was added/removed while registered == true; tells sync() to recompile
  struct ble_gatt_svc_def* compiled_def;  // Heap allocated for this specific service
  struct sys_ble_svc_node* next;
} sys_ble_svc_node_t;

typedef struct {
  sys_ble_svc_node_t* services;

  bool driver_started;
  uint16_t conn_handle;
  bool is_connected;
  uint16_t mtu_size;
  uint32_t rx_overflow_count;

  // Pointer to compiled GATT database definitions for initial cleanup
  struct ble_gatt_svc_def* compiled_db;
} sys_ble_ctx_t;

extern sys_ble_ctx_t g_ble_ctx;
extern SemaphoreHandle_t sys_ble_mutex;
extern SemaphoreHandle_t sys_ble_tx_sem;

/* Internal helpers exported between sys_ble.c and sys_ble_stack.c */
/* Caller holds sys_ble_mutex; shared by peer writes and RX injection. */
err_h sys_ble_rx_enqueue(sys_ble_char_node_t* c, const uint8_t* data, size_t len);
void sys_ble_free_compiled_gatt_db(struct ble_gatt_svc_def* svcs);

/* Stack functions implemented in sys_ble_stack.c */
err_h sys_ble_stack_init(struct ble_gatt_svc_def* svcs);
err_h populate_svc_def(struct ble_gatt_svc_def* svc_def, const sys_ble_svc_node_t* s);

#define CHECK_BLE_CALL(nimble_call)                                                               \
  do {                                                                                            \
    int __rc = (nimble_call);                                                                     \
    if (__rc != 0) {                                                                              \
      ESP_LOGE(__FILE_NAME__, "%s: NimBLE call failed '%s' -> %d", __func__, #nimble_call, __rc); \
      SE_RET_ERR(ERR_BLE_STACK_FAILED, __rc);                                                     \
    }                                                                                             \
  } while (0)

#define CHECK_BLE_CHAR_FIND(var, uuid, mutex_unlock)                                         \
  do {                                                                                       \
    (var) = sys_ble_find_char_by_uuid(uuid);                                                 \
    if ((var) == NULL) {                                                                     \
      ESP_LOGE(__FILE_NAME__, "%s: Characteristic UUID 0x%04X not found", __func__, (uuid)); \
      if (mutex_unlock) {                                                                    \
        R_MUTEX_UNLOCK(sys_ble_mutex);                                                       \
      }                                                                                      \
      SE_RET_ERR(ERR_BASE_NOT_FOUND, uuid);                                                \
    }                                                                                        \
  } while (0)

#define CHECK_BLE_SVC_FIND(var, uuid, mutex_unlock)                                   \
  do {                                                                                \
    (var) = sys_ble_find_svc_by_uuid(uuid);                                           \
    if ((var) == NULL) {                                                              \
      ESP_LOGE(__FILE_NAME__, "%s: Service UUID 0x%04X not found", __func__, (uuid)); \
      if (mutex_unlock) R_MUTEX_UNLOCK(sys_ble_mutex);                                \
      SE_RET_ERR(ERR_BASE_NOT_FOUND, uuid);                                           \
    }                                                                                 \
  } while (0)



