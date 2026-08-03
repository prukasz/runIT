#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "device_ads7128.h"
#include "driver_ads7128.h"
#include "esp_log.h"
#include "sys_device.h"
#include "sys_error.h"
#include "sys_i2c.h"
#include "sys_io.h"

static const char* TAG = __FILE_NAME__;

#undef OWNER
#define OWNER OWNER_DEVICE_ADS7128

#define PINS_COUNT ADS7128_CH_COUNT
#define PINS_MASK ADS7128_CH_MASK_ALL

/* Hysteresis is a 4-bit field applied as [hysteresis, 000b], i.e. in steps of 8 codes */
#define ADS_HYSTERESIS_STEP 8

// Install steps, recorded so teardown rolls back only what was actually built
enum { ADS_STEP_I2C_ADDED = 0, ADS_STEP_INTR_READY = 1 };

typedef struct ads_adapter_ctx_t {
  sys_device_adapter_base_t base;  // must be first
  d_ads7128_cfg_t cfg;             // value copy; the only cfg the adapter reads

  float mv_per_code;

  uint16_t cached_codes[PINS_COUNT];  // snapshot served while the device is frozen
  uint16_t route_masks[PINS_COUNT];
  uint64_t action_masks[PINS_COUNT];
  sys_io_intr_mode_e intr_modes[PINS_COUNT];
  own_funct_t own_funcs[PINS_COUNT];
  // The user-facing config from sys_io_configure_intr(), remembered so
  // device_event_handler can restore it once a crossing has been reported and
  // recovery has been detected (see entry/exit watch swap below).
  ads7128_alert_cfg_t entry_alert_cfg[PINS_COUNT];
  // Bit N set = channel N is currently past its threshold and has already
  // been reported; the chip is presently armed with an "exit watch" (see
  // ads_arm_exit_watch()) instead of its normal entry config. The chip's
  // EVENT_HIGH_FLAG/EVENT_LOW_FLAG has no notion of "newly" violating vs
  // "still" violating - a signal parked past the threshold re-latches it
  // every autonomous sample (~every 8ms here). Rather than poll for recovery,
  // device_event_handler flips the armed config between the entry threshold
  // (waiting to cross) and a hysteresis-retreated exit threshold (waiting to
  // recover) so the chip itself only ever raises ALERT on a genuine
  // transition in either direction - no timer, no per-cycle spam.
  uint8_t alert_active_mask;
} ads_adapter_ctx_t;

// --- Helper Functions ---

static inline uint32_t code_to_mv(const ads_adapter_ctx_t* ctx, uint16_t code) {
  return (uint32_t)((float)code * ctx->mv_per_code + 0.5f);
}

static inline uint16_t mv_to_code(const ads_adapter_ctx_t* ctx, uint32_t mv) {
  uint32_t code = (uint32_t)((float)mv / ctx->mv_per_code + 0.5f);
  return (code > ADS7128_MAX_CODE) ? ADS7128_MAX_CODE : (uint16_t)code;
}

static inline uint8_t hysteresis_field(const ads_adapter_ctx_t* ctx, uint32_t hysteresis_mv) {
  uint16_t steps = (uint16_t)(mv_to_code(ctx, hysteresis_mv) / ADS_HYSTERESIS_STEP);
  return (steps > 0x0F) ? 0x0F : (uint8_t)steps;
}

/* The register field counts n+1 consecutive violations, so a threshold of 0 or 1
   means "alert on the first sample". */
static inline uint8_t event_count_field(uint16_t event_counter_threshold) {
  if (event_counter_threshold <= 1) return 0;
  uint16_t field = (uint16_t)(event_counter_threshold - 1);
  return (field > 0x0F) ? 0x0F : (uint8_t)field;
}

// --- Alert Dispatcher ---

