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
    uint16_t route_flags : 16;
  } route_to;
} sys_callback_head_t;

typedef struct sys_callbac_io {
  sys_callback_head_t head;

}

typedef sys_callback_head_t* sys_callback_handle_t;