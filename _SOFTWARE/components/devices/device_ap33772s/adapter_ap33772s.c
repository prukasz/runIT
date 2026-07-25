#include <stdlib.h>
#include "device_ap33772s.h"
#include "driver_ap33772s.h"
#include "esp_log.h"
#include "sys_device.h"
#include "sys_error.h"
#include "sys_i2c.h"
#include "sys_io.h"
#include "sys_power.h"

#define TAG "AP33772S_ADAPTER"
#undef OWNER
#define OWNER OWNER_DEVICE_AP33772S

// --- 1. The Encapsulated Adapter Context ---
typedef struct ap_adapter_ctx_t {
  sys_device_adapter_base_t base;

  d_ap33772s_cfg_t cfg;

  // Caching mechanism for freeze/sync (Read-only get voltage and current)
  uint32_t cached_voltage_mv;
  int32_t cached_current_ma;

  // Tracked VREG target values
  uint32_t last_voltage_mv;
  uint32_t last_current_ma;
  bool is_enabled;

  uint16_t route_mask;
} ap_adapter_ctx_t;

enum { AP33772S_STEP_I2C_ADDED = 0, AP33772S_STEP_INTR_READY = 1 };

#define get_hw_handle(ctx) ((ap33772s_handle_t)((ctx)->base.hw_handle))

// ap33772s_adapter_isr removed as it is handled by the system callbacks framework

// --- 2. VREG Contract Implementations ---

static err_h d_ap33772s_set_enable(void* device_handle, bool state) {
  SYS_DEV_GET_ADAPTER_CONTEXT(ap_adapter_ctx_t, ap33772s_handle_t, ctx, hw, device_handle);

  ctx->is_enabled = state;
  SYS_DEV_CHECK_DRIVER_CALL(ap33772s_set_output(hw, state), ctx);
  return NULL;
}

static err_h negotiate_pdo(ap_adapter_ctx_t* ctx, uint32_t voltage_mv, uint32_t current_ma) {
  ap33772s_handle_t hw = get_hw_handle(ctx);
  SE_CHECK_HANDLE(hw);

  // 1. Try PPS
  if (hw->index_pps_user != -1) {
    src_spr_and_epr_pdo_fields_t active_pdo = hw->src_pdo_array[hw->index_pps_user - 1];
    int voltage_min_decoded = (active_pdo.pps.voltage_min > 0) ? 3300 : 0;
    int voltage_max_decoded = active_pdo.pps.voltage_max * 100;

    if (voltage_mv >= voltage_min_decoded && voltage_mv <= voltage_max_decoded) {
      esp_err_t err = ap33772s_set_pps_pdo(hw, hw->index_pps_user, voltage_mv, current_ma);
      if (err == ESP_OK) return NULL;
    }
  }

  // 2. Try AVS
  if (hw->index_avs_user != -1) {
    src_spr_and_epr_pdo_fields_t active_pdo = hw->src_pdo_array[hw->index_avs_user - 1];
    int voltage_min_decoded = (active_pdo.avs.voltage_min > 0) ? 15000 : 0;
    int voltage_max_decoded = active_pdo.avs.voltage_max * 200;

    if (voltage_mv >= voltage_min_decoded && voltage_mv <= voltage_max_decoded) {
      esp_err_t err = ap33772s_set_avs_pdo(hw, hw->index_avs_user, voltage_mv, current_ma);
      if (err == ESP_OK) return NULL;
    }
  }

  // 3. Fallback to Fixed
  int best_pdo_index = -1;
  int best_voltage_diff = 1000000;

  for (int i = 1; i <= MAX_PDO_ENTRIES; i++) {
    src_spr_and_epr_pdo_fields_t pdo = hw->src_pdo_array[i - 1];
    if (pdo.fixed.type == 0 && (pdo.byte0 != 0 || pdo.byte1 != 0)) {
      bool isEPR = (i >= 8);
      int pdo_volt_mv = pdo.fixed.voltage_max * (isEPR ? 200 : 100);

      if (pdo_volt_mv <= voltage_mv) {
        int diff = voltage_mv - pdo_volt_mv;
        if (diff < best_voltage_diff) {
          best_voltage_diff = diff;
          best_pdo_index = i;
        }
      }
    }
  }

  return SE_CONVERT_ESP(ap33772s_set_fixed_pdo(hw, best_pdo_index, current_ma));

  SE_RET_ERR(ERR_INVALID_VAL_UI32, voltage_mv, 3300, 21000);
}

