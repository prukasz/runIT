#pragma once
#include <stdint.h>
#include "sys_error.h"

typedef enum callback_type_e {
  CALLBACK_NONE = 0,
  CALLBACK_IO = 1,
  CALLBACK_PWR = 2,
  CALLBACK_BLE = 3,
  CALLBACK_OWN_FUNC = 4,
  CALLBACK_MAX
} callback_type_e;

typedef struct sys_callback_head_t {
  uint16_t callback_type; /* callback_type_e value */
  uint16_t route_mask;    /* Bitmask for destination routing */
} sys_callback_head_t;

/**
 * @brief Static route table slot indices.
 *
 * Bit `i` of `sys_callback_head_t.route_mask` selects route slot `i` in
 * `sys_cb_task`'s dispatch table - callers building a route mask (e.g. the
 * `route_mask` argument of `sys_vreg_add_callback()`, `sys_ble_add_callback()`)
 * OR the bits of every route they want the event delivered to together, using
 * `SYS_CB_ROUTE_BIT()`. Slots are added progressively; an index with no
 * registered handler is silently skipped, not an error.
 */
#define SYS_CB_ROUTE_LOGGER 0 /* Generic ESP_LOGI logger for IO/PWR/BLE events */
#define SYS_CB_ROUTE_BLE 1    /* BLE-specific handling */
#define SYS_CB_ROUTE_WIFI 2   /* WiFi-specific handling - not implemented yet */
#define SYS_CB_ROUTE_COUNT 16 /* route_mask is a uint16_t - one bit per slot */

#define SYS_CB_ROUTE_BIT(idx) (1u << (idx))

typedef struct io_event_t {
  uint8_t device_id;
  uint8_t pin_id;
  uint16_t trigger_event;
  int32_t trigger_value;  // ADC reading or digital state
} io_event_t;

typedef struct pwr_event_t {
  uint8_t device_id;
  uint8_t channel_id;
  uint16_t trigger_event;
  int32_t trigger_value;  // Current or voltage reading
} pwr_event_t;

typedef struct ble_event_t {
  uint32_t event;
  int32_t value;
} ble_event_t;

struct cb_event_t;

typedef struct own_func_t {
  err_h (*own_func)(void* device_handle, struct cb_event_t* event);
  void* device_handle;
} own_func_t;

typedef own_func_t own_funct_t;

typedef struct cb_event_t {
  sys_callback_head_t head;
  union {
    io_event_t io;
    pwr_event_t pwr;
    ble_event_t ble;
    own_func_t own_func;
  } event;
} cb_event_t;

err_h sys_callbacks_init(void);
err_h sys_callback_trigger(const cb_event_t* event);

#define SYS_CB_OWN(own_func_struct)                  \
  do {                                               \
    cb_event_t __cb_evt = {                          \
        .head = {.callback_type = CALLBACK_OWN_FUNC},\
        .event.own_func = (own_func_struct),         \
    };                                               \
    sys_callback_trigger(&__cb_evt);                 \
  } while (0)
