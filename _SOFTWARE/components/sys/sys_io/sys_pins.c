#include "sys_pins.h"


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
  uint64_t out = 0;
  uint32_t out_len = 0;
  ERR_CHECK(mask != NULL, ERR_DEV_BAD_ARG);
  ERR_TRY(sys_device_call(device, op, NULL, 0, &out, sizeof(out), &out_len));
  ERR_CHECK(out_len == sizeof(out), ERR_CMD_BAD_LEN, .got = (uint16_t)out_len, .need = sizeof(out));
  *mask = out;
  return NULL;
}

err_h sys_pins_available(uint16_t device, uint64_t* mask) {
  return get_mask(device, PINS_OP_AVAILABLE, mask);
}

err_h sys_pins_locked(uint16_t device, uint64_t* mask) {
  return get_mask(device, PINS_OP_LOCKED, mask);
}

err_h sys_pins_lock(sys_io_pin_t p) {
  const pins_args_LOCK_t a = {.pin = p.pin};
  return sys_device_call(p.device, PINS_OP_LOCK, &a, sizeof(a), NULL, 0, NULL);
}

err_h sys_pins_unlock(sys_io_pin_t p) {
  const pins_args_UNLOCK_t a = {.pin = p.pin};
  return sys_device_call(p.device, PINS_OP_UNLOCK, &a, sizeof(a), NULL, 0, NULL);
}

err_h sys_pins_set_locked_level(sys_io_pin_t p, bool level) {
  const pins_args_SET_LOCKED_LEVEL_t a = {.pin = p.pin, .level = level};
  return sys_device_call(p.device, PINS_OP_SET_LOCKED_LEVEL, &a, sizeof(a), NULL, 0, NULL);
}
