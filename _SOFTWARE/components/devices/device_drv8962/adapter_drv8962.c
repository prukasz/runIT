#include <stdint.h>
#include <stdlib.h>
#include "device_drv8962.h"
#include "driver_drv8962.h"
#include "esp_log.h"
#include "sys_device.h"
#include "sys_error.h"
#include "sys_hbridge.h"

static const char* TAG = "ADAPT_DRV8962";
#define OWNER OWNER_DEVICE_BASE

typedef struct drv8962_adapter_ctx_t {
  sys_device_adapter_base_t base; // MUST be first
  d_drv8962_cfg_t cfg;
} drv8962_adapter_ctx_t;

// --- VTABLE Implementations (SYS_DEVICE_CONTRACT_HBRIDGE) ---

static err_h contract_hbridge_set_mode(void* handle, uint8_t channel, sys_hbridge_mode_e mode) {
  SYS_DEV_GET_ADAPTER_CONTEXT(drv8962_adapter_ctx_t, drv8962_handle_t, ctx, hw, handle);
  return drv8962_set_mode(hw, channel, mode);
}

static err_h contract_hbridge_set_drive(void* handle, uint8_t channel, float magnitude) {
  SYS_DEV_GET_ADAPTER_CONTEXT(drv8962_adapter_ctx_t, drv8962_handle_t, ctx, hw, handle);
  return drv8962_set_drive(hw, channel, magnitude);
}

static err_h contract_hbridge_brake(void* handle, uint8_t channel) {
  SYS_DEV_GET_ADAPTER_CONTEXT(drv8962_adapter_ctx_t, drv8962_handle_t, ctx, hw, handle);
  return drv8962_brake(hw, channel);
}

static err_h contract_hbridge_coast(void* handle, uint8_t channel) {
  SYS_DEV_GET_ADAPTER_CONTEXT(drv8962_adapter_ctx_t, drv8962_handle_t, ctx, hw, handle);
  return drv8962_coast(hw, channel);
}

static err_h contract_hbridge_get_current_ma(void* handle, uint8_t channel, uint32_t* out_ma) {
  SYS_DEV_GET_ADAPTER_CONTEXT(drv8962_adapter_ctx_t, drv8962_handle_t, ctx, hw, handle);
  return drv8962_get_current_ma(hw, channel, out_ma);
}

static err_h contract_hbridge_set_current_limit_ma(void* handle, uint8_t channel, uint32_t limit_ma) {
  SYS_DEV_GET_ADAPTER_CONTEXT(drv8962_adapter_ctx_t, drv8962_handle_t, ctx, hw, handle);
  return drv8962_set_current_limit_ma(hw, channel, limit_ma);
}

static err_h contract_hbridge_configure_fault(void* handle, uint8_t channel, const sys_hbridge_fault_config_t* config) {
  SYS_DEV_GET_ADAPTER_CONTEXT(drv8962_adapter_ctx_t, drv8962_handle_t, ctx, hw, handle);
  return drv8962_configure_fault(hw, channel, config);
}

static err_h contract_hbridge_get_fault(void* handle, uint8_t channel, bool* out_fault, sys_hbridge_fault_reason_e* out_reason) {
  SYS_DEV_GET_ADAPTER_CONTEXT(drv8962_adapter_ctx_t, drv8962_handle_t, ctx, hw, handle);
  return drv8962_get_fault(hw, channel, out_fault, out_reason);
}

static err_h contract_hbridge_clear_fault(void* handle, uint8_t channel) {
  SYS_DEV_GET_ADAPTER_CONTEXT(drv8962_adapter_ctx_t, drv8962_handle_t, ctx, hw, handle);
  return drv8962_clear_fault(hw, channel);
}

