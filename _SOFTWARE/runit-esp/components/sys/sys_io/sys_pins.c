#include "sys_pins.h"
#include "sys_event.h"

void sys_pins_state_init(sys_pins_state_t* s, uint8_t count) {
  s->available = count >= 64 ? ~(uint64_t)0 : BIT64(count) - 1;
  s->locked = 0;
}

err_h sys_pins_state_check(const sys_pins_state_t* s, uint16_t dev_id, uint8_t pin) {
  ERR_CHECK(pin < 64 && (s->available & BIT64(pin)) != 0, ERR_DEV_BAD_PIN, .id = dev_id, .pin = pin);
  return NULL;
}

err_h sys_pins_state_check_free(const sys_pins_state_t* s, uint16_t dev_id, uint8_t pin) {
  ERR_TRY(sys_pins_state_check(s, dev_id, pin));
  ERR_CHECK((s->locked & BIT64(pin)) == 0, ERR_DEV_PIN_LOCKED, .id = dev_id, .pin = pin);
  return NULL;
}

err_h sys_pins_state_lock(sys_pins_state_t* s, uint16_t dev_id, uint8_t pin) {
  ERR_TRY(sys_pins_state_check_free(s, dev_id, pin));
  s->locked |= BIT64(pin);
  return NULL;
}

err_h sys_pins_state_unlock(sys_pins_state_t* s, uint16_t dev_id, uint8_t pin) {
  ERR_TRY(sys_pins_state_check(s, dev_id, pin));
  s->locked &= ~BIT64(pin);
  return NULL;
}

err_h sys_pins_reply(uint8_t* out, uint32_t out_cap, uint32_t* out_len, uint64_t mask) {
  return cmd_reply(out, out_cap, out_len, &mask, sizeof(mask));  // little-endian CPU
}

/* ---- Wrappers ---- */

static err_h get_mask(uint16_t device, uint16_t op, uint64_t* mask) {
  ERR_CHECK(mask != NULL, ERR_DEV_BAD_ARG);
  return sys_device_read(device, op, NULL, 0, mask, sizeof(*mask));
}

err_h sys_pins_available(uint16_t device, uint64_t* mask) {
  return get_mask(device, PINS_OP_AVAILABLE, mask);
}

err_h sys_pins_locked(uint16_t device, uint64_t* mask) {
  return get_mask(device, PINS_OP_LOCKED, mask);
}

err_h sys_pins_lock(sys_io_pin_t p) {
  const PINS_value_LOCK_t a = {.pin = p.pin};
  return sys_device_write(p.device, PINS_OP_LOCK, &a, sizeof(a));
}

err_h sys_pins_unlock(sys_io_pin_t p) {
  const PINS_value_UNLOCK_t a = {.pin = p.pin};
  return sys_device_write(p.device, PINS_OP_UNLOCK, &a, sizeof(a));
}

err_h sys_pins_set_locked_level(sys_io_pin_t p, bool level) {
  const PINS_value_LOCKED_LEVEL_t a = {.pin = p.pin, .level = level};
  return sys_device_write(p.device, PINS_OP_LOCKED_LEVEL, &a, sizeof(a));
}

/* ----------------------------------------------------------------------------
 * A device's own pins (sys_pins.h): take / drive / release in one order for every driver.
 * ------------------------------------------------------------------------- */

// The owner's ON_EVENT on the pin's `event`, inline: in the publisher's task (an I2C read, no delay).
static err_h subscribe(sys_device_base_t* dev, sys_pins_owned_t* p, uint16_t event) {
  if (event == 0) return NULL;
  ERR_TRY(sys_event_subscribe_device(p->pin.device, event, p->pin.pin, dev->id, &p->sub));
  p->subscribed = true;
  return NULL;
}

err_h sys_pins_take(sys_device_base_t* dev, sys_pins_owned_t* p, sys_io_pin_t pin, sys_io_mode_e mode,
                    uint16_t event) {
  p->pin = pin;  // SYS_IO_PIN_NONE stays NONE: the struct's pin is what the driver checks
  if (!sys_io_pin_valid(pin)) return NULL;
  ERR_TRY(sys_device_use(dev, pin.device));
  ERR_TRY(sys_io_prepare(pin, mode));
  p->used = true;
  return subscribe(dev, p, event);
}

err_h sys_pins_take_output(sys_device_base_t* dev, sys_pins_owned_t* p, sys_io_pin_t pin, bool level) {
  ERR_TRY(sys_pins_take(dev, p, pin, SYS_IO_MODE_OUTPUT_PUSH_PULL, 0));
  if (!sys_io_pin_valid(pin)) return NULL;
  ERR_TRY(sys_io_set_level(pin, level));
  ERR_TRY(sys_pins_lock(pin));
  p->locked = true;
  return NULL;
}

err_h sys_pins_take_input(sys_device_base_t* dev, sys_pins_owned_t* p, sys_io_pin_t pin, uint16_t event,
                          uint8_t mode) {
  p->pin = pin;
  if (!sys_io_pin_valid(pin)) return NULL;
  ERR_TRY(sys_device_use(dev, pin.device));
  ERR_TRY(sys_io_prepare_input(pin));
  p->used = true;
  ERR_TRY(subscribe(dev, p, event));
  const sys_io_intr_cfg_t cfg = {.mode = mode};
  ERR_TRY(sys_io_configure_intr(pin, &cfg));
  ERR_TRY(sys_pins_lock(pin));
  p->locked = true;
  return NULL;
}

err_h sys_pins_drive(const sys_pins_owned_t* p, bool level) {
  return p->locked ? sys_pins_set_locked_level(p->pin, level) : NULL;
}

err_h sys_pins_release(sys_pins_owned_t* p) {
  err_h err = NULL;
  if (p->subscribed) {
    ERR_KEEP_FIRST(err, sys_event_unsubscribe(p->sub));
    p->subscribed = false;
  }
  if (p->locked) {
    ERR_KEEP_FIRST(err, sys_pins_unlock(p->pin));
    p->locked = false;
  }
  if (p->used) {
    if (!p->keep) ERR_KEEP_FIRST(err, sys_io_release(p->pin));
    p->used = false;
  }
  return err;
}
