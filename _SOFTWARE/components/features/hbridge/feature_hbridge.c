#include "feature_hbridge.h"
#include "feature_registry.h"
#include "sys_callbacks.h"
#include "sys_hbridge.h"
#include <math.h>

#define TAG "FEAT_HBRIDGE"
#define OWNER OWNER_SYS_ERRORS_BASE

static err_h hbridge_internal_fault_trampoline(void* handle, struct cb_event_t* event) {
  (void)event;
  uint8_t feature_id = (uint8_t)(uintptr_t)handle;
  feature_hbridge_t* hb = (feature_hbridge_t*)feature_get_by_id(feature_id);
  if (!hb) return NULL;

  hb->is_fault = true;
  sys_hbridge_get_fault(hb->bridge_device_id, hb->channel, &hb->is_fault, &hb->fault_reason);
  sys_hbridge_get_current_ma(hb->bridge_device_id, hb->channel, &hb->last_current_ma);

  if (hb->on_fault) {
    hb->on_fault(feature_id, hb->fault_reason, hb->last_current_ma, hb->fault_user_ctx);
  }

  return NULL;
}

static void hbridge_teardown(void* data) {
  feature_hbridge_t* hb = (feature_hbridge_t*)data;
  if (hb) {
    sys_hbridge_coast(hb->bridge_device_id, hb->channel);
    hb->current_speed = 0.0f;
    hb->is_coasting = true;
  }
}

err_h feature_hbridge_create(uint8_t feature_id, const feature_hbridge_t* config) {
  SE_CHECK_NOT_NULL(config);

  feature_hbridge_t* hb = NULL;
  SE_RET_IF_ERR(feature_alloc(feature_id, sizeof(feature_hbridge_t), hbridge_teardown, (void**)&hb));

  *hb = *config;
  hb->feature_id = feature_id;

  /* Configure channel in Full-Bridge mode */
  SE_RET_IF_ERR(sys_hbridge_set_mode(hb->bridge_device_id, hb->channel, SYS_HBRIDGE_MODE_FULL));

  /* Configure fault callbacks & routing */
  sys_hbridge_fault_config_t fault_cfg = {
      .route_mask = hb->fault_route_mask,
      .static_action_id = hb->fault_static_action_id,
      .dynamic_action_id = hb->fault_dynamic_action_id,
      .own_func = {
          .own_func = hbridge_internal_fault_trampoline,
          .device_handle = (void*)(uintptr_t)feature_id,
      },
  };
  SE_RET_IF_ERR(sys_hbridge_configure_fault(hb->bridge_device_id, hb->channel, &fault_cfg));

  /* Default initial state: Brake (stop) */
  return feature_hbridge_brake(feature_id);
}

err_h feature_hbridge_set_speed(uint8_t feature_id, float speed) {
  feature_hbridge_t* hb = (feature_hbridge_t*)feature_get_by_id(feature_id);
  if (!hb) {
    SE_RET_ERR(ERR_BASE_NOT_FOUND, 0);
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

  SE_RET_IF_ERR(sys_hbridge_set_drive(hb->bridge_device_id, hb->channel, speed));

  hb->current_speed = speed;
  hb->is_braking = false;
  hb->is_coasting = false;
  return NULL;
}

err_h feature_hbridge_get_speed(uint8_t feature_id, float* out_speed) {
  SE_CHECK_NOT_NULL(out_speed);
  feature_hbridge_t* hb = (feature_hbridge_t*)feature_get_by_id(feature_id);
  if (!hb) {
    SE_RET_ERR(ERR_BASE_NOT_FOUND, 0);
  }

  *out_speed = hb->current_speed;
  return NULL;
}

err_h feature_hbridge_brake(uint8_t feature_id) {
  feature_hbridge_t* hb = (feature_hbridge_t*)feature_get_by_id(feature_id);
  if (!hb) {
    SE_RET_ERR(ERR_BASE_NOT_FOUND, 0);
  }

  SE_RET_IF_ERR(sys_hbridge_brake(hb->bridge_device_id, hb->channel));
  hb->current_speed = 0.0f;
  hb->is_braking = true;
  hb->is_coasting = false;
  return NULL;
}

err_h feature_hbridge_coast(uint8_t feature_id) {
  feature_hbridge_t* hb = (feature_hbridge_t*)feature_get_by_id(feature_id);
  if (!hb) {
    SE_RET_ERR(ERR_BASE_NOT_FOUND, 0);
  }

  SE_RET_IF_ERR(sys_hbridge_coast(hb->bridge_device_id, hb->channel));
  hb->current_speed = 0.0f;
  hb->is_braking = false;
  hb->is_coasting = true;
  return NULL;
}

err_h feature_hbridge_get_current_ma(uint8_t feature_id, uint32_t* out_current_ma) {
  SE_CHECK_NOT_NULL(out_current_ma);
  feature_hbridge_t* hb = (feature_hbridge_t*)feature_get_by_id(feature_id);
  if (!hb) {
    SE_RET_ERR(ERR_BASE_NOT_FOUND, 0);
  }

  SE_RET_IF_ERR(sys_hbridge_get_current_ma(hb->bridge_device_id, hb->channel, out_current_ma));
  hb->last_current_ma = *out_current_ma;
  return NULL;
}

err_h feature_hbridge_set_current_limit_ma(uint8_t feature_id, uint32_t limit_ma) {
  feature_hbridge_t* hb = (feature_hbridge_t*)feature_get_by_id(feature_id);
  if (!hb) {
    SE_RET_ERR(ERR_BASE_NOT_FOUND, 0);
  }

  return sys_hbridge_set_current_limit_ma(hb->bridge_device_id, hb->channel, limit_ma);
}

err_h feature_hbridge_get_fault(uint8_t feature_id, bool* out_fault) {
  SE_CHECK_NOT_NULL(out_fault);
  feature_hbridge_t* hb = (feature_hbridge_t*)feature_get_by_id(feature_id);
  if (!hb) {
    SE_RET_ERR(ERR_BASE_NOT_FOUND, 0);
  }

  sys_hbridge_fault_reason_e reason = SYS_HBRIDGE_FAULT_NONE;
  SE_RET_IF_ERR(sys_hbridge_get_fault(hb->bridge_device_id, hb->channel, out_fault, &reason));
  hb->is_fault = *out_fault;
  hb->fault_reason = reason;
  return NULL;
}

err_h feature_hbridge_get_fault_reason(uint8_t feature_id, sys_hbridge_fault_reason_e* out_reason) {
  SE_CHECK_NOT_NULL(out_reason);
  feature_hbridge_t* hb = (feature_hbridge_t*)feature_get_by_id(feature_id);
  if (!hb) {
    SE_RET_ERR(ERR_BASE_NOT_FOUND, 0);
  }

  *out_reason = hb->fault_reason;
  return NULL;
}

err_h feature_hbridge_clear_fault(uint8_t feature_id) {
  feature_hbridge_t* hb = (feature_hbridge_t*)feature_get_by_id(feature_id);
  if (!hb) {
    SE_RET_ERR(ERR_BASE_NOT_FOUND, 0);
  }

  SE_RET_IF_ERR(sys_hbridge_clear_fault(hb->bridge_device_id, hb->channel));
  hb->is_fault = false;
  hb->fault_reason = SYS_HBRIDGE_FAULT_NONE;
  return feature_hbridge_brake(feature_id);
}
