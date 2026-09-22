#include <stdlib.h>
#include "device_ap33772s.h"
#include "driver_ap33772s.h"
#include "sys_device.h"
#include "sys_error.h"
#include "sys_i2c.h"
#include "sys_io.h"
#include "sys_power.h"

#undef OWNER
#define OWNER OWNER_DEVICE_AP33772S

// --- 1. The Encapsulated Adapter Context ---
typedef struct ap_adapter_ctx_t {
  sys_device_adapter_base_t base;

  d_ap33772s_cfg_t cfg;

  // Caching mechanism for freeze/sync (Read-only get voltage and current)
  uint32_t cached_voltage_mV;
  int32_t cached_current_mA;

  // Tracked VREG target values
  uint32_t last_voltage_mV;
  uint32_t last_current_mA;
  bool is_enabled;
} ap_adapter_ctx_t;

enum { AP33772S_STEP_I2C_ADDED = 0, AP33772S_STEP_INTR_READY = 1 };

#define get_hw_handle(ctx) ((ap33772s_handle_t)((ctx)->base.hw_handle))

// ap33772s_adapter_isr removed as it is handled by the system callbacks framework

// --- 2. VREG Contract Implementations ---

static SE_MUST_USE err_h d_ap33772s_set_enable(void* device_handle, bool state) {
  SYS_DEV_GET_ADAPTER_CONTEXT(ap_adapter_ctx_t, ap33772s_handle_t, ctx, hw, device_handle);

  ctx->is_enabled = state;
  SYS_DEV_CHECK_DRIVER_CALL(ap33772s_set_output(hw, state), ctx);
  return NULL;
}

static SE_MUST_USE err_h negotiate_pdo(ap_adapter_ctx_t* ctx, uint32_t voltage_mV, uint32_t current_mA) {
  ap33772s_handle_t hw = get_hw_handle(ctx);
  SE_CHECK_HANDLE(hw);
  SE_CHECK_IN_RANGE(voltage_mV, 0, AP33772S_MAX_SOFTWARE_VOLTAGE_MV);

  // 1. Try PPS
  if (hw->index_pps_user != -1) {
    src_spr_and_epr_pdo_fields_t active_pdo = hw->src_pdo_array[hw->index_pps_user - 1];
    int voltage_min_decoded = (active_pdo.pps.voltage_min > 0) ? 3300 : 0;
    int voltage_max_decoded = active_pdo.pps.voltage_max * 100;

    if (voltage_mV >= voltage_min_decoded && voltage_mV <= voltage_max_decoded) {
      esp_err_t err = ap33772s_set_pps_pdo(hw, hw->index_pps_user, voltage_mV, current_mA);
      if (err == ESP_OK) return NULL;
    }
  }

  // 2. Try AVS
  if (hw->index_avs_user != -1) {
    src_spr_and_epr_pdo_fields_t active_pdo = hw->src_pdo_array[hw->index_avs_user - 1];
    int voltage_min_decoded = (active_pdo.avs.voltage_min > 0) ? 15000 : 0;
    int voltage_max_decoded = active_pdo.avs.voltage_max * 200;

    if (voltage_mV >= voltage_min_decoded && voltage_mV <= voltage_max_decoded) {
      esp_err_t err = ap33772s_set_avs_pdo(hw, hw->index_avs_user, voltage_mV, current_mA);
      if (err == ESP_OK) return NULL;
    }
  }

  // 3. Fallback to Fixed
  int best_pdo_index = -1;
  int best_voltage_diff = 1000000;
  uint32_t lowest_fixed_mV = AP33772S_MAX_SOFTWARE_VOLTAGE_MV;

  for (int i = 1; i <= MAX_PDO_ENTRIES; i++) {
    src_spr_and_epr_pdo_fields_t pdo = hw->src_pdo_array[i - 1];
    if (pdo.fixed.type == 0 && (pdo.byte0 != 0 || pdo.byte1 != 0)) {
      bool isEPR = (i >= 8);
      int pdo_volt_mV = pdo.fixed.voltage_max * (isEPR ? 200 : 100);
      if ((uint32_t)pdo_volt_mV < lowest_fixed_mV) lowest_fixed_mV = (uint32_t)pdo_volt_mV;

      if (pdo_volt_mV <= voltage_mV) {
        int diff = voltage_mV - pdo_volt_mV;
        if (diff < best_voltage_diff) {
          best_voltage_diff = diff;
          best_pdo_index = i;
        }
      }
    }
  }

  // No offered fixed profile is at or below the request: report the usable range
  if (best_pdo_index == -1) {
    SE_FAIL(ERR_INVALID_VAL_UI32, .val = voltage_mV, .min = lowest_fixed_mV, .max = AP33772S_MAX_SOFTWARE_VOLTAGE_MV);
  }
  SYS_DEV_CHECK_DRIVER_CALL(ap33772s_set_fixed_pdo(hw, best_pdo_index, current_mA), ctx);
  return NULL;
}

