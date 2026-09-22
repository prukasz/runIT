#pragma once
#include "sys_error.h"
#include "sys_event.h"

//#ref-enum @alias H-Bridge Mode
typedef enum sys_hbridge_mode_e {
  SYS_HBRIDGE_MODE_FULL = 0,   //@alias Full Bridge @description Bidirectional drive from -1.0 to 1.0
  SYS_HBRIDGE_MODE_HALF_HIGH,  //@alias Half Bridge High @description High-side PWM drive from 0.0 to 1.0
  SYS_HBRIDGE_MODE_HALF_LOW,   //@alias Half Bridge Low @description Low-side PWM sink from 0.0 to 1.0
} sys_hbridge_mode_e;

/* Fault reason; also the event of a SYS_EVENT_DOMAIN_HBRIDGE event. */
//#ref-enum @alias H-Bridge Fault
typedef enum sys_hbridge_fault_reason_e {
  SYS_HBRIDGE_FAULT_NONE = 0,            //@alias None @description No fault
  SYS_HBRIDGE_FAULT_OVERCURRENT = 1,     //@alias Overcurrent @description The channel drew more than its current limit
  SYS_HBRIDGE_FAULT_THERMAL_OR_UVLO = 2, //@alias Overheat or Low Supply @description The driver chip overheated or its supply dropped too low; every channel stops
  SYS_HBRIDGE_FAULT_EXTERNAL = 3,        //@alias External @description An external fault input was triggered
} sys_hbridge_fault_reason_e;

/**
 * @brief Publish a channel fault (SYS_EVENT_DOMAIN_HBRIDGE) from a bridge driver.
 * Call it without holding the driver's lock: inline listeners run before it returns.
 * @param hops SYS_EVENT_CAUSED_BY(cause) when raised from a listener, else 0.
 */
static inline SE_MUST_USE err_h sys_hbridge_publish(uint8_t device_id, uint8_t channel, sys_hbridge_fault_reason_e reason, int32_t current_mA, uint8_t hops) {
  sys_event_t ev = {.domain = SYS_EVENT_DOMAIN_HBRIDGE, .device_id = device_id, .channel = channel, .event = (uint8_t)reason, .value = current_mA, .hops = hops};
  return sys_event_publish(&ev);
}

typedef struct sys_hbridge_contract_t {
  err_h (*set_mode)(void* handle, uint8_t channel, sys_hbridge_mode_e mode);
  err_h (*set_drive)(void* handle, uint8_t channel, float magnitude); /* -1.0f..+1.0f (Full) or 0.0f..1.0f (Half) */
  err_h (*brake)(void* handle, uint8_t channel);
  err_h (*coast)(void* handle, uint8_t channel);
  err_h (*get_current_mA)(void* handle, uint8_t channel, int32_t* out_mA);
  err_h (*set_current_limit_mA)(void* handle, uint8_t channel, uint32_t limit_mA);
  err_h (*get_fault)(void* handle, uint8_t channel, bool* out_fault, sys_hbridge_fault_reason_e* out_reason);
  err_h (*clear_fault)(void* handle, uint8_t channel);
} sys_hbridge_contract_t;

/* Member names of sys_hbridge_contract_t in order, NULL-terminated (feature id = index). */
extern const char* const sys_hbridge_feature_names[];

/* Unified Dispatch API */
SE_MUST_USE err_h sys_hbridge_set_mode(uint8_t device_id, uint8_t channel, sys_hbridge_mode_e mode);
SE_MUST_USE err_h sys_hbridge_set_drive(uint8_t device_id, uint8_t channel, float magnitude);
SE_MUST_USE err_h sys_hbridge_brake(uint8_t device_id, uint8_t channel);
SE_MUST_USE err_h sys_hbridge_coast(uint8_t device_id, uint8_t channel);
SE_MUST_USE err_h sys_hbridge_get_current_mA(uint8_t device_id, uint8_t channel, int32_t* out_mA);
SE_MUST_USE err_h sys_hbridge_set_current_limit_mA(uint8_t device_id, uint8_t channel, uint32_t limit_mA);
SE_MUST_USE err_h sys_hbridge_get_fault(uint8_t device_id, uint8_t channel, bool* out_fault, sys_hbridge_fault_reason_e* out_reason);
SE_MUST_USE err_h sys_hbridge_clear_fault(uint8_t device_id, uint8_t channel);