/* Runs in the callback task, hooked to the ALERT pin of the chip. The chip only
   says "something crossed a threshold", so the flags decide which channels fired
   and the flags are cleared afterwards to release ALERT for the next event.

   ALERT is the live OR of the EVENT_FLAG bits (datasheet 8.3.11), and each
   flag is latched - set on a violation, and NOT self-clearing when the
   signal returns in range; only an explicit write-1 clears it. GPIO42's ESP32
   interrupt is edge-triggered (falling edge only), so if the sequencer
   re-latches a flag between our read and our clear (a real risk: it keeps
   converting autonomously the whole time this handler is running), a flag
   can be left set with no further edge ever able to fire and revisit it -
   ALERT just stays asserted forever, desynced from reality. Looping here
   until a poll comes back with nothing pending closes that gap.

   The flag is also silent about *repetition*: a signal parked past the
   threshold re-latches it on every autonomous sample (~every 8ms here with
   8 channels enabled), not just the first time it crossed. The chip has no
   concept of "newly" vs "still" violating, and no signal at all for "back in
   range" - so getting one notification per real crossing needs something
   watching for recovery too, and that has to be the chip itself (a CPU-side
   poll/timer would mean waking up constantly just to check). ads_arm_exit_watch()
   is that trick: once a crossing fires, instead of re-arming the same
   threshold (which just re-trips every cycle for as long as the signal stays
   past it), the channel gets reconfigured to watch for the *opposite*
   condition - the signal retreating back past (threshold -/+ hysteresis).
   That watch stays genuinely quiet the whole time the signal remains past
   the original threshold, since its own condition simply isn't true yet.
   Only a real recovery re-arms the original entry watch. Net effect: exactly
   one ALERT edge per genuine transition in either direction, entirely
   chip-driven, no polling. */
/* Deliberately reuses OUT_OF_BAND for the exit watch too, rather than
 * IN_BAND: OUT_OF_BAND + EVENT_HIGH_FLAG/EVENT_LOW_FLAG is the mechanism
 * this whole adapter has already proven works (every "above threshold"
 * dispatch in this file goes through it); IN_BAND's exact flag-setting
 * behavior isn't nailed down by the datasheet text available here, so it's
 * not worth trusting for the leg of this that has no independent way to be
 * noticed if it's silently wrong. A single-sided OUT_OF_BAND config (one
 * threshold disabled) is exactly equivalent to "watch this one side only". */
static void ads_arm_exit_watch(ads7128_alert_cfg_t* out, const ads7128_alert_cfg_t* entry, bool high_fired, bool low_fired) {
  *out = (ads7128_alert_cfg_t){
      .enabled = true,
      .high_th = ADS7128_MAX_CODE,  // disabled unless low_fired narrows it below
      .low_th = 0,                  // disabled unless high_fired narrows it above
      .hysteresis = 0,
      .event_count = entry->event_count,
      .region = ADS7128_ALERT_OUT_OF_BAND,
  };
  uint16_t hyst_codes = (uint16_t)entry->hysteresis * ADS_HYSTERESIS_STEP;
  if (high_fired) {
    // Was above entry->high_th; recovered once it drops back below (high_th - hysteresis).
    out->low_th = (entry->high_th > hyst_codes) ? (uint16_t)(entry->high_th - hyst_codes) : 0;
  }
  if (low_fired) {
    // Was below entry->low_th; recovered once it rises back above (low_th + hysteresis).
    uint32_t retreat = (uint32_t)entry->low_th + hyst_codes;
    out->high_th = (retreat > ADS7128_MAX_CODE) ? ADS7128_MAX_CODE : (uint16_t)retreat;
  }
}