static SE_MUST_USE err_h d_ap33772s_set_voltage(void* device_handle, uint32_t voltage_mV) {
  ap_adapter_ctx_t* ctx = (ap_adapter_ctx_t*)device_handle;
  SE_CHECK_HANDLE(ctx);
  ctx->last_voltage_mV = voltage_mV;
  return negotiate_pdo(ctx, ctx->last_voltage_mV, ctx->last_current_mA);
}

static SE_MUST_USE err_h d_ap33772s_set_current(void* device_handle, uint32_t current_mA) {
  ap_adapter_ctx_t* ctx = (ap_adapter_ctx_t*)device_handle;
  SE_CHECK_HANDLE(ctx);
  ctx->last_current_mA = current_mA;
  return negotiate_pdo(ctx, ctx->last_voltage_mV, ctx->last_current_mA);
}

static const sys_power_vreg_contract_t s_ap33772s_vreg_contract = {.set_enable = d_ap33772s_set_enable, .set_voltage = d_ap33772s_set_voltage, .set_current = d_ap33772s_set_current};

// --- 3. USB PD Contract Implementations ---

static SE_MUST_USE err_h d_ap33772s_set_settings(void* device_handle, uint32_t voltage_mV, uint32_t current_mA) {
  ap_adapter_ctx_t* ctx = (ap_adapter_ctx_t*)device_handle;
  SE_CHECK_HANDLE(ctx);
  ctx->last_voltage_mV = voltage_mV;
  ctx->last_current_mA = current_mA;
  return negotiate_pdo(ctx, voltage_mV, current_mA);
}

/* Upper end of a PDO current range code (datasheet: 0 = below 1.25 A,
   n = 1.00 + 0.25n A, 14 = 4.50 A, 15 = 5.00 A and above). Same code for
   fixed, PPS and AVS objects. */
static uint16_t pdo_max_mA(unsigned int code) {
  if (code >= 15) return 5000;
  if (code == 14) return 4500;
  return (uint16_t)(code * 250 + 1250);
}

/* Decode source PDO `slot` (1-based; slots 8-13 are EPR). False if empty. */
static bool pdo_to_option(const src_spr_and_epr_pdo_fields_t* pdo, uint8_t slot, sys_power_usb_pd_option_t* out) {
  if (pdo->byte0 == 0 && pdo->byte1 == 0) return false;
  bool is_epr = (slot >= 8);
  out->slot = slot;
  out->max_mA = pdo_max_mA(pdo->fixed.current_max);
  if (pdo->fixed.type == 0) {
    out->type = SYS_POWER_USB_PD_OPTION_FIXED;
    out->max_mV = (uint16_t)(pdo->fixed.voltage_max * (is_epr ? 200 : 100));
    out->min_mV = out->max_mV;
  } else if (!is_epr) {
    out->type = SYS_POWER_USB_PD_OPTION_PPS;
    out->max_mV = (uint16_t)(pdo->pps.voltage_max * 100);
    out->min_mV = (pdo->pps.voltage_min > 0) ? 3300 : 0;
  } else {
    out->type = SYS_POWER_USB_PD_OPTION_AVS;
    out->max_mV = (uint16_t)(pdo->avs.voltage_max * 200);
    out->min_mV = (pdo->avs.voltage_min > 0) ? 15000 : 0;
  }
  return true;
}

static SE_MUST_USE err_h d_ap33772s_list_options(void* device_handle, sys_power_usb_pd_option_t* out_options, uint8_t max_options, uint8_t* out_count) {
  SYS_DEV_GET_ADAPTER_CONTEXT(ap_adapter_ctx_t, ap33772s_handle_t, ctx, hw, device_handle);
  uint8_t count = 0;
  for (uint8_t slot = 1; slot <= MAX_PDO_ENTRIES && count < max_options; slot++) {
    if (pdo_to_option(&hw->src_pdo_array[slot - 1], slot, &out_options[count])) count++;
  }
  *out_count = count;
  return NULL;
}

