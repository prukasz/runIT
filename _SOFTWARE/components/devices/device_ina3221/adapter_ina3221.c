// INA3221 device adapter implementation
#include "device_ina3221.h"
#include "driver_ina3221.h"
#include "sys_device.h"
#include "sys_i2c.h"
#include "sys_io.h"
#include "sys_power.h"

#undef OWNER
#define OWNER OWNER_DEVICE_INA3221

typedef struct {
  sys_device_adapter_base_t base;

  d_ina3221_cfg_t cfg;

  uint8_t crit_sub; /* sys_event subscriptions on the alert pins */
  uint8_t warn_sub;

  /* Reversed-shunt channel armed on the critical [0] / warning [1] pin, -1 = none (set_inverted_alert). */
  int8_t inverted_alert[2];
} ina_adapter_ctx_t;

enum { INA_STEP_I2C_ADDED = 0, INA_STEP_CRIT_READY = 1, INA_STEP_WARN_READY = 2, INA_STEP_CRIT_SUB = 3, INA_STEP_WARN_SUB = 4 };

static SE_MUST_USE err_h device_event_handler(const sys_event_t* event, void* handle);

static SE_MUST_USE err_h contract_monitor_ina3221_get_voltage(void* device_handle, uint8_t channel, int32_t* out_mV) {
  SYS_DEV_GET_ADAPTER_CONTEXT(ina_adapter_ctx_t, ina3221_handle_t, ctx, hw, device_handle);
  SE_CHECK_HANDLE(out_mV);
  SE_CHECK_IN_RANGE(channel, 0, 2);

  SYS_DEV_CHECK_DRIVER_CALL(ina3221_read_bus_voltage(hw, channel, out_mV), ctx);
  return NULL;
}

#define INA_FEATURE_SET_ALERT 2 /* index in sys_power_monitor_feature_names */

/* Current as the board sees it: flipped for a reversed shunt (cfg.inverted_mask). */
static esp_err_t ina_read_current(const ina_adapter_ctx_t* ctx, ina3221_handle_t hw, uint8_t channel, int32_t* out_mA) {
  esp_err_t rc = ina3221_read_shunt_current(hw, channel, out_mA);
  if (rc == ESP_OK && (ctx->cfg.inverted_mask & (1u << channel))) *out_mA = -*out_mA;
  return rc;
}

static SE_MUST_USE err_h contract_monitor_ina3221_get_current(void* device_handle, uint8_t channel, int32_t* out_mA) {
  SYS_DEV_GET_ADAPTER_CONTEXT(ina_adapter_ctx_t, ina3221_handle_t, ctx, hw, device_handle);
  SE_CHECK_HANDLE(out_mA);
  SE_CHECK_IN_RANGE(channel, 0, 2);

  SYS_DEV_CHECK_DRIVER_CALL(ina_read_current(ctx, hw, channel, out_mA), ctx);
  return NULL;
}

/* An alert pin's interrupt edge; the pin stays locked to this device. */
static SE_MUST_USE err_h set_pin_edge(sys_io_pin_ref_t pin, sys_io_intr_mode_e mode) {
  SE_TRY(sys_io_unlock_pin(pin));
  sys_io_intr_config_t intr_cfg = {.mode = mode};
  err_h err = sys_io_configure_intr(pin, &intr_cfg);
  SYS_DEV_TEARDOWN_STEP(err, sys_io_lock_pin(pin));
  return err;
}

/* A reversed shunt reads negative and the chip compares signed, so the limit
   is -threshold: the alert is active while the current is below the threshold
   and releases above it (checked on the PCB). It is transparent (not latched),
   and its pin takes this one channel only - an active channel holds the shared
   pin and would hide the others. The pin's release (rising edge) is the alert. */
static SE_MUST_USE err_h set_inverted_alert(ina_adapter_ctx_t* ctx, ina3221_handle_t hw, uint8_t channel, bool critical, int32_t threshold_mA) {
  SE_CHECK_IN_RANGE(threshold_mA, 1, INT32_MAX);
  sys_io_pin_ref_t pin = critical ? ctx->cfg.crit_pin : ctx->cfg.warn_pin;
  if (!sys_io_pin_is_valid(pin)) SE_FAIL(ERR_DEV_FEATURE_UNAVAILABLE, SYS_DEV_GET_ID(ctx), SYS_DEVICE_CONTRACT_POWER_MONITOR, INA_FEATURE_SET_ALERT);
  int8_t* armed = &ctx->inverted_alert[critical ? 0 : 1];
  if (*armed >= 0 && *armed != channel) SE_FAIL(ERR_IO_PIN_ALREADY_IN_USE, pin.device_id, pin.pin, pin.mode);
  // Rising edge first: arming pulls the pin low (its normal state now), which must not count.
  if (*armed < 0) SE_TRY(set_pin_edge(pin, SYS_IO_INTR_MODE_RISING_EDGE));
  SYS_DEV_CHECK_DRIVER_CALL(ina3221_enable_latch_pin(hw, critical ? hw->mask.wen : false, critical ? false : hw->mask.cen), ctx);
  SYS_DEV_CHECK_DRIVER_CALL(ina3221_set_alert(hw, channel, -threshold_mA, critical), ctx);
  *armed = (int8_t)channel;
  return NULL;
}

