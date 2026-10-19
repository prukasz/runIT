#pragma once

/* Shared between sys_ble.c (registry, data) and sys_ble_stack.c (NimBLE). */

#include "freertos/FreeRTOS.h"
#include "freertos/ringbuf.h"
#include "host/ble_gatt.h"
#include "sdkconfig.h"
#include "sys_ble.h"
#include "utils.h"

typedef struct sys_ble_char_node {
  sys_ble_char_cfg_t cfg;  // cfg.desc points to `desc`
  char* desc;
  uint16_t val_handle;  // set by NimBLE when the service is registered
  bool is_subscribed;
  RingbufHandle_t rx_rb;  // NULL = no RX buffer
  RingbufHandle_t tx_rb;  // NULL = no TX
  sys_ble_rx_f rx_fn;     // direct RX handler, NULL = none
  void* rx_ctx;
  bool pending_add;     // until its service is next (re)compiled
  bool pending_remove;  // removed from a live service: freed at the next sync
  struct sys_ble_char_node* next;
} sys_ble_char_node_t;

typedef struct sys_ble_svc_node {
  sys_ble_svc_cfg_t cfg;
  sys_ble_char_node_t* chars;
  bool registered;  // live in NimBLE
  bool locked;
  bool removing;  // removed while live: deleted at the next sync, invisible to lookups
  bool dirty;     // characteristics changed while live: rebuilt at the next sync
  struct ble_gatt_svc_def* compiled_def;  // owned by this service after a runtime (re)add
  struct sys_ble_svc_node* next;
} sys_ble_svc_node_t;

typedef struct {
  sys_ble_svc_node_t* services;
  bool started;
  bool is_connected;
  uint16_t conn_handle;
  uint16_t mtu;
  uint32_t rx_overflow_count;
  struct ble_gatt_svc_def* initial_db;  // table given to NimBLE at start; it keeps referencing it
  bool apply_pending;
  TickType_t apply_at;
} sys_ble_ctx_t;

extern sys_ble_ctx_t g_ble;
extern SemaphoreHandle_t sys_ble_mutex;   // registry and context
extern SemaphoreHandle_t sys_ble_tx_sem;  // wakes the BLE task

#define BLE_LOCK() U_MUTEX_LOCK(sys_ble_mutex, WAIT_FOREVER)
#define BLE_UNLOCK() U_MUTEX_UNLOCK(sys_ble_mutex)

/* Errors raised on the host / BLE task have no caller to return to: reported
 * (log + errors stream; the stream is queued, so this is safe under the lock). */
static inline void sys_ble_report(err_h err) {
  err_report(err);
}

/* sys_ble.c */
sys_ble_char_node_t* sys_ble_find_char_by_handle(uint16_t val_handle);  // caller holds the lock
void sys_ble_on_connect(uint16_t conn_handle, uint16_t mtu);
void sys_ble_on_disconnect(int32_t reason);
void sys_ble_on_mtu(uint16_t mtu);
void sys_ble_on_subscribe(uint16_t attr_handle, bool subscribed);
/** Peer write (host task): RX handler or RX buffer. Returns an ATT error code, 0 on success. */
int sys_ble_on_write(sys_ble_char_node_t* c, const uint8_t* data, size_t len);
/** BLE task: due apply, then drain TX. */
void sys_ble_task_step(uint8_t* scratch, size_t scratch_len);
/** BLE task: how long to wait for work. */
TickType_t sys_ble_task_wait(void);

/* sys_ble_stack.c */
MUST_USE err_h sys_ble_stack_start(struct ble_gatt_svc_def* svcs);
MUST_USE err_h sys_ble_compile_svc(struct ble_gatt_svc_def* def, sys_ble_svc_node_t* s);
void sys_ble_free_svc_defs(struct ble_gatt_svc_def* svcs);
MUST_USE err_h sys_ble_stack_add_svc(sys_ble_svc_node_t* s, struct ble_gatt_svc_def* svcs);
MUST_USE err_h sys_ble_stack_delete_svc(uint16_t uuid);
/** One notification / indication. Returns the NimBLE code (BLE_HS_ENOMEM = retry). */
int sys_ble_stack_notify(uint16_t conn_handle, uint16_t val_handle, const uint8_t* data, size_t len, bool indicate);
