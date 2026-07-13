#include <stdlib.h>
#include "device_ap33772s.h"
#include "driver_ap33772s.h"
#include "esp_log.h"
#include "status.h"
#include "sys_device.h"
#include "sys_i2c.h"
#include "sys_io.h"
#include "sys_power.h"

#define TAG "AP33772S_ADAPTER"
#undef OWNER
#define OWNER OWNER_DEVICE_AP33772S

// --- 1. The Encapsulated Adapter Context ---
typedef struct ap_adapter_ctx_t {
  sys_device_adapter_base_t base;

  uint8_t intr_gpio_device_id;
  sys_io_pin_num_t intr_gpio_pin_num;

  // Caching mechanism for freeze/sync (Read-only get voltage and current)
  uint32_t cached_voltage_mv;
  int32_t cached_current_ma;

  // Tracked VREG target values
  uint32_t last_voltage_mv;
  uint32_t last_current_ma;
  bool is_enabled;

  void (*power_callback_event)(uint8_t device_id, sys_power_events_e triggered_by);
} ap_adapter_ctx_t;

#define get_hw_handle(ctx) ((ap33772s_handle_t)((ctx)->base.hw_handle))

static void ap33772s_adapter_isr(void* arg) {
  ap_adapter_ctx_t* ctx = (ap_adapter_ctx_t*)arg;
  if (!ctx) return;
  ap33772s_handle_t hw = get_hw_handle(ctx);
  if (hw) {
    ap33772s_intr_handler(hw);
  }
}

// --- 2. VREG Contract Implementations ---

static status_rep_t d_ap33772s_set_enable(void* device_handle, bool state) {
  SYS_DEV_GET_ADAPTER_CONTEXT(ap_adapter_ctx_t, ap33772s_handle_t, ctx, hw, device_handle);

  ctx->is_enabled = state;
  SYS_DEV_CHECK_DRIVER_CALL(ap33772s_set_output(hw, state), ctx);
  return STA_OK;
}

