#include <stdlib.h>
#include "device_power_mux.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sys_device.h"
#include "sys_error.h"
#include "sys_io.h"
#include "sys_power.h"

#define OWNER OWNER_DEVICE

// Instance state: the whole device in one struct (the create cfg is not kept).
typedef struct mux_ctx_t {
  sys_device_base_t base;  // must be first
  sys_io_pin_ref_t en[2];  // source A, source B
  bool active_high;
  uint8_t switch_delay_ms;
  sys_power_supply_source_e source;  // what is switched on now
  sys_power_supply_source_e wanted;  // what the user chose; restored by resume
} mux_ctx_t;

enum { MUX_STEP_EN_A = 0, MUX_STEP_EN_B = 1 };

static uint8_t en_index(sys_power_supply_source_e source) {
  return source == SYS_POWER_SUPPLY_A ? 0 : 1;
}

/* Drive one enable pin to "on" or "off" (the pin is locked after install). */
static SE_MUST_USE err_h mux_set_switch(mux_ctx_t* c, uint8_t index, bool on) {
  return sys_io_set_locked_level(c->en[index], on == c->active_high);
}

/* Break before make: the source that is on goes off, the delay passes, then the new one goes on. A failure
   leaves both off. */
static SE_MUST_USE err_h mux_apply(mux_ctx_t* c, sys_power_supply_source_e target) {
  if (c->source == target) return NULL;
  if (c->source != SYS_POWER_SUPPLY_OFF) {
    SE_TRY(mux_set_switch(c, en_index(c->source), false));
    c->source = SYS_POWER_SUPPLY_OFF;
    if (target != SYS_POWER_SUPPLY_OFF) vTaskDelay(pdMS_TO_TICKS(c->switch_delay_ms));
  }
  if (target == SYS_POWER_SUPPLY_OFF) return NULL;
  SE_TRY(mux_set_switch(c, en_index(target), true));
  c->source = target;
  return NULL;
}

/* ---- Power supply contract ---- */

static SE_MUST_USE err_h supply_select(void* handle, sys_power_supply_source_e source) {
  SYS_DEV_CTX_FROM(mux_ctx_t, c, handle);
  SE_CHECK_IN_RANGE((uint32_t)source, SYS_POWER_SUPPLY_OFF, SYS_POWER_SUPPLY_B);
  c->wanted = source;
  return mux_apply(c, source);
}

static SE_MUST_USE err_h supply_get_source(void* handle, sys_power_supply_source_e* out_source) {
  SYS_DEV_CTX_FROM(mux_ctx_t, c, handle);
  *out_source = c->source;
  return NULL;
}

static const sys_power_supply_contract_t s_power_mux_contract = {.select = supply_select, .get_source = supply_get_source};

/* ---- Lifecycle ---- */

// Doubles as the install rollback path: each step is gated on having actually run, and no step may early-return.
static SE_MUST_USE err_h device_uninstall(void* handle) {
  SYS_DEV_CTX_FROM(mux_ctx_t, c, handle);
  err_h err = NULL;

  for (uint8_t i = 0; i < 2; i++) {
    IF_SYS_DEV_STEP_DONE(c, i == 0 ? MUX_STEP_EN_A : MUX_STEP_EN_B) {
      SYS_DEV_TEARDOWN_STEP(err, mux_set_switch(c, i, false));
      SYS_DEV_TEARDOWN_STEP(err, sys_io_unlock_pin(c->en[i]));
      SYS_DEV_TEARDOWN_STEP(err, sys_io_reset(c->en[i]));
    }
  }
  free(c);
  return err;
}

/* Back to the install state: both sources off, nothing wanted. */
static SE_MUST_USE err_h device_reset(void* handle) {
  SYS_DEV_CTX_FROM(mux_ctx_t, c, handle);
  c->wanted = SYS_POWER_SUPPLY_OFF;
  return mux_apply(c, SYS_POWER_SUPPLY_OFF);
}

