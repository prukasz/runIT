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

#define CONFIG_PRIORITY_BLE_MANAGER_TASK 5
#define BLE_TASK_STACK_SIZE 4096
#define MAX_TOTAL_TX_SLOTS 24

#define BLE_GAP_APPEARANCE_GENERIC_TAG 0x0200

typedef struct sys_ble_char_node {
  sys_ble_char_cfg_t cfg;
  uint16_t val_handle;

  // RX buffer
  RingbufHandle_t rx_buff;
  SemaphoreHandle_t rx_sem;

  // TX buffers
  sys_ble_tx_slot_t tx_slots[MAX_TX_BUFFERS];
  uint8_t tx_slot_count;

  struct sys_ble_char_node* next;
} sys_ble_char_node_t;

typedef struct sys_ble_svc_node {
  sys_ble_svc_cfg_t cfg;
  sys_ble_char_node_t* chars;
  bool registered;
  struct ble_gatt_svc_def* compiled_def;  // Heap allocated for this specific service
  struct sys_ble_svc_node* next;
} sys_ble_svc_node_t;

typedef struct {
  sys_ble_tx_slot_t* slot;
  sys_ble_char_node_t* chr;
} sys_ble_active_slot_t;

typedef struct {
  sys_ble_svc_node_t* services;
  uint16_t route_masks[SYS_BLE_EVENT_MAX];
  bool initialized;
  bool driver_started;
  uint16_t conn_handle;
  bool is_connected;
  uint16_t mtu_size;
  uint32_t rx_overflow_count;

  // Flat compiled active TX slots
  sys_ble_active_slot_t tx_slots[MAX_TOTAL_TX_SLOTS];
  uint8_t tx_slot_count;

  // Pointer to compiled GATT database definitions for initial cleanup
  struct ble_gatt_svc_def* compiled_db;
} sys_ble_ctx_t;

extern sys_ble_ctx_t g_ble_ctx;
extern SemaphoreHandle_t sys_ble_mutex;
extern SemaphoreHandle_t sys_ble_tx_sem;

/* Internal helpers exported between sys_ble.c and sys_ble_stack.c */
sys_ble_char_node_t* sys_ble_find_char_by_uuid(uint16_t char_uuid);
sys_ble_svc_node_t* sys_ble_find_svc_by_uuid(uint16_t svc_uuid);
void sys_ble_rebuild_active_tx_slots(void);
void sys_ble_free_char_node(sys_ble_char_node_t* c);
void sys_ble_free_compiled_gatt_db(struct ble_gatt_svc_def* svcs);

/* Stack functions implemented in sys_ble_stack.c */
status_rep_t sys_ble_stack_init(struct ble_gatt_svc_def* svcs);
status_rep_t sys_ble_send_raw(uint16_t conn_handle, uint16_t chr_val_handle, const uint8_t* data, size_t len, bool indicate);
status_rep_t sys_ble_reconfigure_advertising(void);
status_rep_t sys_ble_advertising_init(void);
status_rep_t populate_svc_def(struct ble_gatt_svc_def* svc_def, const sys_ble_svc_node_t* s);
status_rep_t sys_ble_set_name(const char* name);


#define CHECK_BLE_CALL_X(nimble_call, R, P)                                                       \
  do {                                                                                            \
    int __rc = (nimble_call);                                                                     \
    if (__rc != 0) {                                                                              \
      ESP_LOGE(__FILE_NAME__, "%s: NimBLE call failed '%s' -> %d", __func__, #nimble_call, __rc); \
      status_rep_t __sta_err = STA_C(ERR_BLE_STACK_FAILED, OWNER, __rc, STATUS_PAYLOAD_UNKNOWN);  \
      _STA_EMIT(__sta_err, (R), (P));                                                             \
    }                                                                                             \
  } while (0)

#define CHECK_BLE_CALL_R(nimble_call) CHECK_BLE_CALL_X(nimble_call, 1, 0)
#define CHECK_BLE_CALL_RP(nimble_call) CHECK_BLE_CALL_X(nimble_call, 1, 1)

#define CHECK_BLE_CHAR_FIND_X(var, uuid, mutex_unlock, R, P)                                           \
  do {                                                                                                 \
    (var) = sys_ble_find_char_by_uuid(uuid);                                                          \
    if ((var) == NULL) {                                                                               \
      ESP_LOGE(__FILE_NAME__, "%s: Characteristic UUID 0x%04X not found", __func__, (uuid));          \
      if (mutex_unlock) R_MUTEX_UNLOCK(sys_ble_mutex);                                                \
      status_rep_t __sta_err = STA_C(ERR_NOT_FOUND, OWNER, (uuid), STATUS_PAYLOAD_BLE_CHAR);          \
      _STA_EMIT(__sta_err, (R), (P));                                                                  \
    }                                                                                                  \
  } while (0)

#define CHECK_BLE_CHAR_FIND_R(var, uuid, mutex_unlock) CHECK_BLE_CHAR_FIND_X(var, uuid, mutex_unlock, 1, 0)

#define CHECK_BLE_SVC_FIND_X(var, uuid, mutex_unlock, R, P)                                            \
  do {                                                                                                 \
    (var) = sys_ble_find_svc_by_uuid(uuid);                                                           \
    if ((var) == NULL) {                                                                               \
      ESP_LOGE(__FILE_NAME__, "%s: Service UUID 0x%04X not found", __func__, (uuid));                 \
      if (mutex_unlock) R_MUTEX_UNLOCK(sys_ble_mutex);                                                \
      status_rep_t __sta_err = STA_C(ERR_NOT_FOUND, OWNER, (uuid), STATUS_PAYLOAD_BLE_SVC);           \
      _STA_EMIT(__sta_err, (R), (P));                                                                  \
    }                                                                                                  \
  } while (0)

#define CHECK_BLE_SVC_FIND_R(var, uuid, mutex_unlock) CHECK_BLE_SVC_FIND_X(var, uuid, mutex_unlock, 1, 0)