static SE_MUST_USE err_h d_ap33772s_get_limits(void* device_handle, int32_t* out_mV, int32_t* out_mA) {
  SYS_DEV_GET_ADAPTER_CONTEXT(ap_adapter_ctx_t, ap33772s_handle_t, ctx, hw, device_handle);
  SE_CHECK_NOT_NULL(out_mV);
  SE_CHECK_NOT_NULL(out_mA);

  uint32_t max_mV = 0;
  uint32_t max_mA = 0;

  // The highest-voltage offer sets the limits.
  for (uint8_t slot = 1; slot <= MAX_PDO_ENTRIES; slot++) {
    sys_power_usb_pd_option_t option;
    if (pdo_to_option(&hw->src_pdo_array[slot - 1], slot, &option) && option.max_mV > max_mV) {
      max_mV = option.max_mV;
      max_mA = option.max_mA;
    }
  }

  *out_mV = (int32_t)max_mV;
  *out_mA = (int32_t)max_mA;
  return NULL;
}

static const sys_power_usb_pd_contract_t s_ap33772s_usb_pd_contract = {.set_settings = d_ap33772s_set_settings, .list_options = d_ap33772s_list_options, .get_limits = d_ap33772s_get_limits};

// --- 4. Monitor Contract (for get voltage & current telemetry) ---

static SE_MUST_USE err_h d_ap33772s_get_telemetry_voltage(void* device_handle, uint8_t channel, int32_t* out_mV) {
  ap_adapter_ctx_t* ctx = (ap_adapter_ctx_t*)device_handle;
  SE_CHECK_HANDLE(ctx);
  SE_CHECK_NOT_NULL(out_mV);

  IF_SYS_DEV_FROZEN(ctx) {
    *out_mV = ctx->cached_voltage_mV;
    return NULL;
  }

  ap33772s_handle_t hw = get_hw_handle(ctx);
  SE_CHECK_HANDLE(hw);

  int vol = ap33772s_read_voltage(hw);
  if (vol < 0) SE_FAIL(ERR_DEV_DRIVER_FAILED, .dev_id = SYS_DEV_GET_ID(ctx), .line = __LINE__);
  *out_mV = vol;
  return NULL;
}

static SE_MUST_USE err_h d_ap33772s_get_telemetry_current(void* device_handle, uint8_t channel, int32_t* out_mA) {
  ap_adapter_ctx_t* ctx = (ap_adapter_ctx_t*)device_handle;
  SE_CHECK_HANDLE(ctx);
  SE_CHECK_NOT_NULL(out_mA);

  IF_SYS_DEV_FROZEN(ctx) {
    *out_mA = ctx->cached_current_mA;
    return NULL;
  }

  ap33772s_handle_t hw = get_hw_handle(ctx);
  SE_CHECK_HANDLE(hw);

  int curr = ap33772s_read_current(hw);
  if (curr < 0) SE_FAIL(ERR_DEV_DRIVER_FAILED, .dev_id = SYS_DEV_GET_ID(ctx), .line = __LINE__);
  *out_mA = curr;
  return NULL;
}

static const sys_power_monitor_contract_t s_ap33772s_monitor_contract = {.get_voltage = d_ap33772s_get_telemetry_voltage, .get_current = d_ap33772s_get_telemetry_current};

// --- 5. sys_device_t VTable Implementations ---

/* Driver background-task failure (AVS keep-alive): there is no caller to
   return to, so raise it here as a device error with this device's id. */
static void on_driver_error(void* arg, esp_err_t esp_code) {
  ap_adapter_ctx_t* ctx = (ap_adapter_ctx_t*)arg;
  SE_push_to_handler(SYS_DEV_DRIVER_ERR(esp_code, ctx));
}

