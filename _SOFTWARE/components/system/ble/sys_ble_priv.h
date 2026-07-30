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

/* Internal-only: one TX slot's buffer. tx_buff.header doubles as the slot's
   identifier (see sys_ble_char_send()/sys_ble_char_assign_tx_buffer()). */
typedef struct {
  bool is_indication;
  sys_buff_t tx_buff;
} sys_ble_tx_slot_t;

typedef struct sys_ble_char_node {
  sys_ble_char_cfg_t cfg;
  uint16_t val_handle;

  // RX buffer (header unused - RX frames are dequeued via sys_buff_pop_raw())
  sys_buff_t rx_buff;
  SemaphoreHandle_t rx_notify_sem;  // caller-owned, given on each peer write if non-NULL (see sys_ble_char_create_t.rx_notify_sem)
  own_func_t rx_handler;            // Optional: dispatched via SYS_CB_OWN() on each RX write, in addition to rx_buff/rx_notify_sem

  // TX buffers
  sys_ble_tx_slot_t tx_slots[MAX_TX_BUFFERS];
  uint8_t tx_slot_count;

  // Dynamic (re)compile bookkeeping - see sys_ble_database_sync()
  bool pending_add;     // true from creation until the owning service is next successfully (re)compiled
  bool pending_remove;  // set by sys_ble_char_remove() instead of freeing, when the owning service is already live

  struct sys_ble_char_node* next;
} sys_ble_char_node_t;

typedef struct sys_ble_svc_node {
  sys_ble_svc_cfg_t cfg;
  sys_ble_char_node_t* chars;
  bool registered;
  bool dirty;  // true when a characteristic was added/removed while registered == true; tells sync() to recompile
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
err_h sys_ble_stack_init(struct ble_gatt_svc_def* svcs);
err_h sys_ble_send_raw(uint16_t conn_handle, uint16_t chr_val_handle, const uint8_t* data, size_t len, bool indicate);
err_h sys_ble_reconfigure_advertising(void);
err_h sys_ble_advertising_init(void);
err_h populate_svc_def(struct ble_gatt_svc_def* svc_def, const sys_ble_svc_node_t* s);
err_h sys_ble_set_name(const char* name);


#define CHECK_BLE_CALL(nimble_call)                                                               \
  do {                                                                                            \
    int __rc = (nimble_call);                                                                     \
    if (__rc != 0) {                                                                              \
      ESP_LOGE(__FILE_NAME__, "%s: NimBLE call failed '%s' -> %d", __func__, #nimble_call, __rc); \
      SE_RET_ERR(ERR_BLE_STACK_FAILED, __rc);                                                        \
    }                                                                                             \
  } while (0)

#define CHECK_BLE_CHAR_FIND(var, uuid, mutex_unlock)                                                  \
  do {                                                                                                 \
    (var) = sys_ble_find_char_by_uuid(uuid);                                                          \
    if ((var) == NULL) {                                                                               \
      ESP_LOGE(__FILE_NAME__, "%s: Characteristic UUID 0x%04X not found", __func__, (uuid));          \
      if (mutex_unlock) R_MUTEX_UNLOCK(sys_ble_mutex);                                                \
      SE_RET_ERR(ERR_BASE_NOT_FOUND, uuid);                                                              \
    }                                                                                                  \
  } while (0)

#define CHECK_BLE_SVC_FIND(var, uuid, mutex_unlock)                                                   \
  do {                                                                                                 \
    (var) = sys_ble_find_svc_by_uuid(uuid);                                                           \
    if ((var) == NULL) {                                                                               \
      ESP_LOGE(__FILE_NAME__, "%s: Service UUID 0x%04X not found", __func__, (uuid));                 \
      if (mutex_unlock) R_MUTEX_UNLOCK(sys_ble_mutex);                                                \
      SE_RET_ERR(ERR_BASE_NOT_FOUND, uuid);                                                              \
    }                                                                                                  \
  } while (0)