static err_h device_event_handler(void* handle, cb_event_t* event) {
  SYS_DEV_GET_ADAPTER_CONTEXT(ads_adapter_ctx_t, ads_handle_t, ctx, hw, handle);
  (void)event;

  for (int guard = 0; guard < 16; guard++) {
    ads7128_event_flags_t flags;
    SYS_DEV_CHECK_DRIVER_CALL(ads_get_event_flags(hw, &flags), ctx);

    uint8_t pending = (uint8_t)(flags.high | flags.low);
    if (!pending) break;

    for (uint8_t pin = 0; pin < PINS_COUNT; pin++) {
      if (!(pending & (1u << pin))) continue;

      sys_io_intr_mode_e mode = ctx->intr_modes[pin];
      if (mode == SYS_IO_INTR_DISABLE) continue;

      uint16_t code = 0;
      if (ads_read_channel(hw, pin, &code) != ESP_OK) {
        code = hw->recent_codes[pin];  // report the last good reading rather than nothing
      }

      bool was_active = (ctx->alert_active_mask & (1u << pin)) != 0;
      if (!was_active) {
        // Entry watch tripped: a genuine new crossing.
        ctx->alert_active_mask |= (uint8_t)(1u << pin);
        if (ctx->own_funcs[pin].own_func) {
          SYS_CB_OWN(ctx->own_funcs[pin]);
        } else {
          SYS_IO_CB(ctx, pin, mode, (int32_t)code_to_mv(ctx, code), ctx->route_masks[pin], ctx->action_masks[pin]);
        }
        ads7128_alert_cfg_t exit_cfg;
        ads_arm_exit_watch(&exit_cfg, &ctx->entry_alert_cfg[pin], (flags.high & (1u << pin)) != 0, (flags.low & (1u << pin)) != 0);
        SYS_DEV_CHECK_DRIVER_CALL(ads_set_alert_cfg(hw, pin, &exit_cfg), ctx);
        // TEMP DIAGNOSTIC
        ESP_LOGW(TAG, "ch%u entry->exit: code=%u mv=%lu, exit watch high_th=%u low_th=%u region=%d", pin, code, (unsigned long)code_to_mv(ctx, code), exit_cfg.high_th,
            exit_cfg.low_th, (int)exit_cfg.region);
      } else {
        // Exit watch tripped: genuinely recovered - re-arm the original watch.
        ctx->alert_active_mask &= (uint8_t)~(1u << pin);
        SYS_DEV_CHECK_DRIVER_CALL(ads_set_alert_cfg(hw, pin, &ctx->entry_alert_cfg[pin]), ctx);
        // TEMP DIAGNOSTIC
        ESP_LOGW(TAG, "ch%u exit->entry: code=%u mv=%lu, re-armed entry watch high_th=%u low_th=%u", pin, code, (unsigned long)code_to_mv(ctx, code), ctx->entry_alert_cfg[pin].high_th,
            ctx->entry_alert_cfg[pin].low_th);
      }
    }

    SYS_DEV_CHECK_DRIVER_CALL(ads_clear_event_flags(hw, flags.high, flags.low), ctx);
  }
  return NULL;
}

// --- VTABLE Implementations (IO Contract) ---

static err_h contract_io_ads7128_get_voltage(void* handle, sys_io_pin_num_t pin, uint32_t* out_mV) {
  SYS_DEV_GET_ADAPTER_CONTEXT(ads_adapter_ctx_t, ads_handle_t, ctx, hw, handle);
  SE_CHECK_NOT_NULL(out_mV);
  VERIFY_PIN(SYS_DEV_GET_ID(ctx), pin, PINS_MASK);

  uint16_t code = 0;
  IF_SYS_DEV_FROZEN(ctx) {
    code = ctx->cached_codes[pin];
  }
  else {
    SYS_DEV_CHECK_DRIVER_CALL(ads_read_channel(hw, pin, &code), ctx);
  }

  *out_mV = code_to_mv(ctx, code);
  return NULL;
}

/* Channels are analog inputs out of reset and this adapter exposes nothing else,
   so the only mode that can be honoured is ADC. */
static err_h contract_io_ads7128_set_mode(void* handle, sys_io_pin_num_t pin, sys_io_mode_e mode) {
  SYS_DEV_GET_ADAPTER_CONTEXT(ads_adapter_ctx_t, ads_handle_t, ctx, hw, handle);
  VERIFY_PIN(SYS_DEV_GET_ID(ctx), pin, PINS_MASK);

  if (mode != SYS_IO_MODE_ADC) {
    SE_RET_ERR(ERR_IO_PIN_MODE_UNSUPPORTED, SYS_DEV_GET_ID(ctx), pin, mode);
  }
  return NULL;
}

