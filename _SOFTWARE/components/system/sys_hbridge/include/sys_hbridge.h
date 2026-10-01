#pragma once
#include "sys_error.h"
#include "sys_event.h"

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

/* What every H-bridge driver offers. Chip specifics (current limit, fault latch, ...) are operations of the device. */
typedef struct sys_hbridge_contract_t {
  err_h (*set_drive)(void* handle, uint8_t channel, float magnitude); /* -1.0f..+1.0f (full bridge) or 0.0f..1.0f (half bridge) */
  err_h (*brake)(void* handle, uint8_t channel);
  err_h (*coast)(void* handle, uint8_t channel);
  err_h (*get_current_mA)(void* handle, uint8_t channel, int32_t* out_mA);
} sys_hbridge_contract_t;

/* Member names of sys_hbridge_contract_t in order, NULL-terminated (feature id = index). */
extern const char* const sys_hbridge_feature_names[];

/* Unified Dispatch API */
SE_MUST_USE err_h sys_hbridge_set_drive(uint8_t device_id, uint8_t channel, float magnitude);
SE_MUST_USE err_h sys_hbridge_brake(uint8_t device_id, uint8_t channel);
SE_MUST_USE err_h sys_hbridge_coast(uint8_t device_id, uint8_t channel);
SE_MUST_USE err_h sys_hbridge_get_current_mA(uint8_t device_id, uint8_t channel, int32_t* out_mA);