static status_rep_t negotiate_pdo(ap_adapter_ctx_t* ctx, uint32_t voltage_mv, uint32_t current_ma) {
  ap33772s_handle_t hw = get_hw_handle(ctx);
  CHECK_HANDLE_R(hw);

  // 1. Try PPS
  if (hw->index_pps_user != -1) {
    src_spr_and_epr_pdo_fields_t active_pdo = hw->src_pdo_array[hw->index_pps_user - 1];
    int voltage_min_decoded = (active_pdo.pps.voltage_min > 0) ? 3300 : 0;
    int voltage_max_decoded = active_pdo.pps.voltage_max * 100;
    
    if (voltage_mv >= voltage_min_decoded && voltage_mv <= voltage_max_decoded) {
      esp_err_t err = ap33772s_set_pps_pdo(hw, hw->index_pps_user, voltage_mv, current_ma);
      if (err == ESP_OK) return STA_OK;
    }
  }

  // 2. Try AVS
  if (hw->index_avs_user != -1) {
    src_spr_and_epr_pdo_fields_t active_pdo = hw->src_pdo_array[hw->index_avs_user - 1];
    int voltage_min_decoded = (active_pdo.avs.voltage_min > 0) ? 15000 : 0;
    int voltage_max_decoded = active_pdo.avs.voltage_max * 200;

    if (voltage_mv >= voltage_min_decoded && voltage_mv <= voltage_max_decoded) {
      esp_err_t err = ap33772s_set_avs_pdo(hw, hw->index_avs_user, voltage_mv, current_ma);
      if (err == ESP_OK) return STA_OK;
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

  if (best_pdo_index != -1) {
    esp_err_t err = ap33772s_set_fixed_pdo(hw, best_pdo_index, current_ma);
    if (err == ESP_OK) return STA_OK;
    return STA_FROM_ESP(err);
  }

  return STA_C(ERR_INVALID_ARG, OWNER, voltage_mv, STATUS_PAYLOAD_DEVICE);
}

static status_rep_t d_ap33772s_set_voltage(void* device_handle, uint32_t voltage_mV) {
  ap_adapter_ctx_t* ctx = (ap_adapter_ctx_t*)device_handle;
  CHECK_HANDLE_R(ctx);
  ctx->last_voltage_mv = voltage_mV;
  return negotiate_pdo(ctx, ctx->last_voltage_mv, ctx->last_current_ma);
}

static status_rep_t d_ap33772s_set_current(void* device_handle, uint32_t current_mA) {
  ap_adapter_ctx_t* ctx = (ap_adapter_ctx_t*)device_handle;
  CHECK_HANDLE_R(ctx);
  ctx->last_current_ma = current_mA;
  return negotiate_pdo(ctx, ctx->last_voltage_mv, ctx->last_current_ma);
}

static status_rep_t d_ap33772s_add_callback(void* device_handle, sys_power_events_e on_event, void (*callback)(uint8_t device_id, sys_power_events_e triggered_by)) {
  ap_adapter_ctx_t* ctx = (ap_adapter_ctx_t*)device_handle;
  CHECK_HANDLE_R(ctx);
  ctx->power_callback_event = callback;
  return STA_OK;
}

static const sys_power_vreg_contract s_ap_vreg_contract = {
  .set_enable = d_ap33772s_set_enable,
  .set_voltage = d_ap33772s_set_voltage,
  .set_current = d_ap33772s_set_current,
  .add_callback = d_ap33772s_add_callback
};

// --- 3. USB PD Contract Implementations ---

static status_rep_t d_ap33772s_set_settings(void* device_handle, uint32_t voltage_mV, uint32_t current_mA) {
  ap_adapter_ctx_t* ctx = (ap_adapter_ctx_t*)device_handle;
  CHECK_HANDLE_R(ctx);
  ctx->last_voltage_mv = voltage_mV;
  ctx->last_current_ma = current_mA;
  return negotiate_pdo(ctx, voltage_mV, current_mA);
}

static status_rep_t d_ap33772s_list_options(void* device_handle) {
  SYS_DEV_GET_ADAPTER_CONTEXT(ap_adapter_ctx_t, ap33772s_handle_t, ctx, hw, device_handle);
  ap33772s_log_profiles(hw);
  return STA_OK;
}

static status_rep_t d_ap33772s_get_limits(void* device_handle, uint32_t* out_mV, uint32_t* out_mA) {
  SYS_DEV_GET_ADAPTER_CONTEXT(ap_adapter_ctx_t, ap33772s_handle_t, ctx, hw, device_handle);
  CHECK_NOT_NULL_R(out_mV);
  CHECK_NOT_NULL_R(out_mA);

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
        if (pdo.fixed.current_max >= 15) pdo_ma = 5000;
        else if (pdo.fixed.current_max >= 14) pdo_ma = 4500;
        else pdo_ma = pdo.fixed.current_max * 250 + 1250;
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
  return STA_OK;
}

static const sys_power_usb_pd_contract s_ap_usb_pd_contract = {
  .set_settings = d_ap33772s_set_settings,
  .list_options = d_ap33772s_list_options,
  .get_limits = d_ap33772s_get_limits
};

// --- 4. Monitor Contract (for get voltage & current telemetry) ---

static status_rep_t d_ap33772s_get_telemetry_voltage(void* device_handle, uint8_t channel, int32_t* out_mV) {
  ap_adapter_ctx_t* ctx = (ap_adapter_ctx_t*)device_handle;
  CHECK_HANDLE_R(ctx);
  CHECK_NOT_NULL_R(out_mV);

  IF_SYS_DEV_FROZEN(ctx) {
    *out_mV = ctx->cached_voltage_mv;
    return STA_OK;
  }

  ap33772s_handle_t hw = get_hw_handle(ctx);
  CHECK_HANDLE_R(hw);

  int vol = ap33772s_read_voltage(hw);
  if (vol < 0) return STA_C(ERR_SYS_POWER_BASE, OWNER, 0, STATUS_PAYLOAD_DEVICE);
  *out_mV = vol;
  return STA_OK;
}

static status_rep_t d_ap33772s_get_telemetry_current(void* device_handle, uint8_t channel, int32_t* out_mA) {
  ap_adapter_ctx_t* ctx = (ap_adapter_ctx_t*)device_handle;
  CHECK_HANDLE_R(ctx);
  CHECK_NOT_NULL_R(out_mA);

  IF_SYS_DEV_FROZEN(ctx) {
    *out_mA = ctx->cached_current_ma;
    return STA_OK;
  }

  ap33772s_handle_t hw = get_hw_handle(ctx);
  CHECK_HANDLE_R(hw);

  int curr = ap33772s_read_current(hw);
  if (curr < 0) return STA_C(ERR_SYS_POWER_BASE, OWNER, 0, STATUS_PAYLOAD_DEVICE);
  *out_mA = curr;
  return STA_OK;
}

static const sys_power_monitor_contract s_ap_monitor_contract = {
  .get_voltage = d_ap33772s_get_telemetry_voltage,
  .get_current = d_ap33772s_get_telemetry_current,
  .add_callback = NULL
};

// --- 5. sys_device_t VTable Implementations ---

static status_rep_t adapter_uninstall_device(void* driver_handle) {
  ap_adapter_ctx_t* ctx = (ap_adapter_ctx_t*)driver_handle;
  CHECK_HANDLE_R(ctx);

  IF_PIN(ctx->intr_gpio_pin_num) {
    sys_io_reset(ctx->intr_gpio_device_id, ctx->intr_gpio_pin_num);
  }
  sys_power_unregister(ctx->base.device_id);
  sys_i2c_remove_driver(ctx->base.hw_handle);
  ap33772s_delete(get_hw_handle(ctx));
  free(ctx);
  return STA_OK;
}

static status_rep_t adapter_reset_device(void* driver_handle) {
  SYS_DEV_GET_ADAPTER_CONTEXT(ap_adapter_ctx_t, ap33772s_handle_t, ctx, hw, driver_handle);

  SYS_DEV_CHECK_DRIVER_CALL(ap33772s_set_output(hw, false), ctx);
  return STA_OK;
}

static status_rep_t adapter_error_handler(void* driver_handle, status_rep_t* error) {
  ESP_LOGE(TAG, "Device error: code=%d, owner=%d", error->e_code, error->e_owner);
  return adapter_reset_device(driver_handle);
}

static status_rep_t adapter_suspend_device(void* driver_handle) {
  SYS_DEV_GET_ADAPTER_CONTEXT(ap_adapter_ctx_t, ap33772s_handle_t, ctx, hw, driver_handle);
  SYS_DEV_CHECK_DRIVER_CALL(ap33772s_set_output(hw, false), ctx);
  return STA_OK;
}

static status_rep_t adapter_resume_device(void* driver_handle) {
  SYS_DEV_GET_ADAPTER_CONTEXT(ap_adapter_ctx_t, ap33772s_handle_t, ctx, hw, driver_handle);
  SYS_DEV_CHECK_DRIVER_CALL(ap33772s_set_output(hw, ctx->is_enabled), ctx);
  return STA_OK;
}

static status_rep_t adapter_freeze_device(void* driver_handle) {
  SYS_DEV_GET_ADAPTER_CONTEXT(ap_adapter_ctx_t, ap33772s_handle_t, ctx, hw, driver_handle);
  ctx->base.is_frozen = true;

  int vol = ap33772s_read_voltage(hw);
  int curr = ap33772s_read_current(hw);
  ctx->cached_voltage_mv = (vol >= 0) ? vol : 0;
  ctx->cached_current_ma = (curr >= 0) ? curr : 0;

  return STA_OK;
}

static status_rep_t adapter_sync_device(void* driver_handle) {
  ap_adapter_ctx_t* ctx = (ap_adapter_ctx_t*)driver_handle;
  CHECK_HANDLE_R(ctx);
  ctx->base.is_frozen = false;
  return STA_OK;
}

static void* fallback_install(ap_adapter_ctx_t* ctx) {
  if (ctx) {
    IF_PIN(ctx->intr_gpio_pin_num) {
      sys_io_reset(ctx->intr_gpio_device_id, ctx->intr_gpio_pin_num);
    }
    if (ctx->base.hw_handle) {
      sys_i2c_remove_driver(ctx->base.hw_handle);
      ap33772s_delete(get_hw_handle(ctx));
    }
    free(ctx);
  }
  return NULL;
}

static void* d_ap33772s_install(void** install_args) {
  uint8_t sys_dev_id = (uint8_t)(uintptr_t)install_args[0];
  bool i2c_bus_num = (bool)(uintptr_t)install_args[1];
  uint8_t i2c_address = (uint8_t)(uintptr_t)install_args[2];
  uint8_t int_gpio_device_id = (uint8_t)(uintptr_t)install_args[3];
  sys_io_pin_num_t int_pin_num = (sys_io_pin_num_t)(uintptr_t)install_args[4];
  sys_io_mode_e int_gpio_mode = (sys_io_mode_e)(uintptr_t)install_args[5];

  ap_adapter_ctx_t* ctx = sys_device_allocate_ctx(sizeof(ap_adapter_ctx_t), install_args);
  if (!ctx) return NULL;

  ctx->last_voltage_mv = 5000;
  ctx->last_current_ma = 500;
  ctx->is_enabled = false;
  ctx->intr_gpio_device_id = int_gpio_device_id;
  ctx->intr_gpio_pin_num = int_pin_num;

  ctx->base.hw_handle = ap33772s_new(i2c_bus_num);
  if (!ctx->base.hw_handle) {
    return fallback_install(ctx);
  }

  ap33772s_handle_t hw = get_hw_handle(ctx);

  hw->header.i2c_device_config.device_address = i2c_address;
  hw->header.transmit = sys_i2c_master_transmit;
  hw->header.transmit_receive = sys_i2c_master_transmit_receive;

  if (ap33772s_start(hw) != ESP_OK) {
    return fallback_install(ctx);
  }

  if (STA_IS_ERR(sys_i2c_add_driver(ctx->base.hw_handle))) {
    return fallback_install(ctx);
  }

  IF_PIN(int_pin_num) {
    if (STA_IS_ERR(sys_io_set_mode(int_gpio_device_id, int_pin_num, int_gpio_mode))) {
      return fallback_install(ctx);
    }
    sys_io_intr_config_t config = {.mode = SYS_IO_INTR_MODE_FALLING_EDGE, .callback = (sys_io_isr_callback_t)(void*)ap33772s_adapter_isr, .user_ctx = ctx};
    if (STA_IS_ERR(sys_io_configure_intr(int_gpio_device_id, int_pin_num, &config))) {
      return fallback_install(ctx);
    }
  }

  if (ap33772s_begin(hw) != ESP_OK) {
    return fallback_install(ctx);
  }

  if (STA_IS_ERR(sys_power_register_vreg(sys_dev_id, ctx, &s_ap_vreg_contract))) {
    return fallback_install(ctx);
  }

  if (STA_IS_ERR(sys_power_register_usb_pd(sys_dev_id, ctx, &s_ap_usb_pd_contract))) {
    sys_power_unregister(sys_dev_id);
    return fallback_install(ctx);
  }

  if (STA_IS_ERR(sys_power_register_monitor(sys_dev_id, ctx, &s_ap_monitor_contract))) {
    sys_power_unregister(sys_dev_id);
    return fallback_install(ctx);
  }

  return ctx;
}

// --- 6. Exposed Initialization API ---
status_rep_t d_ap33772s_create(uint8_t device_id, bool i2c_bus, uint8_t i2c_addr,
                               uint8_t intr_io_device, sys_io_pin_num_t intr_io_num,
                               sys_io_mode_e intr_io_mode) {
  void* args[] = {(void*)(uintptr_t)device_id, (void*)(uintptr_t)i2c_bus, (void*)(uintptr_t)i2c_addr,
                  (void*)(uintptr_t)intr_io_device, (void*)(uintptr_t)intr_io_num, (void*)(uintptr_t)intr_io_mode};

  sys_device_t dev = {.device_id = device_id,
      .role = SYS_DEV_ROLE_PWR,
      .name = "AP33772S",
      .install_args = args,
      .install_device = d_ap33772s_install,
      .uninstall_device = adapter_uninstall_device,
      .reset_device = adapter_reset_device,
      .error_handler = adapter_error_handler,
      .suspend_device = adapter_suspend_device,
      .resume_device = adapter_resume_device,
      .freeze_device = adapter_freeze_device,
      .sync_device = adapter_sync_device};

  return sys_device_install(&dev);
}