static const sys_hbridge_vtable_t s_drv8962_bridge_vtable = {
    .set_mode = contract_hbridge_set_mode,
    .set_drive = contract_hbridge_set_drive,
    .brake = contract_hbridge_brake,
    .coast = contract_hbridge_coast,
    .get_current_ma = contract_hbridge_get_current_ma,
    .set_current_limit_ma = contract_hbridge_set_current_limit_ma,
    .configure_fault = contract_hbridge_configure_fault,
    .get_fault = contract_hbridge_get_fault,
    .clear_fault = contract_hbridge_clear_fault,
};

// --- Lifecycle Callbacks ---

static err_h device_uninstall(void* device_handle) {
  if (!device_handle) return NULL;
  drv8962_adapter_ctx_t* ctx = (drv8962_adapter_ctx_t*)device_handle;
  if (ctx->base.hw_handle) {
    drv8962_stop((drv8962_handle_t)ctx->base.hw_handle);
    free(ctx->base.hw_handle);
    ctx->base.hw_handle = NULL;
  }
  free(ctx);
  return NULL;
}

static err_h device_reset(void* device_handle) {
  if (!device_handle) return NULL;
  drv8962_adapter_ctx_t* ctx = (drv8962_adapter_ctx_t*)device_handle;
  drv8962_handle_t hw = (drv8962_handle_t)ctx->base.hw_handle;
  uint8_t channels = (ctx->cfg.topology == DRV8962_TOPOLOGY_2_FULL_BRIDGES) ? 2 : 4;
  for (uint8_t i = 0; i < channels; i++) {
    drv8962_brake(hw, i);
  }
  return NULL;
}

static err_h device_suspend(void* device_handle) {
  if (!device_handle) return NULL;
  drv8962_adapter_ctx_t* ctx = (drv8962_adapter_ctx_t*)device_handle;
  drv8962_handle_t hw = (drv8962_handle_t)ctx->base.hw_handle;
  uint8_t channels = (ctx->cfg.topology == DRV8962_TOPOLOGY_2_FULL_BRIDGES) ? 2 : 4;
  for (uint8_t i = 0; i < channels; i++) {
    drv8962_coast(hw, i);
  }
  return NULL;
}

static err_h device_resume(void* device_handle) {
  if (!device_handle) return NULL;
  return NULL;
}

static err_h device_freeze(void* device_handle) {
  return device_reset(device_handle);
}

static err_h device_sync(void* device_handle) {
  (void)device_handle;
  return NULL;
}

static err_h device_install(const void* cfg_blob, void** out_device_handle) {
  const d_drv8962_cfg_t* cfg = (const d_drv8962_cfg_t*)cfg_blob;
  SE_CHECK_NOT_NULL(cfg);
  SE_CHECK_NOT_NULL(out_device_handle);

  SYS_DEV_CTX_NEW(drv8962_adapter_ctx_t, ctx, cfg);

  ctx->base.hw_handle = drv8962_new(&ctx->cfg);
  if (!ctx->base.hw_handle) {
    free(ctx);
    SE_RET_ERR(ERR_BASE_NO_MEM, 0);
  }

  err_h err = drv8962_start((drv8962_handle_t)ctx->base.hw_handle);
  if (err != NULL) {
    free(ctx->base.hw_handle);
    free(ctx);
    return err;
  }

  ESP_LOGI(TAG, "DRV8962 driver installed as Device ID %u", ctx->cfg.device_id);
  *out_device_handle = ctx;
  return NULL;
}

static const sys_device_class_t s_drv8962_class = {
    .name = "DRV8962_BRIDGE_DRIVER",
    .contracts = {[SYS_DEVICE_CONTRACT_HBRIDGE] = (void*)&s_drv8962_bridge_vtable},
    .ops = {
        .install = device_install,
        .uninstall = device_uninstall,
        .reset = device_reset,
        .suspend = device_suspend,
        .resume = device_resume,
        .freeze = device_freeze,
        .sync = device_sync,
    },
};

err_h d_drv8962_create(const d_drv8962_cfg_t* cfg) {
  SE_CHECK_NOT_NULL(cfg);
  return SYS_DEVICE_CREATE(&s_drv8962_class, cfg);
}

