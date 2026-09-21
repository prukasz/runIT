#pragma once
#include "sys_error.h"
#include "sys_callbacks.h"

typedef enum {
  SYS_HBRIDGE_MODE_FULL = 0,   /* Bi-directional Full H-Bridge: magnitude -1.0f (rev) to +1.0f (fwd) */
  SYS_HBRIDGE_MODE_HALF_HIGH,  /* Unidirectional Half-Bridge: PWM high-side drive (0.0f to 1.0f) */
  SYS_HBRIDGE_MODE_HALF_LOW,   /* Unidirectional Half-Bridge: PWM low-side sink (0.0f to 1.0f) */
} sys_hbridge_mode_e;

typedef enum {
  SYS_HBRIDGE_FAULT_NONE = 0,
  SYS_HBRIDGE_FAULT_OVERCURRENT,        /* Overcurrent protection (OCP) tripped on this channel */
  SYS_HBRIDGE_FAULT_THERMAL_OR_UVLO,    /* Chip-wide thermal shutdown (OTSD) or UVLO */
  SYS_HBRIDGE_FAULT_EXTERNAL,           /* External fault pin triggered */
} sys_hbridge_fault_reason_e;

typedef struct sys_hbridge_fault_config_t {
  uint16_t route_mask;         /* Route bitmask (e.g. SYS_CB_ROUTE_BIT(CONFIG_SYS_CB_ROUTE_VM)) */
  uint8_t static_action_id;    /* System action ID; zero disables execution */
  uint8_t dynamic_action_id;   /* User dynamic action ID; zero disables execution */
  own_funct_t own_func;        /* Direct C callback / trampoline */
} sys_hbridge_fault_config_t;

typedef struct sys_hbridge_vtable_t {
  err_h (*set_mode)(void* handle, uint8_t channel, sys_hbridge_mode_e mode);
  err_h (*set_drive)(void* handle, uint8_t channel, float magnitude); /* -1.0f..+1.0f (Full) or 0.0f..1.0f (Half) */
  err_h (*brake)(void* handle, uint8_t channel);
  err_h (*coast)(void* handle, uint8_t channel);
  err_h (*get_current_ma)(void* handle, uint8_t channel, uint32_t* out_ma);
  err_h (*set_current_limit_ma)(void* handle, uint8_t channel, uint32_t limit_ma);
  err_h (*configure_fault)(void* handle, uint8_t channel, const sys_hbridge_fault_config_t* config);
  err_h (*get_fault)(void* handle, uint8_t channel, bool* out_fault, sys_hbridge_fault_reason_e* out_reason);
  err_h (*clear_fault)(void* handle, uint8_t channel);
} sys_hbridge_vtable_t;

typedef enum sys_hbridge_feature_e {
  SYS_HBRIDGE_FEATURE_SET_MODE = 0,
  SYS_HBRIDGE_FEATURE_SET_DRIVE,
  SYS_HBRIDGE_FEATURE_BRAKE,
  SYS_HBRIDGE_FEATURE_COAST,
  SYS_HBRIDGE_FEATURE_GET_CURRENT_MA,
  SYS_HBRIDGE_FEATURE_SET_CURRENT_LIMIT_MA,
  SYS_HBRIDGE_FEATURE_CONFIGURE_FAULT,
  SYS_HBRIDGE_FEATURE_GET_FAULT,
  SYS_HBRIDGE_FEATURE_CLEAR_FAULT,
} sys_hbridge_feature_e;

extern const char* const sys_hbridge_feature_e_to_string[];

/* Unified Dispatch API */
err_h sys_hbridge_set_mode(uint8_t device_id, uint8_t channel, sys_hbridge_mode_e mode);
err_h sys_hbridge_set_drive(uint8_t device_id, uint8_t channel, float magnitude);
err_h sys_hbridge_brake(uint8_t device_id, uint8_t channel);
err_h sys_hbridge_coast(uint8_t device_id, uint8_t channel);
err_h sys_hbridge_get_current_ma(uint8_t device_id, uint8_t channel, uint32_t* out_ma);
err_h sys_hbridge_set_current_limit_ma(uint8_t device_id, uint8_t channel, uint32_t limit_ma);
err_h sys_hbridge_configure_fault(uint8_t device_id, uint8_t channel, const sys_hbridge_fault_config_t* config);
err_h sys_hbridge_get_fault(uint8_t device_id, uint8_t channel, bool* out_fault, sys_hbridge_fault_reason_e* out_reason);
err_h sys_hbridge_clear_fault(uint8_t device_id, uint8_t channel);