static err_h contract_io_ads7128_configure_intr(void* handle, sys_io_pin_num_t pin, const sys_io_intr_config_t* config) {
  SYS_DEV_GET_ADAPTER_CONTEXT(ads_adapter_ctx_t, ads_handle_t, ctx, hw, handle);
  SE_CHECK_NOT_NULL(config);
  VERIFY_PIN(SYS_DEV_GET_ID(ctx), pin, PINS_MASK);

  if (config->mode == SYS_IO_INTR_DISABLE) {
    ctx->route_masks[pin] = 0;
    ctx->action_masks[pin] = 0;
    ctx->intr_modes[pin] = SYS_IO_INTR_DISABLE;
    ctx->alert_active_mask &= (uint8_t)~(1u << pin);
    memset(&ctx->own_funcs[pin], 0, sizeof(own_funct_t));
    SYS_DEV_CHECK_DRIVER_CALL(ads_clear_alert_cfg(hw, pin), ctx);
    return NULL;
  }

  /* The on-chip window comparator is the only trigger an analog input has;
     edge modes belong to digital pins. */
  if (config->mode != SYS_IO_INTR_ADC_WINDOW_INSIDE && config->mode != SYS_IO_INTR_ADC_WINDOW_OUTSIDE) {
    SE_RET_ERR(ERR_IO_PIN_FEATURE_UNSUPPORTED, SYS_DEV_GET_ID(ctx), pin);
  }

  ads7128_alert_cfg_t alert = {
      .enabled = true,
      /* 0 mV up means "no high limit": full scale never trips the comparator */
      .high_th = (config->adc.adc_threshold_up_mV == 0) ? ADS7128_MAX_CODE : mv_to_code(ctx, config->adc.adc_threshold_up_mV),
      .low_th = mv_to_code(ctx, config->adc.adc_threshold_down_mV),
      .hysteresis = hysteresis_field(ctx, config->adc.adc_threshold_hysteresis_mV),
      .event_count = event_count_field(config->adc.adc_event_counter_threshold),
      .region = (config->mode == SYS_IO_INTR_ADC_WINDOW_INSIDE) ? ADS7128_ALERT_IN_BAND : ADS7128_ALERT_OUT_OF_BAND,
  };

  SYS_DEV_CHECK_DRIVER_CALL(ads_set_alert_cfg(hw, pin, &alert), ctx);

  ctx->route_masks[pin] = config->route_mask;
  ctx->action_masks[pin] = config->action_mask;
  ctx->intr_modes[pin] = config->mode;
  ctx->own_funcs[pin] = config->own_func;
  // Remembered so device_event_handler can restore this exact watch after a
  // crossing has been reported and recovery detected (see ads_arm_exit_watch).
  ctx->entry_alert_cfg[pin] = alert;
  // Fresh config, no crossing reported yet - let the next real violation
  // dispatch instead of carrying over whatever a previous config left behind.
  ctx->alert_active_mask &= (uint8_t)~(1u << pin);

  return NULL;
}

static err_h contract_io_ads7128_reset_pin(void* handle, sys_io_pin_num_t pin) {
  SYS_DEV_GET_ADAPTER_CONTEXT(ads_adapter_ctx_t, ads_handle_t, ctx, hw, handle);
  VERIFY_PIN(SYS_DEV_GET_ID(ctx), pin, PINS_MASK);

  ctx->route_masks[pin] = 0;
  ctx->action_masks[pin] = 0;
  ctx->intr_modes[pin] = SYS_IO_INTR_DISABLE;
  ctx->cached_codes[pin] = 0;
  memset(&ctx->own_funcs[pin], 0, sizeof(own_funct_t));

  SYS_DEV_CHECK_DRIVER_CALL(ads_clear_alert_cfg(hw, pin), ctx);
  return NULL;
}

static sys_io_vtable_t io_ads_vtable = {.io_reset = contract_io_ads7128_reset_pin,
    .io_set_mode = contract_io_ads7128_set_mode,
    .io_configure_intr = contract_io_ads7128_configure_intr,
    .io_get_voltage = contract_io_ads7128_get_voltage,
    .io_set_level = NULL,
    .io_get_level = NULL,
    .io_toggle = NULL,
    .io_set_voltage = NULL,
    .io_set_pwm_frequency = NULL,
    .io_set_pwm_duty = NULL,
    .protected_pins = 0};

// --- sys_device VTable Implementations ---

// Teardown must never early-return: a failing step would leak the i2c
// registration, the hw handle and ctx. Keep the first error, free everything.
static err_h device_uninstall(void* handle) {
  SYS_DEV_GET_ADAPTER_CONTEXT(ads_adapter_ctx_t, ads_handle_t, ctx, hw, handle);
  err_h err = NULL;

  IF_SYS_DEV_STEP_DONE(ctx, ADS_STEP_INTR_READY) {
    SYS_IO_REF_UNLOCK(ctx->cfg.intr_pin);
    SYS_DEV_TEARDOWN_STEP(err, SYS_IO_REF_RESET(ctx->cfg.intr_pin));
  }

  if (ctx->base.hw_handle) {
    IF_SYS_DEV_STEP_DONE(ctx, ADS_STEP_I2C_ADDED) {
      SYS_DEV_TEARDOWN_STEP(err, sys_i2c_remove_driver(hw));
    }
    ads_delete(hw);
  }
  free(ctx);
  return err;
}