/* After a chip reset (limits at their defaults): the armed reversed-shunt alerts are gone, their pins back to the falling edge. */
static SE_MUST_USE err_h disarm_inverted_alerts(ina_adapter_ctx_t* ctx) {
  err_h err = NULL;
  const sys_io_pin_ref_t pins[2] = {ctx->cfg.crit_pin, ctx->cfg.warn_pin};
  for (int i = 0; i < 2; i++) {
    if (ctx->inverted_alert[i] < 0) continue;
    ctx->inverted_alert[i] = -1;
    SYS_DEV_TEARDOWN_STEP(err, set_pin_edge(pins[i], SYS_IO_INTR_MODE_FALLING_EDGE));
  }
  return err;
}

static SE_MUST_USE err_h contract_monitor_ina3221_set_alert(void* device_handle, uint8_t channel, sys_power_events_e alert, int32_t threshold_mA) {
  SYS_DEV_GET_ADAPTER_CONTEXT(ina_adapter_ctx_t, ina3221_handle_t, ctx, hw, device_handle);
  SE_CHECK_IN_RANGE(channel, 0, 2);
  bool critical = alert == SYS_PWR_EVENT_OCP_CRITICAL;
  if (!critical && alert != SYS_PWR_EVENT_OCP_WARNING) SE_FAIL(ERR_DEV_FEATURE_UNAVAILABLE, SYS_DEV_GET_ID(ctx), 0, alert);
  if (ctx->cfg.inverted_mask & (1u << channel)) return set_inverted_alert(ctx, hw, channel, critical, threshold_mA);

  SYS_DEV_CHECK_DRIVER_CALL(ina3221_set_alert(hw, channel, threshold_mA, critical), ctx);
  return NULL;
}

static const sys_power_monitor_contract_t s_ina3221_monitor_contract = {.get_voltage = contract_monitor_ina3221_get_voltage, .get_current = contract_monitor_ina3221_get_current, .set_alert = contract_monitor_ina3221_set_alert};

// --- sys_device_t VTable Implementations ---
static SE_MUST_USE err_h device_uninstall(void* handle) {
  SYS_DEV_GET_ADAPTER_CONTEXT(ina_adapter_ctx_t, ina3221_handle_t, ctx, hw, handle);
  err_h err = NULL;

  IF_SYS_DEV_STEP_DONE(ctx, INA_STEP_CRIT_SUB) {
    SYS_DEV_TEARDOWN_STEP(err, sys_event_unsubscribe(ctx->crit_sub, false));
  }
  IF_SYS_DEV_STEP_DONE(ctx, INA_STEP_WARN_SUB) {
    SYS_DEV_TEARDOWN_STEP(err, sys_event_unsubscribe(ctx->warn_sub, false));
  }
  IF_SYS_DEV_STEP_DONE(ctx, INA_STEP_CRIT_READY) {
    SYS_DEV_TEARDOWN_STEP(err, sys_io_unlock_pin(ctx->cfg.crit_pin));
    SYS_DEV_TEARDOWN_STEP(err, sys_io_reset(ctx->cfg.crit_pin));
  }
  IF_SYS_DEV_STEP_DONE(ctx, INA_STEP_WARN_READY) {
    SYS_DEV_TEARDOWN_STEP(err, sys_io_unlock_pin(ctx->cfg.warn_pin));
    SYS_DEV_TEARDOWN_STEP(err, sys_io_reset(ctx->cfg.warn_pin));
  }
  if (ctx->base.hw_handle) {
    IF_SYS_DEV_STEP_DONE(ctx, INA_STEP_I2C_ADDED) {
      SYS_DEV_TEARDOWN_STEP(err, sys_i2c_remove_driver(ctx->base.hw_handle));
    }
    ina3221_delete(hw);
  }
  free(ctx);
  return err;
}

