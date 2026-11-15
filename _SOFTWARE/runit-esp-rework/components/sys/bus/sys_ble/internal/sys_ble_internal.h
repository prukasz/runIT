#pragma once

/* Shared by the sources of sys_ble: the registry (sys_ble.c), NimBLE (sys_ble_stack.c) and the router. */

#include "freertos/FreeRTOS.h"
#include "freertos/ringbuf.h"
#include "freertos/semphr.h"
#include "host/ble_gatt.h"
#include "sys_ble.h"
#include "sys_ble_router.h"

/** A frame being joined from its packets (a framed characteristic the peer writes). */
typedef struct {
  uint8_t buf[CMD_FRAME_MAX];
  uint32_t len;
  uint8_t expected;  // the counter of the next packet
  bool active;       // inside a frame
} ble_frag_t;

typedef struct ble_char {
  uint16_t uuid;
  uint8_t flags;          // SYS_BLE_CHR_*
  uint16_t tx_size;       // its TX buffer's bytes
  char* name;             // NULL = none
  uint16_t val_handle;    // set by NimBLE when its service is registered
  bool subscribed;        // the client takes its notifications
  RingbufHandle_t tx;     // NULL = no TX
  ble_frag_t* rx;         // framed and written by the peer
  sys_ble_rx_f cb;        // the hook, NULL = none
  void* ctx;
  bool up;                // the state the hook was last told
  bool adding;            // until its service is next (re)compiled
  bool removing;          // gone for lookups; freed at the next sync (NimBLE still points at it)
  struct ble_char* next;
} ble_char_t;

typedef struct ble_svc {
  uint16_t uuid;
  bool primary;
  bool locked;     // the board's own: no change, no removal
  bool registered; // live in NimBLE
  bool removing;   // deleted at the next sync, invisible to lookups
  bool dirty;      // characteristics changed while live: rebuilt at the next sync
  ble_char_t* chars;
  struct ble_gatt_svc_def* def;  // owned after a run-time (re)add
  struct ble_svc* next;
} ble_svc_t;

typedef struct {
  ble_svc_t* services;
  bool connected;
  uint16_t conn;
  uint16_t mtu;
  bool apply_pending;
  TickType_t apply_at;
} ble_t;

extern ble_t g_ble;
extern SemaphoreHandle_t g_ble_lock;  // the registry and g_ble; released around NimBLE calls that wait on the host
extern SemaphoreHandle_t g_ble_wake;  // wakes the BLE task

/* sys_ble.c: the router's reads */
MUST_USE err_h sys_ble_status(sys_ble_status_t* out);
/** The LIST answer (sys_ble_router.h). */
MUST_USE err_h sys_ble_list(cmd_out_t* out);

/* sys_ble.c: NimBLE's events (host task) */
void sys_ble_on_connect(uint16_t conn, uint16_t mtu);
void sys_ble_on_disconnect(void);
void sys_ble_on_mtu(uint16_t mtu);
void sys_ble_on_subscribe(uint16_t val_handle, bool subscribed);
/** A peer's write: an ATT error code, 0 when taken. */
int sys_ble_on_write(ble_char_t* c, const uint8_t* data, uint32_t len);
/** The BLE task's loop. */
void sys_ble_task(void* arg);

/* sys_ble_stack.c: the start in three steps (the host does not run until the third: no lock order to keep) */
MUST_USE err_h sys_ble_stack_init(uint16_t adv_service);
MUST_USE err_h sys_ble_stack_add_boot(struct ble_gatt_svc_def* svcs);
MUST_USE err_h sys_ble_stack_run(void);
/** On failure the partial definition stays in `def`: sys_ble_free_defs frees it. */
MUST_USE err_h sys_ble_compile(struct ble_gatt_svc_def* def, ble_svc_t* s);
void sys_ble_free_defs(struct ble_gatt_svc_def* svcs);
MUST_USE err_h sys_ble_stack_add(uint16_t uuid, struct ble_gatt_svc_def* svcs);
MUST_USE err_h sys_ble_stack_delete(uint16_t uuid);
/** One notification / indication: NimBLE's code (BLE_HS_ENOMEM: try again). */
int sys_ble_stack_notify(uint16_t conn, uint16_t val_handle, const uint8_t* data, uint32_t len, bool indicate);
