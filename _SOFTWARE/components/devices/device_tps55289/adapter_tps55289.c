#include <stdlib.h>
#include "device_tps55289.h"
#include "driver_tps55289.h"
#include "sys_device.h"
#include "sys_error.h"
#include "sys_i2c.h"
#include "sys_io.h"
#include "sys_power.h"

#undef OWNER
#define OWNER OWNER_DEVICE_TPS55289

typedef struct {
  sys_device_adapter_base_t base;

  d_tps55289_cfg_t cfg;

  /* Wanted settings. The hardware EN pin has priority: while the rail is off EN
     is low, and the TPS55289 forgets every register, so these are written
     again each time EN goes high (tps_apply_config). */
  uint16_t last_voltage_mV;
  uint16_t last_current_limit_mA;
  bool last_enable_state;
  bool is_current_limit_enabled;
  /* True while an output rise charges the capacitors (tps_soft_rise): the
     chip's OCP flag is expected then and not reported. */
  volatile bool rising;

  uint8_t intr_sub; /* sys_event subscription on intr_pin */
} tps_adapter_ctx_t;

#define TPS_DEFAULT_VOLTAGE_MV 5000
/* Raising the output charges its capacitors (VOUT_SR slew, set to the slowest 1.25 mV/us;
   3.6 ms soft start after OE). With a high current limit that inrush comes
   straight from the input and can pull a USB source down, so every rise runs
   at the soft-rise current limit (TPS_SOFT_RISE_MA) and the wanted limit is applied after this
   settle time. Note: the limit is VISP - VISN in 0.5 mV steps over the sense
   resistor, specified only at 30 / 50 mV (datasheet 6.5), so limits below ~1 A
   (a few mV) are coarse: on the PCB 250 / 350 mA settings held ~150 / ~240 mA. */
#define TPS_RAMP_SETTLE_MS 100
/* Current limit while the output rises. The limit is VISP - VISN over the
   10 mOhm sense resistor, so 200 mA is 2 mV - within the chip's offset: a rail
   then flags OCP just charging its capacitors (seen on rail A). 500 mA = 5 mV. */
#define TPS_SOFT_RISE_MA 500

/* Lowest limit written to the chip. Below ~5 mV sense the offset dominates: on
   the PCB rail A's chip sat in current limit with no load at a 200 mA (2 mV)
   setting and flagged OCP right after enable. Lower requests are raised to it. */
#define TPS_LIMIT_FLOOR_MA TPS_SOFT_RISE_MA

/* TEMP bring-up (short test): cap every current limit written to the chip,
   below the floor if needed. 0 = no cap. */
#define TPS_TEST_CURRENT_CAP_MA 350
static inline uint16_t tps_limit(uint16_t mA) {
  if (mA < TPS_LIMIT_FLOOR_MA) mA = TPS_LIMIT_FLOOR_MA;
  return (TPS_TEST_CURRENT_CAP_MA && mA > TPS_TEST_CURRENT_CAP_MA) ? TPS_TEST_CURRENT_CAP_MA : mA;
}

enum { TPS_STEP_I2C_ADDED = 0, TPS_STEP_EN_READY = 1, TPS_STEP_INTR_READY = 2, TPS_STEP_INTR_SUB = 3 };

/* True while the chip is powered and answers I2C: always without an EN pin,
   else only while the rail is enabled (EN high). */
static inline bool tps_powered(const tps_adapter_ctx_t* ctx) {
  return !sys_io_pin_is_valid(ctx->cfg.en_pin) || ctx->last_enable_state;
}

/* Drive EN; without an EN pin only the MODE register's output-enable bit switches the rail. */
static SE_MUST_USE err_h tps_set_en(tps_adapter_ctx_t* ctx, bool high) {
  if (sys_io_pin_is_valid(ctx->cfg.en_pin)) {
    SYS_DEV_TRY(sys_io_set_locked_level(ctx->cfg.en_pin, high), ctx);
    if (high) vTaskDelay(pdMS_TO_TICKS(10));  // I2C is up ~10 ms after EN rises
  }
  return NULL;
}

/* The output is rising (enable, or a higher voltage): hold the soft-rise current
   limit (TPS_SOFT_RISE_MA) while it settles, then apply the wanted one. */
static SE_MUST_USE err_h tps_soft_rise(tps_adapter_ctx_t* ctx, tps55289_handle_t hw) {
  vTaskDelay(pdMS_TO_TICKS(TPS_RAMP_SETTLE_MS));
  SYS_DEV_CHECK_DRIVER_CALL(tps55289_set_current_limit(hw, ctx->is_current_limit_enabled, tps_limit(ctx->last_current_limit_mA)), ctx);
  // Clear the OCP the rise may have latched; if the overload persists the
  // chip sets it again right away and the fault is reported normally.
  SYS_DEV_CHECK_DRIVER_CALL(tps55289_get_status(hw), ctx);
  ctx->rising = false;
  return NULL;
}

