#include "feature_hbridge.h"
#include "feature_registry.h"
#include "sys_event.h"
#include "sys_hbridge.h"
#include <math.h>

#define TAG "FEAT_HBRIDGE"
#define OWNER OWNER_FEATURES_HBRIDGE

/* Inline listener of the bridge channel: record the fault and republish it as this feature's event. */
static SE_MUST_USE err_h hbridge_on_bridge_fault(const sys_event_t* event, void* ctx) {
  uint8_t feature_id = (uint8_t)(uintptr_t)ctx;
  feature_hbridge_t* hb = (feature_hbridge_t*)feature_get_by_id(feature_id);
  if (!hb) return NULL;

  hb->is_fault = true;
  hb->fault_reason = (sys_hbridge_fault_reason_e)event->event;
  hb->last_current_mA = event->value;

  sys_event_t ev = {
      .domain = SYS_EVENT_DOMAIN_FEATURE,
      .device_id = feature_id,
      .event = event->event,
      .value = event->value,
      .hops = SYS_EVENT_CAUSED_BY(event),
  };
  return sys_event_publish(&ev);
}

static void hbridge_teardown(void* data) {
  feature_hbridge_t* hb = (feature_hbridge_t*)data;
  if (hb) {
    if (hb->fault_subscribed) SE_REPORT(sys_event_unsubscribe(hb->fault_sub, false));
    // TODO(features analysis): teardown returns void, so a coast failure can't be reported yet.
    SE_release(sys_hbridge_coast(hb->bridge_device_id, hb->channel));
    hb->current_speed = 0.0f;
    hb->is_coasting = true;
  }
}

err_h feature_hbridge_create(uint8_t feature_id, const feature_hbridge_t* config) {
  SE_CHECK_NOT_NULL(config);

  feature_hbridge_t* hb = NULL;
  SE_TRY(feature_alloc(feature_id, sizeof(feature_hbridge_t), hbridge_teardown, (void**)&hb));

  *hb = *config;
  hb->feature_id = feature_id;
  hb->fault_subscribed = false;

  /* Configure channel in Full-Bridge mode */
  SE_TRY(sys_hbridge_set_mode(hb->bridge_device_id, hb->channel, SYS_HBRIDGE_MODE_FULL));

  /* Listen to the channel's faults */
  sys_event_subscription_t sub = {
      .domain = SYS_EVENT_DOMAIN_HBRIDGE,
      .device_id = hb->bridge_device_id,
      .channel = hb->channel,
      .event = SYS_EVENT_ANY,
      .inline_call = true,
      .handler = hbridge_on_bridge_fault,
      .ctx = (void*)(uintptr_t)feature_id,
  };
  SE_TRY(sys_event_subscribe(&sub, &hb->fault_sub));
  hb->fault_subscribed = true;

  /* Default initial state: Brake (stop) */
  return feature_hbridge_brake(feature_id);
}

err_h feature_hbridge_set_speed(uint8_t feature_id, float speed) {
  feature_hbridge_t* hb = (feature_hbridge_t*)feature_get_by_id(feature_id);
  if (!hb) {
    SE_FAIL(ERR_BASE_NOT_FOUND, 0);
  }

  /* Clamp speed to [-1.0f, +1.0f] */
  if (speed > 1.0f) speed = 1.0f;
  if (speed < -1.0f) speed = -1.0f;

  if (hb->inverted) {
    speed = -speed;
  }

  if (fabsf(speed) < 0.005f) {
    return feature_hbridge_brake(feature_id);
  }

  SE_TRY(sys_hbridge_set_drive(hb->bridge_device_id, hb->channel, speed));

  hb->current_speed = speed;
  hb->is_braking = false;
  hb->is_coasting = false;
  return NULL;
}