static SE_MUST_USE err_h device_reset(void* handle) {
  SYS_DEV_GET_ADAPTER_CONTEXT(ina_adapter_ctx_t, ina3221_handle_t, ctx, hw, handle);

  SYS_DEV_CHECK_DRIVER_CALL(ina3221_reset(hw), ctx);
  SE_TRY(disarm_inverted_alerts(ctx));
  // Enable latches & options (Warning & Critical alert latch)
  SYS_DEV_CHECK_DRIVER_CALL(ina3221_enable_latch_pin(hw, true, true), ctx);
  SYS_DEV_CHECK_DRIVER_CALL(ina3221_set_options(hw, true, true, true), ctx);
  return NULL;
}

static SE_MUST_USE err_h device_suspend(void* handle) {
  SYS_DEV_GET_ADAPTER_CONTEXT(ina_adapter_ctx_t, ina3221_handle_t, ctx, hw, handle);
  // Put INA3221 into power-down mode (mode = 0 in config)
  SYS_DEV_CHECK_DRIVER_CALL(ina3221_set_options(hw, false, false, false), ctx);
  return NULL;
}

static SE_MUST_USE err_h device_resume(void* handle) {
  SYS_DEV_GET_ADAPTER_CONTEXT(ina_adapter_ctx_t, ina3221_handle_t, ctx, hw, handle);
  // Put INA3221 back into continuous mode (mode = 1)
  SYS_DEV_CHECK_DRIVER_CALL(ina3221_set_options(hw, true, true, true), ctx);
  return NULL;
}

static SE_MUST_USE err_h device_install(const void* cfg_blob, void** out_device_handle) {
  const d_ina3221_cfg_t* cfg = (const d_ina3221_cfg_t*)cfg_blob;
  SE_CHECK_IN_RANGE(cfg->i2c_addr, INA3221_I2C_ADDR_GND, INA3221_I2C_ADDR_SCL);

  SYS_DEV_CTX_NEW(ina_adapter_ctx_t, ctx, cfg);
  ctx->inverted_alert[0] = ctx->inverted_alert[1] = -1;
  err_h err = NULL;

  ctx->base.hw_handle = ina3221_new(ctx->cfg.i2c_addr, ctx->cfg.i2c_bus);
  if (!ctx->base.hw_handle) {
    free(ctx);
    SE_FAIL(ERR_BASE_NO_MEM, 0);
  }

  ina3221_handle_t hw = (ina3221_handle_t)(ctx->base.hw_handle);

  SYS_DEV_INSTALL_STEP(sys_i2c_add_driver(ctx->base.hw_handle), "i2c add driver");
  SYS_DEV_STEP_DONE(ctx, INA_STEP_I2C_ADDED);

  SYS_DEV_INSTALL_STEP(sys_i2c_device_present(ctx->base.hw_handle), "probe i2c device");
  SYS_DEV_INSTALL_STEP(SE_CONVERT_ESP(ina3221_start(hw)), "start ina3221");

  // Configure critical alert interrupt pin
  if (sys_io_pin_is_valid(ctx->cfg.crit_pin)) {
    SYS_DEV_INSTALL_STEP(sys_io_set_mode(ctx->cfg.crit_pin), "crit pin mode");
    SYS_DEV_INSTALL_STEP(sys_io_subscribe_pin(ctx->cfg.crit_pin, device_event_handler, ctx, &ctx->crit_sub), "crit pin subscribe");
    SYS_DEV_STEP_DONE(ctx, INA_STEP_CRIT_SUB);
    sys_io_intr_config_t intr_cfg = {.mode = SYS_IO_INTR_MODE_FALLING_EDGE};
    SYS_DEV_INSTALL_STEP(sys_io_configure_intr(ctx->cfg.crit_pin, &intr_cfg), "crit pin intr");
    SYS_DEV_INSTALL_STEP(sys_io_lock_pin(ctx->cfg.crit_pin), "crit pin lock");
    SYS_DEV_STEP_DONE(ctx, INA_STEP_CRIT_READY);
  }

  // Configure warning alert interrupt pin
  if (sys_io_pin_is_valid(ctx->cfg.warn_pin)) {
    SYS_DEV_INSTALL_STEP(sys_io_set_mode(ctx->cfg.warn_pin), "warn pin mode");
    SYS_DEV_INSTALL_STEP(sys_io_subscribe_pin(ctx->cfg.warn_pin, device_event_handler, ctx, &ctx->warn_sub), "warn pin subscribe");
    SYS_DEV_STEP_DONE(ctx, INA_STEP_WARN_SUB);
    sys_io_intr_config_t intr_cfg = {.mode = SYS_IO_INTR_MODE_FALLING_EDGE};
    SYS_DEV_INSTALL_STEP(sys_io_configure_intr(ctx->cfg.warn_pin, &intr_cfg), "warn pin intr");
    SYS_DEV_INSTALL_STEP(sys_io_lock_pin(ctx->cfg.warn_pin), "warn pin lock");
    SYS_DEV_STEP_DONE(ctx, INA_STEP_WARN_READY);
  }

  // Initialize INA3221 defaults
  SYS_DEV_INSTALL_STEP(SE_CONVERT_ESP(ina3221_reset(hw)), "ina3221 reset");
  SYS_DEV_INSTALL_STEP(SE_CONVERT_ESP(ina3221_enable_latch_pin(hw, true, true)), "ina3221 enable latch");
  SYS_DEV_INSTALL_STEP(SE_CONVERT_ESP(ina3221_set_options(hw, true, true, true)), "ina3221 set options");

  *out_device_handle = ctx;
  return NULL;

fail:
  SYS_DEV_INSTALL_FAIL(err, cfg->device_id, out_device_handle, device_uninstall, ctx);
  return NULL;
}