/* Write every wanted setting; called right after EN goes high (the chip lost them).
   An enabled output starts at the soft-rise limit (tps_soft_rise). */
static SE_MUST_USE err_h tps_apply_config(tps_adapter_ctx_t* ctx, tps55289_handle_t hw) {
  if (sys_io_pin_is_valid(ctx->cfg.intr_pin)) {
    // Every fault raises the alert; listeners decide what matters.
    SYS_DEV_CHECK_DRIVER_CALL(tps55289_set_fault_reporting(hw, true, true, true), ctx);
  }
  // Slowest output slew: voltage changes charge the output capacitors gently.
  SYS_DEV_CHECK_DRIVER_CALL(tps55289_set_slew_rate(hw, TPS55289_SLEW_1_25_MV_US), ctx);
  uint16_t start_mA = ctx->last_enable_state ? TPS_SOFT_RISE_MA : ctx->last_current_limit_mA;
  ctx->rising = ctx->last_enable_state;
  SYS_DEV_CHECK_DRIVER_CALL(tps55289_set_current_limit(hw, ctx->is_current_limit_enabled, tps_limit(start_mA)), ctx);
  SYS_DEV_CHECK_DRIVER_CALL(tps55289_set_voltage(hw, ctx->last_voltage_mV), ctx);
  SYS_DEV_CHECK_DRIVER_CALL(tps55289_set_output_enable(hw, ctx->last_enable_state), ctx);
  if (ctx->last_enable_state) SE_TRY(tps_soft_rise(ctx, hw));
  return NULL;
}

/* Inline listener of intr_pin: publish each fault in the status register. */
static SE_MUST_USE err_h device_event_handler(const sys_event_t* event, void* handle) {
  SYS_DEV_GET_ADAPTER_CONTEXT(tps_adapter_ctx_t, tps55289_handle_t, ctx, hw, handle);
  if (!tps_powered(ctx)) return NULL;  // EN low: nothing to read, no fault possible
  SYS_DEV_CHECK_DRIVER_CALL(tps55289_get_status(hw), ctx);

  err_h err = NULL;
  uint8_t hops = SYS_EVENT_CAUSED_BY(event);
  if (hw->last_status.ovp) {
    SYS_DEV_TEARDOWN_STEP(err, sys_power_publish(SYS_DEV_GET_ID(ctx), 0, SYS_PWR_EVENT_OVP, ctx->last_voltage_mV, hops));
  }
  if (hw->last_status.ocp && !ctx->rising) {
    SYS_DEV_TEARDOWN_STEP(err, sys_power_publish(SYS_DEV_GET_ID(ctx), 0, SYS_PWR_EVENT_OCP_CRITICAL, ctx->last_current_limit_mA, hops));
  }
  if (hw->last_status.scp) {
    SYS_DEV_TEARDOWN_STEP(err, sys_power_publish(SYS_DEV_GET_ID(ctx), 0, SYS_PWR_EVENT_SPC, 0, hops));
  }
  return err;
}

// --- VREG Contract Implementations ---
/* On: EN high, then every setting again (EN low cleared them). Off: output
   disabled, then EN low - the hardware switch has the last word. */
static SE_MUST_USE err_h contract_vreg_tps55289_set_enable(void* device_handle, bool state) {
  SYS_DEV_GET_ADAPTER_CONTEXT(tps_adapter_ctx_t, tps55289_handle_t, ctx, hw, device_handle);
  bool was_powered = tps_powered(ctx);
  ctx->last_enable_state = state;
  ctx->rising = false;

  if (state) {
    SE_TRY(tps_set_en(ctx, true));
    SE_TRY(tps_apply_config(ctx, hw));
  } else {
    err_h err = NULL;
    if (was_powered) SYS_DEV_TEARDOWN_DRIVER_STEP(err, tps55289_set_output_enable(hw, false), ctx);
    SYS_DEV_TEARDOWN_STEP(err, tps_set_en(ctx, false));
    return err;
  }
  return NULL;
}

static SE_MUST_USE err_h contract_vreg_tps55289_set_voltage(void* device_handle, uint32_t voltage_mV) {
  SYS_DEV_GET_ADAPTER_CONTEXT(tps_adapter_ctx_t, tps55289_handle_t, ctx, hw, device_handle);
  SE_CHECK_IN_RANGE(voltage_mV, DEVICE_TPS55289_MIN_VOLTAGE_MV, DEVICE_TPS55289_MAX_VOLTAGE_MV);
  bool rising = voltage_mV > ctx->last_voltage_mV;
  ctx->last_voltage_mV = voltage_mV;
  if (!tps_powered(ctx)) return NULL;  // applied by the next enable
  if (rising) {
    ctx->rising = true;
    SYS_DEV_CHECK_DRIVER_CALL(tps55289_set_current_limit(hw, ctx->is_current_limit_enabled, tps_limit(TPS_SOFT_RISE_MA)), ctx);
  }
  SYS_DEV_CHECK_DRIVER_CALL(tps55289_set_voltage(hw, voltage_mV), ctx);
  if (rising) SE_TRY(tps_soft_rise(ctx, hw));
  return NULL;
}