static err_h d_ap33772s_set_voltage(void* device_handle, uint32_t voltage_mV) {
  ap_adapter_ctx_t* ctx = (ap_adapter_ctx_t*)device_handle;
  SE_CHECK_HANDLE(ctx);
  ctx->last_voltage_mv = voltage_mV;
  return negotiate_pdo(ctx, ctx->last_voltage_mv, ctx->last_current_ma);
}

static err_h d_ap33772s_set_current(void* device_handle, uint32_t current_mA) {
  ap_adapter_ctx_t* ctx = (ap_adapter_ctx_t*)device_handle;
  SE_CHECK_HANDLE(ctx);
  ctx->last_current_ma = current_mA;
  return negotiate_pdo(ctx, ctx->last_voltage_mv, ctx->last_current_ma);
}

static err_h d_ap33772s_add_callback(void* device_handle, sys_power_events_e on_event, uint16_t route_mask) {
  ap_adapter_ctx_t* ctx = (ap_adapter_ctx_t*)device_handle;
  SE_CHECK_HANDLE(ctx);
  ctx->route_mask = route_mask;
  return NULL;
}

static const sys_power_vreg_contract s_ap_vreg_contract = {.set_enable = d_ap33772s_set_enable, .set_voltage = d_ap33772s_set_voltage, .set_current = d_ap33772s_set_current, .add_callback = d_ap33772s_add_callback};

// --- 3. USB PD Contract Implementations ---

static err_h d_ap33772s_set_settings(void* device_handle, uint32_t voltage_mV, uint32_t current_mA) {
  ap_adapter_ctx_t* ctx = (ap_adapter_ctx_t*)device_handle;
  SE_CHECK_HANDLE(ctx);
  ctx->last_voltage_mv = voltage_mV;
  ctx->last_current_ma = current_mA;
  return negotiate_pdo(ctx, voltage_mV, current_mA);
}

static err_h d_ap33772s_list_options(void* device_handle) {
  SYS_DEV_GET_ADAPTER_CONTEXT(ap_adapter_ctx_t, ap33772s_handle_t, ctx, hw, device_handle);
  ap33772s_log_profiles(hw);
  return NULL;
}

static err_h d_ap33772s_get_limits(void* device_handle, uint32_t* out_mV, uint32_t* out_mA) {
  SYS_DEV_GET_ADAPTER_CONTEXT(ap_adapter_ctx_t, ap33772s_handle_t, ctx, hw, device_handle);
  SE_CHECK_NOT_NULL(out_mV);
  SE_CHECK_NOT_NULL(out_mA);

  uint32_t max_mv = 0;
  uint32_t max_ma = 0;

  for (int i = 1; i <= MAX_PDO_ENTRIES; i++) {
    src_spr_and_epr_pdo_fields_t pdo = hw->src_pdo_array[i - 1];
    if (pdo.byte0 != 0 || pdo.byte1 != 0) {
      uint32_t pdo_mv = 0;
      uint32_t pdo_ma = 0;
      if (pdo.fixed.type == 0) {
        bool isEPR = (i >= 8);
        pdo_mv = pdo.fixed.voltage_max * (isEPR ? 200 : 100);
        if (pdo.fixed.current_max >= 15)
          pdo_ma = 5000;
        else if (pdo.fixed.current_max >= 14)
          pdo_ma = 4500;
        else
          pdo_ma = pdo.fixed.current_max * 250 + 1250;
      } else if (pdo.pps.type == 1 && i < 8) {
        pdo_mv = pdo.pps.voltage_max * 100;
        pdo_ma = pdo.pps.current_max * 50;
      } else if (pdo.avs.type == 1 && i >= 8) {
        pdo_mv = pdo.avs.voltage_max * 200;
        pdo_ma = pdo.avs.current_max * 50;
      }
      if (pdo_mv > max_mv) {
        max_mv = pdo_mv;
        max_ma = pdo_ma;
      }
    }
  }

  *out_mV = max_mv;
  *out_mA = max_ma;
  return NULL;
}

static const sys_power_usb_pd_contract s_ap_usb_pd_contract = {.set_settings = d_ap33772s_set_settings, .list_options = d_ap33772s_list_options, .get_limits = d_ap33772s_get_limits};