static err_h device_reset(void* handle) {
  SYS_DEV_GET_ADAPTER_CONTEXT(ads_adapter_ctx_t, ads_handle_t, ctx, hw, handle);

  for (uint8_t pin = 0; pin < PINS_COUNT; pin++) {
    ctx->route_masks[pin] = 0;
    ctx->action_masks[pin] = 0;
    ctx->intr_modes[pin] = SYS_IO_INTR_DISABLE;
    ctx->cached_codes[pin] = 0;
    memset(&ctx->own_funcs[pin], 0, sizeof(own_funct_t));
  }

  SYS_DEV_CHECK_DRIVER_CALL(ads_reset(hw), ctx);
  return NULL;
}

/* The chip has no shutdown state: it simply stops converting once the sequencer
   is idle, which is what a manual-mode configuration already gives. */
static err_h device_suspend(void* handle) {
  SYS_DEV_GET_ADAPTER_CONTEXT(ads_adapter_ctx_t, ads_handle_t, ctx, hw, handle);
  return NULL;
}

static err_h device_resume(void* handle) {
  SYS_DEV_GET_ADAPTER_CONTEXT(ads_adapter_ctx_t, ads_handle_t, ctx, hw, handle);
  SYS_DEV_CHECK_DRIVER_CALL(ads_restore_state(hw), ctx);
  return NULL;
}

/* Frozen readings are served from a snapshot, so a whole control cycle sees one
   consistent set of samples no matter how often it asks. */
static err_h device_freeze(void* handle) {
  SYS_DEV_GET_ADAPTER_CONTEXT(ads_adapter_ctx_t, ads_handle_t, ctx, hw, handle);
  IF_SYS_DEV_FROZEN(ctx) {
    return NULL;
  }

  SYS_DEV_CHECK_DRIVER_CALL(ads_read_channels(hw, ADS7128_CH_MASK_ALL), ctx);
  for (uint8_t ch = 0; ch < PINS_COUNT; ch++) {
    ctx->cached_codes[ch] = hw->recent_codes[ch];
  }

  SYS_DEV_CTX_FREEZE(ctx);
  return NULL;
}

static err_h device_sync(void* handle) {
  SYS_DEV_GET_ADAPTER_CONTEXT(ads_adapter_ctx_t, ads_handle_t, ctx, hw, handle);
  SYS_DEV_CTX_UNFREEZE(ctx);

  // Inputs only: nothing was deferred, the snapshot just gets refreshed
  SYS_DEV_CHECK_DRIVER_CALL(ads_read_channels(hw, ADS7128_CH_MASK_ALL), ctx);
  for (uint8_t ch = 0; ch < PINS_COUNT; ch++) {
    ctx->cached_codes[ch] = hw->recent_codes[ch];
  }
  return NULL;
}

// Same shape as device_pca9685's explain_root_cause() (see that file for
// the full rationale): identifies which node in the chain is the root
// cause and adds this adapter's own interpretation for it, without
// repeating SE_describe_payload() - sys_error_handler_task's own stack
// trace already prints that same description for every node, including
// the root. Every ERR_ESP_ERR reaching this adapter's error_handler
// originates from an I2C driver call (SYS_DEV_CHECK_DRIVER_CALL wraps every
// ads_*() call, all of which go over I2C).
static void explain_root_cause(uint8_t device_id, err_h error) {
  err_h root = error;
  while (root && root->next_cause) root = root->next_cause;
  if (!root) return;
  ESP_LOGE(TAG, "ADS7128 (device %u) error root cause: owner=%s (0x%04X), tag=%s (%d)", device_id, SE_get_owner_name(root->owner), (unsigned int)root->owner, SE_get_tag_name(root->tag), (int)root->tag);

  if (root->tag == ERR_ESP_ERR) {
    ESP_LOGE(TAG, "  -> communication with ADS7128 (device %u) failed - check that it is connected, powered, and present at the configured I2C bus/address", device_id);
  }
}

static err_h device_error_handler(void* handle, err_h error) {
  ads_adapter_ctx_t* ctx = (ads_adapter_ctx_t*)handle;
  SYS_DEV_CHECK_HANDLE(ctx, 0);
  sys_device_t* dev = sys_device_get_by_id(SYS_DEV_GET_ID(ctx));
  if (!dev) return NULL;

  explain_root_cause(SYS_DEV_GET_ID(ctx), error);

  if (dev->generate_error_callback) {
    // TODO: report to the VM via the callback system. Payload should carry
    // at least: device_id, and the root cause's tag/owner - walk
    // error->next_cause to the end, since a wrapper like ERR_DEV_DEP_FAILED
    // only carries dev_id, not the underlying failure's tag/owner. Always
    // attach device_id explicitly (the root cause itself may not carry one).
    return NULL;
  }

  if (dev->use_error_handler) {
    // TODO: classify `error` into a sys_device_err_level_e (critical/
    // warning/notice) and sys_actions_invoke(dev->actions[level]).
  }
  return NULL;
}

