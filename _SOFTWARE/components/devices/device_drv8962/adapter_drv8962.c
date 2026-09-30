#include <stdint.h>
#include <stdlib.h>
#include "device_drv8962.h"
#include "driver_drv8962.h"
#include "sys_device.h"
#include "sys_error.h"
#include "sys_hbridge.h"

#undef OWNER
#define OWNER OWNER_DEVICE_DRV8962

typedef struct drv8962_adapter_ctx_t {
  sys_device_adapter_base_t base; // MUST be first
  d_drv8962_cfg_t cfg;
} drv8962_adapter_ctx_t;

// --- VTABLE Implementations (SYS_DEVICE_CONTRACT_HBRIDGE) ---

static SE_MUST_USE err_h contract_hbridge_set_mode(void* handle, uint8_t channel, sys_hbridge_mode_e mode) {
  SYS_DEV_GET_ADAPTER_CONTEXT(drv8962_adapter_ctx_t, drv8962_handle_t, ctx, hw, handle);
  return drv8962_set_mode(hw, channel, mode);
}

static SE_MUST_USE err_h contract_hbridge_set_drive(void* handle, uint8_t channel, float magnitude) {
  SYS_DEV_GET_ADAPTER_CONTEXT(drv8962_adapter_ctx_t, drv8962_handle_t, ctx, hw, handle);
  return drv8962_set_drive(hw, channel, magnitude);
}

static SE_MUST_USE err_h contract_hbridge_brake(void* handle, uint8_t channel) {
  SYS_DEV_GET_ADAPTER_CONTEXT(drv8962_adapter_ctx_t, drv8962_handle_t, ctx, hw, handle);
  return drv8962_brake(hw, channel);
}

static SE_MUST_USE err_h contract_hbridge_coast(void* handle, uint8_t channel) {
  SYS_DEV_GET_ADAPTER_CONTEXT(drv8962_adapter_ctx_t, drv8962_handle_t, ctx, hw, handle);
  return drv8962_coast(hw, channel);
}

static SE_MUST_USE err_h contract_hbridge_get_current_mA(void* handle, uint8_t channel, int32_t* out_mA) {
  SYS_DEV_GET_ADAPTER_CONTEXT(drv8962_adapter_ctx_t, drv8962_handle_t, ctx, hw, handle);
  return drv8962_get_current_mA(hw, channel, out_mA);
}

static SE_MUST_USE err_h contract_hbridge_set_current_limit_mA(void* handle, uint8_t channel, uint32_t limit_mA) {
  SYS_DEV_GET_ADAPTER_CONTEXT(drv8962_adapter_ctx_t, drv8962_handle_t, ctx, hw, handle);
  return drv8962_set_current_limit_mA(hw, channel, limit_mA);
}

static SE_MUST_USE err_h contract_hbridge_get_fault(void* handle, uint8_t channel, bool* out_fault, sys_hbridge_fault_reason_e* out_reason) {
  SYS_DEV_GET_ADAPTER_CONTEXT(drv8962_adapter_ctx_t, drv8962_handle_t, ctx, hw, handle);
  return drv8962_get_fault(hw, channel, out_fault, out_reason);
}

static SE_MUST_USE err_h contract_hbridge_clear_fault(void* handle, uint8_t channel) {
  SYS_DEV_GET_ADAPTER_CONTEXT(drv8962_adapter_ctx_t, drv8962_handle_t, ctx, hw, handle);
  return drv8962_clear_fault(hw, channel);
}

static const sys_hbridge_contract_t s_drv8962_hbridge_contract = {
    .set_mode = contract_hbridge_set_mode,
    .set_drive = contract_hbridge_set_drive,
    .brake = contract_hbridge_brake,
    .coast = contract_hbridge_coast,
    .get_current_mA = contract_hbridge_get_current_mA,
    .set_current_limit_mA = contract_hbridge_set_current_limit_mA,
    .get_fault = contract_hbridge_get_fault,
    .clear_fault = contract_hbridge_clear_fault,
};

// --- Lifecycle Callbacks ---

// Install steps, recorded so teardown rolls back only what was actually built
enum { DRV_STEP_STARTED = 0 };

#define DRV_CHANNELS(ctx) (((ctx)->cfg.topology == DRV8962_TOPOLOGY_2_FULL_BRIDGES) ? 2 : 4)