// --- 4. Monitor Contract (for get voltage & current telemetry) ---

static err_h d_ap33772s_get_telemetry_voltage(void* device_handle, uint8_t channel, int32_t* out_mV) {
  ap_adapter_ctx_t* ctx = (ap_adapter_ctx_t*)device_handle;
  SE_CHECK_HANDLE(ctx);
  SE_CHECK_NOT_NULL(out_mV);

  IF_SYS_DEV_FROZEN(ctx) {
    *out_mV = ctx->cached_voltage_mv;
    return NULL;
  }

  ap33772s_handle_t hw = get_hw_handle(ctx);
  SE_CHECK_HANDLE(hw);

  int vol = ap33772s_read_voltage(hw);
  if (vol < 0) SE_RET_ERR(ERR_ESP_ERR, 0);
  *out_mV = vol;
  return NULL;
}

static err_h d_ap33772s_get_telemetry_current(void* device_handle, uint8_t channel, int32_t* out_mA) {
  ap_adapter_ctx_t* ctx = (ap_adapter_ctx_t*)device_handle;
  SE_CHECK_HANDLE(ctx);
  SE_CHECK_NOT_NULL(out_mA);

  IF_SYS_DEV_FROZEN(ctx) {
    *out_mA = ctx->cached_current_ma;
    return NULL;
  }

  ap33772s_handle_t hw = get_hw_handle(ctx);
  SE_CHECK_HANDLE(hw);

  int curr = ap33772s_read_current(hw);
  if (curr < 0) SE_RET_ERR(ERR_ESP_ERR, 0);
  *out_mA = curr;
  return NULL;
}

static const sys_power_monitor_contract s_ap_monitor_contract = {.get_voltage = d_ap33772s_get_telemetry_voltage, .get_current = d_ap33772s_get_telemetry_current, .add_callback = NULL};

// --- 5. sys_device_t VTable Implementations ---

static err_h device_uninstall(void* handle) {
  SYS_DEV_GET_ADAPTER_CONTEXT(ap_adapter_ctx_t, ap33772s_handle_t, ctx, hw, handle);
  err_h err = NULL;

  IF_SYS_DEV_STEP_DONE(ctx, AP33772S_STEP_INTR_READY) {
    SYS_IO_REF_UNLOCK(ctx->cfg.intr_pin);
    SYS_DEV_TEARDOWN_STEP(err, SYS_IO_REF_RESET(ctx->cfg.intr_pin));
  }
  if (ctx->base.hw_handle) {
    IF_SYS_DEV_STEP_DONE(ctx, AP33772S_STEP_I2C_ADDED) {
      SYS_DEV_TEARDOWN_STEP(err, sys_i2c_remove_driver(ctx->base.hw_handle));
    }
    ap33772s_delete(hw);
  }

  free(ctx);
  return err;
}

static err_h adapter_reset_device(void* driver_handle) {
  SYS_DEV_GET_ADAPTER_CONTEXT(ap_adapter_ctx_t, ap33772s_handle_t, ctx, hw, driver_handle);

  SYS_DEV_CHECK_DRIVER_CALL(ap33772s_set_output(hw, false), ctx);
  return NULL;
}

static err_h adapter_error_handler(void* driver_handle, err_h error) {
  if (!error) return NULL;
  ESP_LOGE(TAG, "AP33772S Error: owner=%u, tag=%d", (unsigned int)error->owner, (int)error->tag);
  return error;
}

static err_h adapter_suspend_device(void* driver_handle) {
  SYS_DEV_GET_ADAPTER_CONTEXT(ap_adapter_ctx_t, ap33772s_handle_t, ctx, hw, driver_handle);
  SYS_DEV_CHECK_DRIVER_CALL(ap33772s_set_output(hw, false), ctx);
  return NULL;
}

static err_h adapter_resume_device(void* driver_handle) {
  SYS_DEV_GET_ADAPTER_CONTEXT(ap_adapter_ctx_t, ap33772s_handle_t, ctx, hw, driver_handle);
  SYS_DEV_CHECK_DRIVER_CALL(ap33772s_set_output(hw, ctx->is_enabled), ctx);
  return NULL;
}

