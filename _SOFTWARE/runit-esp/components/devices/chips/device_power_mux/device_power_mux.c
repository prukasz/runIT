/*
 * Power mux: two switched sources into one load (the LM73100 ideal diodes that feed an H-bridge), each switched by an
 * enable pin of another device. The whole device is this file: nothing includes it, the rest of the firmware reaches
 * it by its ID and the POWER_SUPPLY contract (SUPPLY). Break before make: the source that is on goes off, the switch
 * delay passes, then the new one goes on, so the two are never on together.
 */
#include "sys_device_driver.h"
#include "sys_io_driver.h"
#include "sys_power_supply_contract.h"
#include "utils.h"

SYS_DEVICE_TYPE_DECLARE(POWER_MUX, 0x004A)

#define POWER_MUX_MIN_SWITCH_DELAY_MS 1u
#define POWER_MUX_MAX_SWITCH_DELAY_MS 100u

/** @brief CREATE's struct (wire format). */
typedef struct __attribute__((packed)) {
  sys_io_pin_t en_a_pin;    /**< Source A enable control pin */
  sys_io_pin_t en_b_pin;    /**< Source B enable control pin */
  uint8_t active_level;     /**< 1 = active high, 0 = active low */
  uint8_t switch_delay_ms;  /**< Break-before-make delay in ms */
} power_mux_cfg_t;

typedef struct {
  uint8_t id;
  sys_io_claim_t en[2];  // source A, source B: locked outputs
  bool active_high;
  uint8_t switch_delay_ms;
  uint8_t source;        // sys_power_supply_source_e: what is on now
  uint8_t wanted;        // what was chosen: RESUME restores it
} mux_t;

#define SYS_DEVICE_CTX mux_t

// Both sources off, every pin written even if one fails.
static err_h mux_off(mux_t* c) {
  err_h err = NULL;
  ERR_KEEP_FIRST(err, sys_io_claim_drive(&c->en[0], !c->active_high));
  ERR_KEEP_FIRST(err, sys_io_claim_drive(&c->en[1], !c->active_high));
  c->source = SYS_POWER_SUPPLY_OFF;
  return err;
}

/* Break before make; a failure leaves both off. The delay is the hardware's own switching time, at most
 * POWER_MUX_MAX_SWITCH_DELAY_MS: a bounded wait inside an op. */
static err_h mux_apply(mux_t* c, uint8_t target) {
  if (c->source == target) {
    return NULL;
  }
  const bool was_on = c->source != SYS_POWER_SUPPLY_OFF;
  ERR_TRY(mux_off(c));
  if (target == SYS_POWER_SUPPLY_OFF) {
    return NULL;
  }
  if (was_on) {
    vTaskDelay(MSEC(c->switch_delay_ms));
  }
  err_h err = sys_io_claim_drive(&c->en[target == SYS_POWER_SUPPLY_A ? 0 : 1], c->active_high);
  if (err != NULL) {
    ERR_KEEP_FIRST(err, mux_off(c));
    return err;
  }
  c->source = target;
  return NULL;
}

/*******************************************************************************
 * CONTRACT_SUPPLY : OP_SOURCE (write / read)
 ******************************************************************************/
OP_DECLARE_WRITE(CONTRACT_SUPPLY, OP_SOURCE) {
  ERR_CHECK_RANGE(args->source, SYS_POWER_SUPPLY_OFF, SYS_POWER_SUPPLY_B);
  c->wanted = args->source;
  return mux_apply(c, c->wanted);
}

OP_DECLARE_READ(CONTRACT_SUPPLY, OP_SOURCE) {
  return CMD_REPLY(CONTRACT_SUPPLY, OP_SOURCE, .source = c->source);
}

/*******************************************************************************
 * CONTRACT_SYS_DEVICE : OP_RESET (write)
 * Both sources off, nothing chosen.
 ******************************************************************************/
OP_DECLARE_WRITE(CONTRACT_SYS_DEVICE, OP_RESET) {
  c->wanted = SYS_POWER_SUPPLY_OFF;
  return mux_off(c);
}

// Both off; the chosen source is kept for resume.
static err_h suspend(mux_t* c) {
  return mux_off(c);
}

static err_h resume(mux_t* c) {
  return mux_apply(c, c->wanted);
}

static err_h create(uint8_t id, mux_t* c, const power_mux_cfg_t* cfg) {
  ERR_CHECK(cfg->active_level <= 1, ERR_BAD_ARG);
  ERR_CHECK_RANGE(cfg->switch_delay_ms, POWER_MUX_MIN_SWITCH_DELAY_MS, POWER_MUX_MAX_SWITCH_DELAY_MS);
  // Each switch has its own pin: one pin for both could switch the two sources on together.
  ERR_CHECK(sys_io_pin_valid(cfg->en_a_pin), ERR_DEV_BAD_PIN, .id = id, .pin = cfg->en_a_pin.pin);
  ERR_CHECK(sys_io_pin_valid(cfg->en_b_pin), ERR_DEV_BAD_PIN, .id = id, .pin = cfg->en_b_pin.pin);
  ERR_CHECK(cfg->en_a_pin.device != cfg->en_b_pin.device || cfg->en_a_pin.pin != cfg->en_b_pin.pin, ERR_DEV_BAD_PIN,
            .id = id, .pin = cfg->en_b_pin.pin);
  c->id = id;
  c->active_high = cfg->active_level != 0;
  c->switch_delay_ms = cfg->switch_delay_ms;
  // Both off when the pins are taken: nothing is fed until a source is chosen. Kept off on REMOVE.
  ERR_TRY(sys_io_claim_output(id, &c->en[0], cfg->en_a_pin, !c->active_high));
  ERR_TRY(sys_io_claim_output(id, &c->en[1], cfg->en_b_pin, !c->active_high));
  c->en[0].keep = c->en[1].keep = true;
  return NULL;
}

static const sys_device_op_t k_ops[] = {
    OP_ENTRY_W(CONTRACT_SYS_DEVICE, OP_RESET),
    OP_ENTRY(CONTRACT_SUPPLY, OP_SOURCE),
};

SYS_DEVICE_TYPE_DEFINE(POWER_MUX, power_mux_cfg_t, create, suspend, resume, k_ops)