static SE_MUST_USE err_h contract_vreg_tps55289_set_current(void* device_handle, uint32_t current_mA) {
  SYS_DEV_GET_ADAPTER_CONTEXT(tps_adapter_ctx_t, tps55289_handle_t, ctx, hw, device_handle);
  SE_CHECK_IN_RANGE(current_mA, DEVICE_TPS55289_MIN_CURRENT_MA, DEVICE_TPS55289_MAX_CURRENT_MA);
  ctx->last_current_limit_mA = current_mA;
  if (!tps_powered(ctx)) return NULL;  // applied by the next enable
  SYS_DEV_CHECK_DRIVER_CALL(tps55289_set_current_limit(hw, ctx->is_current_limit_enabled, tps_limit(current_mA)), ctx);
  return NULL;
}

static const sys_power_vreg_contract_t s_tps55289_vreg_contract = {.set_enable = contract_vreg_tps55289_set_enable, .set_voltage = contract_vreg_tps55289_set_voltage, .set_current = contract_vreg_tps55289_set_current};

// --- sys_device VTable Implementations ---
static SE_MUST_USE err_h device_uninstall(void* handle) {
  SYS_DEV_GET_ADAPTER_CONTEXT(tps_adapter_ctx_t, tps55289_handle_t, ctx, hw, handle);
  err_h err = NULL;

  IF_SYS_DEV_STEP_DONE(ctx, TPS_STEP_EN_READY) {
    SYS_DEV_TEARDOWN_STEP(err, sys_io_unlock_pin(ctx->cfg.en_pin));
    SYS_DEV_TEARDOWN_STEP(err, sys_io_set_level(ctx->cfg.en_pin, false));
    SYS_DEV_TEARDOWN_STEP(err, sys_io_reset(ctx->cfg.en_pin));
  }
  IF_SYS_DEV_STEP_DONE(ctx, TPS_STEP_INTR_SUB) {
    SYS_DEV_TEARDOWN_STEP(err, sys_event_unsubscribe(ctx->intr_sub, false));
  }
  IF_SYS_DEV_STEP_DONE(ctx, TPS_STEP_INTR_READY) {
    SYS_DEV_TEARDOWN_STEP(err, sys_io_unlock_pin(ctx->cfg.intr_pin));
    SYS_DEV_TEARDOWN_STEP(err, sys_io_reset(ctx->cfg.intr_pin));
  }
  if (ctx->base.hw_handle) {
    IF_SYS_DEV_STEP_DONE(ctx, TPS_STEP_I2C_ADDED) {
      SYS_DEV_TEARDOWN_STEP(err, sys_i2c_remove_driver(ctx->base.hw_handle));
    }
    tps55289_delete(hw);
  }

  free(ctx);
  return err;
}

/* Back to the install defaults: rail off (EN low), 5 V / minimum current wanted. */
static SE_MUST_USE err_h device_reset(void* handle) {
  SYS_DEV_GET_ADAPTER_CONTEXT(tps_adapter_ctx_t, tps55289_handle_t, ctx, hw, handle);
  ctx->last_voltage_mV = TPS_DEFAULT_VOLTAGE_MV;
  ctx->last_current_limit_mA = DEVICE_TPS55289_MIN_CURRENT_MA;
  ctx->is_current_limit_enabled = true;
  return contract_vreg_tps55289_set_enable(handle, false);
}

// Fault safe state: EN is driven low even if the I2C disable fails. The wanted
// state (last_enable_state) is kept for resume.
static SE_MUST_USE err_h device_suspend(void* handle) {
  SYS_DEV_GET_ADAPTER_CONTEXT(tps_adapter_ctx_t, tps55289_handle_t, ctx, hw, handle);
  err_h err = NULL;
  ctx->rising = false;

  if (tps_powered(ctx)) SYS_DEV_TEARDOWN_DRIVER_STEP(err, tps55289_set_output_enable(hw, false), ctx);
  SYS_DEV_TEARDOWN_STEP(err, tps_set_en(ctx, false));
  return err;
}