static err_h device_install(const void* cfg_blob, void** out_device_handle) {
  const d_ads7128_cfg_t* cfg = (const d_ads7128_cfg_t*)cfg_blob;
  SE_CHECK_NOT_NULL(cfg);
  SE_CHECK_NOT_NULL(out_device_handle);

  SYS_DEV_CTX_NEW(ads_adapter_ctx_t, ctx, cfg);
  err_h err = NULL;

  // Every reading and every threshold is scaled by this, so it may not be zero
  if (ctx->cfg.vref_mv == 0) {
    ESP_LOGE(TAG, "install: vref_mv must be non-zero");
    free(ctx);
    SE_RET_ERR(ERR_INVALID_VAL_UI32, .val = 0, .min = 1, .max = UINT32_MAX);
  }
  ctx->mv_per_code = (float)ctx->cfg.vref_mv / (float)ADS7128_MAX_CODE;

  ctx->base.hw_handle = ads_new(ctx->cfg.i2c_addr, ctx->cfg.i2c_bus);
  if (!ctx->base.hw_handle) {
    free(ctx);
    SE_RET_ERR(ERR_BASE_NO_MEM, 0);
  }

  ads_handle_t hw = (ads_handle_t)ctx->base.hw_handle;

  SYS_DEV_INSTALL_STEP(sys_i2c_add_driver(ctx->base.hw_handle), "i2c add driver");
  SYS_DEV_STEP_DONE(ctx, ADS_STEP_I2C_ADDED);

  SYS_DEV_INSTALL_STEP(sys_i2c_device_present(ctx->base.hw_handle), "probe i2c device");
  SYS_DEV_INSTALL_STEP(SE_CONVERT_ESP(ads_start(hw)), "chip start");

  IF_PIN_REF(ctx->cfg.intr_pin) {
    SYS_DEV_INSTALL_STEP(SYS_IO_REF_SET_MODE(ctx->cfg.intr_pin), "intr pin mode");
    // ALERT is active low, so the falling edge is the assertion
    sys_io_intr_config_t intr_cfg = {
        .mode = SYS_IO_INTR_MODE_FALLING_EDGE,
        .own_func = {.own_func = device_event_handler, .device_handle = ctx},
    };
    SYS_DEV_INSTALL_STEP(sys_io_configure_intr(ctx->cfg.intr_pin.device_id, ctx->cfg.intr_pin.pin, &intr_cfg), "intr pin configure");
    SYS_IO_REF_LOCK(ctx->cfg.intr_pin);
    SYS_DEV_STEP_DONE(ctx, ADS_STEP_INTR_READY);
  }

  SYS_DEV_INSTALL_STEP(SE_CONVERT_ESP(ads_read_channels(hw, ADS7128_CH_MASK_ALL)), "initial read");
  for (uint8_t ch = 0; ch < PINS_COUNT; ch++) {
    ctx->cached_codes[ch] = hw->recent_codes[ch];
  }

  ESP_LOGI(TAG, "ADS7128 successfully installed as Device ID %d", ctx->cfg.device_id);
  *out_device_handle = ctx;
  return NULL;

fail:
  SYS_DEV_INSTALL_FAIL(err, cfg->device_id, out_device_handle, device_uninstall, ctx);
  return NULL;
}

// The IO contract is declared here, not registered imperatively during install.
static const sys_device_class_t s_ads7128_class = {
    .name = "ADS7128_ADC",
    .contracts = {[SYS_DEVICE_CONTRACT_IO] = (void*)&io_ads_vtable},
    .ops = {.install = device_install, .uninstall = device_uninstall, .reset = device_reset, .suspend = device_suspend, .resume = device_resume, .freeze = device_freeze, .sync = device_sync, .error_handler = device_error_handler},
};

// --- Exposed Initialization API ---
err_h d_ads7128_create(const d_ads7128_cfg_t* cfg) {
  SE_CHECK_NOT_NULL(cfg);
  return SYS_DEVICE_CREATE(&s_ads7128_class, cfg);
}