/* Returns every dependency pin (possibly SYS_IO_PIN_NONE) to its unconfigured state. */
static SE_MUST_USE err_h reset_dependency_pins(const d_drv8962_cfg_t* cfg) {
  err_h err = NULL;
  for (int i = 0; i < 4; i++) {
    if (sys_io_pin_is_valid(cfg->in_pins[i])) SYS_DEV_TEARDOWN_STEP(err, sys_io_reset(cfg->in_pins[i]));
    if (sys_io_pin_is_valid(cfg->en_pins[i])) SYS_DEV_TEARDOWN_STEP(err, sys_io_reset(cfg->en_pins[i]));
    if (sys_io_pin_is_valid(cfg->current_adc_pins[i])) SYS_DEV_TEARDOWN_STEP(err, sys_io_reset(cfg->current_adc_pins[i]));
  }
  if (sys_io_pin_is_valid(cfg->nsleep_pin)) SYS_DEV_TEARDOWN_STEP(err, sys_io_reset(cfg->nsleep_pin));
  if (sys_io_pin_is_valid(cfg->nfault_pin)) SYS_DEV_TEARDOWN_STEP(err, sys_io_reset(cfg->nfault_pin));
  if (sys_io_pin_is_valid(cfg->vref_dac_pin)) SYS_DEV_TEARDOWN_STEP(err, sys_io_reset(cfg->vref_dac_pin));
  return err;
}

// Doubles as the install rollback path: no step may early-return - teardown
// must always free everything.
static SE_MUST_USE err_h device_uninstall(void* handle) {
  SYS_DEV_GET_ADAPTER_CONTEXT(drv8962_adapter_ctx_t, drv8962_handle_t, ctx, hw, handle);
  err_h err = NULL;

  IF_SYS_DEV_STEP_DONE(ctx, DRV_STEP_STARTED) {
    SYS_DEV_TEARDOWN_STEP(err, drv8962_stop(hw));
  }
  // drv8962_start configures pins one by one, so a partial start still needs them reset
  SYS_DEV_TEARDOWN_STEP(err, reset_dependency_pins(&ctx->cfg));

  free(hw);
  free(ctx);
  return err;
}

static SE_MUST_USE err_h device_reset(void* handle) {
  SYS_DEV_GET_ADAPTER_CONTEXT(drv8962_adapter_ctx_t, drv8962_handle_t, ctx, hw, handle);
  err_h err = NULL;
  for (uint8_t i = 0; i < DRV_CHANNELS(ctx); i++) {
    SYS_DEV_TEARDOWN_STEP(err, drv8962_brake(hw, i));
  }
  return err;
}

// Fault safe state: every channel must be reached even if one fails.
static SE_MUST_USE err_h device_suspend(void* handle) {
  SYS_DEV_GET_ADAPTER_CONTEXT(drv8962_adapter_ctx_t, drv8962_handle_t, ctx, hw, handle);
  err_h err = NULL;
  for (uint8_t i = 0; i < DRV_CHANNELS(ctx); i++) {
    SYS_DEV_TEARDOWN_STEP(err, drv8962_coast(hw, i));
  }
  return err;
}

// Channels stay coasted after a suspend; the next drive command re-engages them.
static SE_MUST_USE err_h device_resume(void* handle) {
  (void)handle;
  return NULL;
}

static SE_MUST_USE err_h device_freeze(void* handle) {
  return device_reset(handle);
}

static SE_MUST_USE err_h device_sync(void* handle) {
  (void)handle;
  return NULL;
}

static SE_MUST_USE err_h device_install(const void* cfg_blob, void** out_device_handle) {
  const d_drv8962_cfg_t* cfg = (const d_drv8962_cfg_t*)cfg_blob;

  SYS_DEV_CTX_NEW(drv8962_adapter_ctx_t, ctx, cfg);
  err_h err = NULL;

  ctx->base.hw_handle = drv8962_new(&ctx->cfg);
  if (!ctx->base.hw_handle) {
    free(ctx);
    SE_FAIL(ERR_BASE_NO_MEM, 0);
  }
  drv8962_handle_t hw = (drv8962_handle_t)ctx->base.hw_handle;

  SYS_DEV_INSTALL_STEP(drv8962_start(hw), "start (pins, nFAULT, brake)");
  SYS_DEV_STEP_DONE(ctx, DRV_STEP_STARTED);

  *out_device_handle = ctx;
  return NULL;

fail:
  SYS_DEV_INSTALL_FAIL(err, cfg->device_id, out_device_handle, device_uninstall, ctx);
  return NULL;
}

static const sys_device_class_t s_drv8962_class = {
    .name = "DRV8962_BRIDGE_DRIVER",
    .contracts = {[SYS_DEVICE_CONTRACT_HBRIDGE] = &s_drv8962_hbridge_contract},
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