/* Inline listener of both alert pins: publish every flagged channel. */
static SE_MUST_USE err_h device_event_handler(const sys_event_t* event, void* handle) {
  SYS_DEV_GET_ADAPTER_CONTEXT(ina_adapter_ctx_t, ina3221_handle_t, ctx, hw, handle);
  // Read and clear alert flags from the mask/status register
  SYS_DEV_CHECK_DRIVER_CALL(ina3221_get_status(hw), ctx);

  // A reversed-shunt alert's pin released: that channel went over its threshold.
  bool on_crit = event->device_id == ctx->cfg.crit_pin.device_id && event->channel == ctx->cfg.crit_pin.pin;
  bool on_warn = event->device_id == ctx->cfg.warn_pin.device_id && event->channel == ctx->cfg.warn_pin.pin;
  int8_t inverted = on_crit ? ctx->inverted_alert[0] : on_warn ? ctx->inverted_alert[1] : -1;
  if (inverted >= 0) {
    int32_t ma_val = 0;
    err_h alert_err = NULL;
    SYS_DEV_TEARDOWN_DRIVER_STEP(alert_err, ina_read_current(ctx, hw, (uint8_t)inverted, &ma_val), ctx);
    SYS_DEV_TEARDOWN_STEP(alert_err, sys_power_publish(SYS_DEV_GET_ID(ctx), (uint8_t)inverted, on_crit ? SYS_PWR_EVENT_OCP_CRITICAL : SYS_PWR_EVENT_OCP_WARNING, ma_val, SYS_EVENT_CAUSED_BY(event)));
    return alert_err;
  }

  // Every flagged channel is published, even if a current read fails (the
  // value is then 0); the first failure is returned.
  err_h err = NULL;
  // Check critical alert flags
  // A reversed-shunt channel's flag is set while it is under its threshold (its normal state): skipped.
  uint8_t cf = hw->mask.cf;
  for (uint8_t ch = 0; ch < 3; ch++) {
    if (ctx->cfg.inverted_mask & (1u << ch)) continue;
    if (((cf >> (2 - ch)) & 1)) {
      int32_t ma_val = 0;
      SYS_DEV_TEARDOWN_DRIVER_STEP(err, ina_read_current(ctx, hw, ch, &ma_val), ctx);
      SYS_DEV_TEARDOWN_STEP(err, sys_power_publish(SYS_DEV_GET_ID(ctx), ch, SYS_PWR_EVENT_OCP_CRITICAL, ma_val, SYS_EVENT_CAUSED_BY(event)));
    }
  }
  // Check warning alert flags
  uint8_t wf = hw->mask.wf;
  for (uint8_t ch = 0; ch < 3; ch++) {
    if (ctx->cfg.inverted_mask & (1u << ch)) continue;
    if (((wf >> (2 - ch)) & 1)) {
      int32_t ma_val = 0;
      SYS_DEV_TEARDOWN_DRIVER_STEP(err, ina_read_current(ctx, hw, ch, &ma_val), ctx);
      SYS_DEV_TEARDOWN_STEP(err, sys_power_publish(SYS_DEV_GET_ID(ctx), ch, SYS_PWR_EVENT_OCP_WARNING, ma_val, SYS_EVENT_CAUSED_BY(event)));
    }
  }
  return err;
}

static const sys_device_class_t s_ina3221_class = {
    .name = "INA3221_PWR_MONITOR",
    .contracts = {[SYS_DEVICE_CONTRACT_POWER_MONITOR] = &s_ina3221_monitor_contract},
    .ops = {.install = device_install, .uninstall = device_uninstall, .reset = device_reset, .suspend = device_suspend, .resume = device_resume},
};

// --- Exposed Initialization API ---
err_h d_ina3221_create(const d_ina3221_cfg_t* cfg) {
  SE_CHECK_NOT_NULL(cfg);
  return SYS_DEVICE_CREATE(&s_ina3221_class, cfg);
}
// 291