err_h feature_hbridge_get_speed(uint8_t feature_id, float* out_speed) {
  SE_CHECK_NOT_NULL(out_speed);
  feature_hbridge_t* hb = (feature_hbridge_t*)feature_get_by_id(feature_id);
  if (!hb) {
    SE_FAIL(ERR_BASE_NOT_FOUND, 0);
  }

  *out_speed = hb->current_speed;
  return NULL;
}

err_h feature_hbridge_brake(uint8_t feature_id) {
  feature_hbridge_t* hb = (feature_hbridge_t*)feature_get_by_id(feature_id);
  if (!hb) {
    SE_FAIL(ERR_BASE_NOT_FOUND, 0);
  }

  SE_TRY(sys_hbridge_brake(hb->bridge_device_id, hb->channel));
  hb->current_speed = 0.0f;
  hb->is_braking = true;
  hb->is_coasting = false;
  return NULL;
}

err_h feature_hbridge_coast(uint8_t feature_id) {
  feature_hbridge_t* hb = (feature_hbridge_t*)feature_get_by_id(feature_id);
  if (!hb) {
    SE_FAIL(ERR_BASE_NOT_FOUND, 0);
  }

  SE_TRY(sys_hbridge_coast(hb->bridge_device_id, hb->channel));
  hb->current_speed = 0.0f;
  hb->is_braking = false;
  hb->is_coasting = true;
  return NULL;
}

err_h feature_hbridge_get_current_mA(uint8_t feature_id, int32_t* out_current_mA) {
  SE_CHECK_NOT_NULL(out_current_mA);
  feature_hbridge_t* hb = (feature_hbridge_t*)feature_get_by_id(feature_id);
  if (!hb) {
    SE_FAIL(ERR_BASE_NOT_FOUND, 0);
  }

  SE_TRY(sys_hbridge_get_current_mA(hb->bridge_device_id, hb->channel, out_current_mA));
  hb->last_current_mA = *out_current_mA;
  return NULL;
}

err_h feature_hbridge_set_current_limit_mA(uint8_t feature_id, uint32_t limit_mA) {
  feature_hbridge_t* hb = (feature_hbridge_t*)feature_get_by_id(feature_id);
  if (!hb) {
    SE_FAIL(ERR_BASE_NOT_FOUND, 0);
  }

  return sys_hbridge_set_current_limit_mA(hb->bridge_device_id, hb->channel, limit_mA);
}

err_h feature_hbridge_get_fault(uint8_t feature_id, bool* out_fault) {
  SE_CHECK_NOT_NULL(out_fault);
  feature_hbridge_t* hb = (feature_hbridge_t*)feature_get_by_id(feature_id);
  if (!hb) {
    SE_FAIL(ERR_BASE_NOT_FOUND, 0);
  }

  sys_hbridge_fault_reason_e reason = SYS_HBRIDGE_FAULT_NONE;
  SE_TRY(sys_hbridge_get_fault(hb->bridge_device_id, hb->channel, out_fault, &reason));
  hb->is_fault = *out_fault;
  hb->fault_reason = reason;
  return NULL;
}

err_h feature_hbridge_get_fault_reason(uint8_t feature_id, sys_hbridge_fault_reason_e* out_reason) {
  SE_CHECK_NOT_NULL(out_reason);
  feature_hbridge_t* hb = (feature_hbridge_t*)feature_get_by_id(feature_id);
  if (!hb) {
    SE_FAIL(ERR_BASE_NOT_FOUND, 0);
  }

  *out_reason = hb->fault_reason;
  return NULL;
}

err_h feature_hbridge_clear_fault(uint8_t feature_id) {
  feature_hbridge_t* hb = (feature_hbridge_t*)feature_get_by_id(feature_id);
  if (!hb) {
    SE_FAIL(ERR_BASE_NOT_FOUND, 0);
  }

  SE_TRY(sys_hbridge_clear_fault(hb->bridge_device_id, hb->channel));
  hb->is_fault = false;
  hb->fault_reason = SYS_HBRIDGE_FAULT_NONE;
  return feature_hbridge_brake(feature_id);
}
