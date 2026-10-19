#include "device_power_mux.h"
#include "sys_pins.h"
#include "utils.h"

typedef struct {
  sys_device_base_t base;  // first: id, suspended (both sources off, writes refused until RESUME)
  sys_pins_owned_t en[2];  // source A, source B
  bool active_high;
  uint8_t switch_delay_ms;
  sys_power_supply_source_e source;  // what is switched on now
  sys_power_supply_source_e wanted;  // what the user chose; restored by RESUME
} mux_t;

static uint8_t en_index(sys_power_supply_source_e source) {
  return source == SYS_POWER_SUPPLY_A ? 0 : 1;
}

/* Drive one enable pin to "on" or "off" (the pin is locked after create). */
static err_h mux_set_switch(mux_t* c, uint8_t index, bool on) {
  return sys_pins_drive(&c->en[index], on == c->active_high);
}

/* Break before make: the source that is on goes off, the delay passes, then the new one goes on. A failure leaves
   both off. The delay is the second bounded wait inside an op (SYS_DEVICE.MD "Concurrency"): at most
   POWER_MUX_MAX_SWITCH_DELAY_MS, the hardware's own switching time. */
static err_h mux_apply(mux_t* c, sys_power_supply_source_e target) {
  if (c->source == target) {
    return NULL;
  }
  if (c->source != SYS_POWER_SUPPLY_OFF) {
    ERR_TRY(mux_set_switch(c, en_index(c->source), false));
    c->source = SYS_POWER_SUPPLY_OFF;
    if (target != SYS_POWER_SUPPLY_OFF) {
      vTaskDelay(MSEC(c->switch_delay_ms));
    }
  }
  if (target == SYS_POWER_SUPPLY_OFF) {
    return NULL;
  }
  ERR_TRY(mux_set_switch(c, en_index(target), true));
  c->source = target;
  return NULL;
}

/* ---- POWER_SUPPLY contract ---- */

SUPPLY_HANDLER(SELECT) {
  mux_t* c = ctx;
  ERR_TRY(sys_device_check_active(&c->base));
  SYS_DEV_CHECK_IN_RANGE(c->base.id, args->source, SYS_POWER_SUPPLY_OFF, SYS_POWER_SUPPLY_B);
  c->wanted = (sys_power_supply_source_e)args->source;
  return mux_apply(c, c->wanted);
}

SUPPLY_HANDLER(GET_SOURCE) {
  (void)args;
  const uint8_t source = (uint8_t)((mux_t*)ctx)->source;
  return cmd_reply(out, out_cap, out_len, &source, sizeof(source));
}

/* ---- Lifecycle ---- */

/* Back to the create state: both sources off, nothing wanted. */
DEVICE_HANDLER(RESET) {
  mux_t* c = ctx;
  ERR_TRY(sys_device_check_active(&c->base));
  c->wanted = SYS_POWER_SUPPLY_OFF;
  return mux_apply(c, SYS_POWER_SUPPLY_OFF);
}

/* Fault safe state: both sources off, whatever the state was. The wanted source is kept for RESUME. The device counts
   as suspended even if a step failed. */
static err_h suspend_hook(void* ctx) {
  mux_t* c = ctx;
  err_h err = NULL;
  for (uint8_t i = 0; i < 2; i++) {
    ERR_KEEP_FIRST(err, mux_set_switch(c, i, false));
  }
  c->source = SYS_POWER_SUPPLY_OFF;
  return err;
}

/* Back to the wanted source. A failing resume leaves the device suspended with both sources off. */
static err_h resume_hook(void* ctx) {
  mux_t* c = ctx;
  err_h err = mux_apply(c, c->wanted);
  if (err != NULL) {
    for (uint8_t i = 0; i < 2; i++) {
      ERR_KEEP_FIRST(err, mux_set_switch(c, i, false));
    }
    c->source = SYS_POWER_SUPPLY_OFF;
    return err;
  }
  return NULL;
}

/* ---- Create / uninstall ---- */

// Releases the hardware; REMOVE and the create rollback. Each step runs only if it was built, none returns early.
static err_h uninstall(void* ctx) {
  mux_t* c = ctx;
  err_h err = NULL;
  for (uint8_t i = 0; i < 2; i++) {
    ERR_KEEP_FIRST(err, mux_set_switch(c, i, false));  // off before the pin is let go (a no-op if not locked)
    ERR_KEEP_FIRST(err, sys_pins_release(&c->en[i]));
  }
  return err;
}

static err_h create(void* ctx, const void* cfg_in) {
  mux_t* c = ctx;
  const power_mux_cfg_t* cfg = cfg_in;
  const uint16_t id = c->base.id;
  SYS_DEV_CHECK_IN_RANGE(id, cfg->active_level, 0, 1);
  SYS_DEV_CHECK_IN_RANGE(id, cfg->switch_delay_ms, POWER_MUX_MIN_SWITCH_DELAY_MS, POWER_MUX_MAX_SWITCH_DELAY_MS);
  // Both switches need their own enable pin; one pin for both could switch the two sources on together.
  ERR_CHECK(sys_io_pin_valid(cfg->en_a_pin), ERR_DEV_BAD_PIN, .id = id, .pin = cfg->en_a_pin.pin);
  ERR_CHECK(sys_io_pin_valid(cfg->en_b_pin), ERR_DEV_BAD_PIN, .id = id, .pin = cfg->en_b_pin.pin);
  ERR_CHECK(cfg->en_a_pin.device != cfg->en_b_pin.device || cfg->en_a_pin.pin != cfg->en_b_pin.pin, ERR_DEV_BAD_PIN,
            .id = id, .pin = cfg->en_b_pin.pin);

  c->active_high = cfg->active_level != 0;
  c->switch_delay_ms = cfg->switch_delay_ms;
  c->source = SYS_POWER_SUPPLY_OFF;
  c->wanted = SYS_POWER_SUPPLY_OFF;

  // Both switches off before the pins are locked: nothing is fed until a source is selected.
  const sys_io_pin_t en_pin[2] = {cfg->en_a_pin, cfg->en_b_pin};
  for (uint8_t i = 0; i < 2; i++) {
    ERR_TRY(sys_pins_take_output(&c->base, &c->en[i], en_pin[i], !c->active_high));
  }
  return NULL;
}

/* ---- Type ---- */

static const sys_device_op_t k_ops[] = {
    DEVICE_ENTRY(RESET),
    SUPPLY_ENTRY(SELECT),
    SUPPLY_ENTRY(GET_SOURCE),
};

SYS_DEVICE_TYPE_DEFINE(.type = SYS_DEVICE_TYPE_POWER_MUX, .cfg_size = sizeof(power_mux_cfg_t), .ctx_size = sizeof(mux_t),
                       .create = create, .uninstall = uninstall, .suspend = suspend_hook, .resume = resume_hook,
                       SYS_DEVICE_OPS(k_ops))