static SE_MUST_USE err_h device_uninstall(void* handle) {
  SYS_DEV_GET_ADAPTER_CONTEXT(ap_adapter_ctx_t, ap33772s_handle_t, ctx, hw, handle);
  err_h err = NULL;

  IF_SYS_DEV_STEP_DONE(ctx, AP33772S_STEP_INTR_READY) {
    SYS_DEV_TEARDOWN_STEP(err, sys_io_unlock_pin(ctx->cfg.intr_pin));
    SYS_DEV_TEARDOWN_STEP(err, sys_io_reset(ctx->cfg.intr_pin));
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

static SE_MUST_USE err_h adapter_reset_device(void* driver_handle) {
  SYS_DEV_GET_ADAPTER_CONTEXT(ap_adapter_ctx_t, ap33772s_handle_t, ctx, hw, driver_handle);

  SYS_DEV_CHECK_DRIVER_CALL(ap33772s_set_output(hw, false), ctx);
  return NULL;
}

// Same shape as device_pca9685's explain_root_cause() (see that file for
// the full rationale): identifies which node in the chain is the root
static SE_MUST_USE err_h adapter_suspend_device(void* driver_handle) {
  SYS_DEV_GET_ADAPTER_CONTEXT(ap_adapter_ctx_t, ap33772s_handle_t, ctx, hw, driver_handle);
  SYS_DEV_CHECK_DRIVER_CALL(ap33772s_set_output(hw, false), ctx);
  return NULL;
}

static SE_MUST_USE err_h adapter_resume_device(void* driver_handle) {
  SYS_DEV_GET_ADAPTER_CONTEXT(ap_adapter_ctx_t, ap33772s_handle_t, ctx, hw, driver_handle);
  SYS_DEV_CHECK_DRIVER_CALL(ap33772s_set_output(hw, ctx->is_enabled), ctx);
  return NULL;
}

static SE_MUST_USE err_h adapter_freeze_device(void* driver_handle) {
  SYS_DEV_GET_ADAPTER_CONTEXT(ap_adapter_ctx_t, ap33772s_handle_t, ctx, hw, driver_handle);

  // Snapshot first; the device only counts as frozen once the snapshot is complete
  int vol = ap33772s_read_voltage(hw);
  if (vol < 0) SE_FAIL(ERR_DEV_DRIVER_FAILED, .dev_id = SYS_DEV_GET_ID(ctx), .line = __LINE__);
  int curr = ap33772s_read_current(hw);
  if (curr < 0) SE_FAIL(ERR_DEV_DRIVER_FAILED, .dev_id = SYS_DEV_GET_ID(ctx), .line = __LINE__);
  ctx->cached_voltage_mV = (uint32_t)vol;
  ctx->cached_current_mA = curr;
  ctx->base.is_frozen = true;

  return NULL;
}

static SE_MUST_USE err_h adapter_sync_device(void* driver_handle) {
  ap_adapter_ctx_t* ctx = (ap_adapter_ctx_t*)driver_handle;
  SE_CHECK_HANDLE(ctx);
  ctx->base.is_frozen = false;
  return NULL;
}

static SE_MUST_USE err_h device_install(const void* cfg_blob, void** out_device_handle) {
  const d_ap33772s_cfg_t* cfg = (const d_ap33772s_cfg_t*)cfg_blob;

  SYS_DEV_CTX_NEW(ap_adapter_ctx_t, ctx, cfg);
  err_h err = NULL;

  ctx->last_voltage_mV = 5000;
  ctx->last_current_mA = 500;
  ctx->is_enabled = false;

  ctx->base.hw_handle = ap33772s_new(ctx->cfg.i2c_bus);
  if (!ctx->base.hw_handle) {
    free(ctx);
    SE_FAIL(ERR_BASE_NO_MEM, 0);
  }

  ap33772s_handle_t hw = get_hw_handle(ctx);

  hw->header.i2c_device_config.device_address = ctx->cfg.i2c_addr;
  hw->header.transmit = sys_i2c_master_transmit;
  hw->header.transmit_receive = sys_i2c_master_transmit_receive;
  hw->error_callback = on_driver_error;
  hw->error_arg = ctx;

  SYS_DEV_INSTALL_STEP(sys_i2c_add_driver(ctx->base.hw_handle), "i2c add driver");
  SYS_DEV_STEP_DONE(ctx, AP33772S_STEP_I2C_ADDED);

  SYS_DEV_INSTALL_STEP(sys_i2c_device_present(ctx->base.hw_handle), "probe i2c device");
  SYS_DEV_INSTALL_STEP(SE_CONVERT_ESP(ap33772s_start(hw)), "ap start");

  if (sys_io_pin_is_valid(ctx->cfg.intr_pin)) {
    SYS_DEV_INSTALL_STEP(sys_io_set_mode(ctx->cfg.intr_pin), "intr pin mode");
    sys_io_intr_config_t config = {.mode = SYS_IO_INTR_MODE_FALLING_EDGE};
    SYS_DEV_INSTALL_STEP(sys_io_configure_intr(ctx->cfg.intr_pin, &config), "intr pin configure");
    SYS_DEV_INSTALL_STEP(sys_io_lock_pin(ctx->cfg.intr_pin), "intr pin lock");
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
    .contracts = {[SYS_DEVICE_CONTRACT_POWER_VREG] = &s_ap33772s_vreg_contract, [SYS_DEVICE_CONTRACT_POWER_USB_PD] = &s_ap33772s_usb_pd_contract, [SYS_DEVICE_CONTRACT_POWER_MONITOR] = &s_ap33772s_monitor_contract},
    .ops = {.install = device_install, .uninstall = device_uninstall, .reset = adapter_reset_device, .suspend = adapter_suspend_device, .resume = adapter_resume_device, .freeze = adapter_freeze_device, .sync = adapter_sync_device},
};

err_h d_ap33772s_create(const d_ap33772s_cfg_t* cfg) {
  SE_CHECK_NOT_NULL(cfg);
  return SYS_DEVICE_CREATE(&s_ap33772s_class, cfg);
}