// Back to the wanted state: an enabled rail gets EN high and every setting again.
static SE_MUST_USE err_h device_resume(void* handle) {
  SYS_DEV_GET_ADAPTER_CONTEXT(tps_adapter_ctx_t, tps55289_handle_t, ctx, hw, handle);
  if (!tps_powered(ctx)) return NULL;
  SE_TRY(tps_set_en(ctx, true));
  SE_TRY(tps_apply_config(ctx, hw));
  return NULL;
}

static SE_MUST_USE err_h device_install(const void* cfg_blob, void** out_device_handle) {
  const d_tps55289_cfg_t* cfg = (const d_tps55289_cfg_t*)cfg_blob;
  SE_CHECK_IN_RANGE(cfg->i2c_addr, TPS55289_I2C_ADDR_74, TPS55289_I2C_ADDR_75);

  SYS_DEV_CTX_NEW(tps_adapter_ctx_t, ctx, cfg);
  err_h err = NULL;

  ctx->base.hw_handle = tps55289_new(ctx->cfg.i2c_addr, ctx->cfg.i2c_bus);
  if (!ctx->base.hw_handle) {
    free(ctx);
    SE_FAIL(ERR_BASE_NO_MEM, 0);
  }

  ctx->last_voltage_mV = TPS_DEFAULT_VOLTAGE_MV;
  ctx->last_current_limit_mA = DEVICE_TPS55289_MIN_CURRENT_MA;
  ctx->last_enable_state = false;
  ctx->is_current_limit_enabled = true;

  tps55289_handle_t hw = (tps55289_handle_t)(ctx->base.hw_handle);

  if (sys_io_pin_is_valid(ctx->cfg.en_pin)) {
    SYS_DEV_INSTALL_STEP(sys_io_set_mode(ctx->cfg.en_pin), "en pin mode");
    SYS_DEV_INSTALL_STEP(sys_io_set_level(ctx->cfg.en_pin, true), "en pin high");
    SYS_DEV_INSTALL_STEP(sys_io_lock_pin(ctx->cfg.en_pin), "en pin lock");
    SYS_DEV_STEP_DONE(ctx, TPS_STEP_EN_READY);
    vTaskDelay(pdMS_TO_TICKS(10));
  }

  SYS_DEV_INSTALL_STEP(sys_i2c_add_driver(hw), "i2c add driver");
  SYS_DEV_STEP_DONE(ctx, TPS_STEP_I2C_ADDED);

  // EN must be driven high before the I2C probe below - the TPS55289's I2C
  // interface is unavailable while EN is low/floating, so probing first
  // would always fail with ERR_I2C_DEV_NOT_FOUND on a fresh boot. The delay
  // matches the settling time contract_vreg_tps55289_set_enable() already
  // waits after driving this same pin high at runtime.

  // Configure interrupt pin & callback
  if (sys_io_pin_is_valid(ctx->cfg.intr_pin)) {
    SYS_DEV_INSTALL_STEP(sys_io_set_mode(ctx->cfg.intr_pin), "intr pin mode");
    SYS_DEV_INSTALL_STEP(sys_io_subscribe_pin(ctx->cfg.intr_pin, device_event_handler, ctx, &ctx->intr_sub), "intr pin subscribe");
    SYS_DEV_STEP_DONE(ctx, TPS_STEP_INTR_SUB);
    sys_io_intr_config_t intr_cfg = {.mode = SYS_IO_INTR_MODE_FALLING_EDGE};
    SYS_DEV_INSTALL_STEP(sys_io_configure_intr(ctx->cfg.intr_pin, &intr_cfg), "intr pin configure");
    SYS_DEV_INSTALL_STEP(sys_io_lock_pin(ctx->cfg.intr_pin), "intr pin lock");
    SYS_DEV_STEP_DONE(ctx, TPS_STEP_INTR_READY);
  }

  // Probe with EN high (above), write the defaults with the output off, then
  // hand over to the hardware switch: the rail stays off (EN low) until enabled.
  SYS_DEV_INSTALL_STEP(tps_apply_config(ctx, hw), "tps apply defaults");
  SYS_DEV_INSTALL_STEP(tps_set_en(ctx, false), "en pin low");

  *out_device_handle = ctx;
  return NULL;

fail:
  SYS_DEV_INSTALL_FAIL(err, cfg->device_id, out_device_handle, device_uninstall, ctx);
  return NULL;
}

static const sys_device_class_t s_tps55289_class = {
    .name = "TPS55289_VREG",
    .contracts = {[SYS_DEVICE_CONTRACT_POWER_VREG] = &s_tps55289_vreg_contract},
    .ops = {.install = device_install, .uninstall = device_uninstall, .reset = device_reset, .suspend = device_suspend, .resume = device_resume},
};

err_h d_tps55289_create(const d_tps55289_cfg_t* cfg) {
  SE_CHECK_NOT_NULL(cfg);
  return SYS_DEVICE_CREATE(&s_tps55289_class, cfg);
}