// Fault safe state: both sources off. The wanted source is kept for resume.
static SE_MUST_USE err_h device_suspend(void* handle) {
  SYS_DEV_CTX_FROM(mux_ctx_t, c, handle);
  err_h err = NULL;
  for (uint8_t i = 0; i < 2; i++) SYS_DEV_TEARDOWN_STEP(err, mux_set_switch(c, i, false));
  c->source = SYS_POWER_SUPPLY_OFF;
  return err;
}

static SE_MUST_USE err_h device_resume(void* handle) {
  SYS_DEV_CTX_FROM(mux_ctx_t, c, handle);
  return mux_apply(c, c->wanted);
}

static SE_MUST_USE err_h device_install(const void* cfg_blob, void** out_device_handle) {
  const d_power_mux_cfg_t* cfg = (const d_power_mux_cfg_t*)cfg_blob;
  SE_CHECK_IN_RANGE(cfg->active_level, 0, 1);
  SE_CHECK_IN_RANGE(cfg->switch_delay_ms, 1, 100);
  // Both switches need their own enable pin; one pin for both could switch the two sources on together.
  if (!sys_io_pin_is_valid(cfg->en_a_pin)) SE_FAIL(ERR_IO_PIN_UNAVAILABLE, .dev_id = cfg->en_a_pin.device_id, .pin_num = cfg->en_a_pin.pin);
  if (!sys_io_pin_is_valid(cfg->en_b_pin)) SE_FAIL(ERR_IO_PIN_UNAVAILABLE, .dev_id = cfg->en_b_pin.device_id, .pin_num = cfg->en_b_pin.pin);
  if (cfg->en_a_pin.device_id == cfg->en_b_pin.device_id && cfg->en_a_pin.pin == cfg->en_b_pin.pin) {
    SE_FAIL(ERR_IO_PIN_ALREADY_IN_USE, .dev_id = cfg->en_b_pin.device_id, .pin_num = cfg->en_b_pin.pin, .mode = cfg->en_b_pin.mode);
  }

  mux_ctx_t* c = (mux_ctx_t*)calloc(1, sizeof(mux_ctx_t));
  SE_CHECK_IF_ALLOCATED(c);
  c->base.device_id = cfg->device_id;
  c->en[0] = cfg->en_a_pin;
  c->en[1] = cfg->en_b_pin;
  c->active_high = cfg->active_level != 0;
  c->switch_delay_ms = cfg->switch_delay_ms;
  c->source = SYS_POWER_SUPPLY_OFF;
  c->wanted = SYS_POWER_SUPPLY_OFF;
  err_h err = NULL;

  // Both switches off before the pins are locked: nothing is fed until a source is selected.
  for (uint8_t i = 0; i < 2; i++) {
    SYS_DEV_INSTALL_STEP(sys_io_set_mode(c->en[i]), "enable pin mode");
    SYS_DEV_INSTALL_STEP(sys_io_set_level(c->en[i], !c->active_high), "enable pin off");
    SYS_DEV_INSTALL_STEP_BIT(c, i == 0 ? MUX_STEP_EN_A : MUX_STEP_EN_B, sys_io_lock_pin(c->en[i]), "enable pin lock");
  }

  *out_device_handle = c;
  return NULL;

fail:
  SYS_DEV_INSTALL_FAIL(err, out_device_handle, device_uninstall, c);
  return NULL;
}

static const uint8_t s_pin_refs[] = {offsetof(d_power_mux_cfg_t, en_a_pin), offsetof(d_power_mux_cfg_t, en_b_pin)};

const sys_device_class_t g_power_mux_class = {
    .type_id = POWER_MUX_TYPE_ID,
    .cfg_size = sizeof(d_power_mux_cfg_t),
    SYS_DEVICE_PINS(s_pin_refs),
    .name = "POWER_MUX",
    .contracts = {[SYS_DEVICE_CONTRACT_POWER_SUPPLY] = &s_power_mux_contract},
    .ops = {.install = device_install,
        .uninstall = device_uninstall,
        .reset = device_reset,
        .suspend = device_suspend,
        .resume = device_resume},
};