static err_h adapter_freeze_device(void* driver_handle) {
  SYS_DEV_GET_ADAPTER_CONTEXT(ap_adapter_ctx_t, ap33772s_handle_t, ctx, hw, driver_handle);
  ctx->base.is_frozen = true;

  int vol = ap33772s_read_voltage(hw);
  int curr = ap33772s_read_current(hw);
  ctx->cached_voltage_mv = (vol >= 0) ? vol : 0;
  ctx->cached_current_ma = (curr >= 0) ? curr : 0;

  return NULL;
}

static err_h adapter_sync_device(void* driver_handle) {
  ap_adapter_ctx_t* ctx = (ap_adapter_ctx_t*)driver_handle;
  SE_CHECK_HANDLE(ctx);
  ctx->base.is_frozen = false;
  return NULL;
}

static err_h device_install(const void* cfg_blob, void** out_device_handle) {
  const d_ap33772s_cfg_t* cfg = (const d_ap33772s_cfg_t*)cfg_blob;
  SE_CHECK_NOT_NULL(cfg);
  SE_CHECK_NOT_NULL(out_device_handle);

  SYS_DEV_CTX_NEW(ap_adapter_ctx_t, ctx, cfg);
  err_h err = NULL;

  ctx->last_voltage_mv = 5000;
  ctx->last_current_ma = 500;
  ctx->is_enabled = false;

  ctx->base.hw_handle = ap33772s_new(ctx->cfg.i2c_bus);
  if (!ctx->base.hw_handle) {
    free(ctx);
    SE_RET_ERR(ERR_BASE_NO_MEM, 0);
  }

  ap33772s_handle_t hw = get_hw_handle(ctx);

  hw->header.i2c_device_config.device_address = ctx->cfg.i2c_addr;
  hw->header.transmit = sys_i2c_master_transmit;
  hw->header.transmit_receive = sys_i2c_master_transmit_receive;

  SYS_DEV_INSTALL_STEP(SE_CONVERT_ESP(ap33772s_start(hw)), "ap start");
  SYS_DEV_INSTALL_STEP(sys_i2c_add_driver(ctx->base.hw_handle), "i2c add driver");
  SYS_DEV_STEP_DONE(ctx, AP33772S_STEP_I2C_ADDED);

  SYS_DEV_INSTALL_STEP(sys_i2c_device_present(ctx->base.hw_handle), "probe i2c device");

  IF_PIN_REF(ctx->cfg.intr_pin) {
    SYS_DEV_INSTALL_STEP(SYS_IO_REF_SET_MODE(ctx->cfg.intr_pin), "intr pin mode");
    sys_io_intr_config_t config = {.mode = SYS_IO_INTR_MODE_FALLING_EDGE};
    SYS_DEV_INSTALL_STEP(sys_io_configure_intr(ctx->cfg.intr_pin.device_id, ctx->cfg.intr_pin.pin, &config), "intr pin configure");
    SYS_IO_REF_LOCK(ctx->cfg.intr_pin);
    SYS_DEV_STEP_DONE(ctx, AP33772S_STEP_INTR_READY);
  }

  SYS_DEV_INSTALL_STEP(SE_CONVERT_ESP(ap33772s_begin(hw)), "ap begin");

  *out_device_handle = ctx;
  return NULL;

fail:
  SYS_DEV_INSTALL_FAIL(err, cfg->device_id, out_device_handle, device_uninstall, ctx);
  return NULL;
}

static const sys_device_class_t s_ap33772s_class = {
    .name = "AP33772S",
    .roles = SYS_DEV_ROLE_PWR,
    .contracts = {[SYS_DEVICE_CONTRACT_POWER_VREG] = (void*)&s_ap_vreg_contract, [SYS_DEVICE_CONTRACT_POWER_USB_PD] = (void*)&s_ap_usb_pd_contract, [SYS_DEVICE_CONTRACT_POWER_MONITOR] = (void*)&s_ap_monitor_contract},
    .ops = {.install = device_install, .uninstall = device_uninstall, .reset = adapter_reset_device, .suspend = adapter_suspend_device, .resume = adapter_resume_device, .freeze = adapter_freeze_device, .sync = adapter_sync_device, .error_handler = adapter_error_handler},
};

err_h d_ap33772s_create(const d_ap33772s_cfg_t* cfg) {
  SE_CHECK_NOT_NULL(cfg);
  return SYS_DEVICE_CREATE(&s_ap33772s_class, cfg);
}
