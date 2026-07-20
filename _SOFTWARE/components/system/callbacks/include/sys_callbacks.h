#pragma once
#include <stdint.h>
#include "status.h"

typedef enum callback_type_e {
  CALLBACK_NONE = 0,
  CALLBACK_IO = 1,
  CALLBACK_PWR = 2,
  CALLBACK_BLE = 3,
} callback_type_e;

typedef struct sys_callback_t {
  uint16_t callback_type;
  struct {
    uint16_t route_mask : 16;
  } route_to;
} sys_callback_head_t;

typedef struct io_event_t {
  uint8_t device_id;
  uint8_t pin_id;
  uint16_t trigger_event;
  int32_t trigger_value;  // adc or bool
} io_event_t;

typedef struct pwr_event_t {
  uint8_t device_id;
  uint8_t channel_id;
  uint16_t trigger_event;
  int32_t trigger_value;  // current or voltage
} pwr_event_t;

typedef struct cb_event_t {
  sys_callback_head_t head;
  union {
    io_event_t io;
    pwr_event_t pwr;
  } event;
} cb_event_t;

status_rep_t sys_callback_trigger(cb_event_t* event);
status_rep_t sys_callbacks_init(void);