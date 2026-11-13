#include "sys_io_driver.h"

#include <string.h>

_Static_assert(SYS_IO_PINS_MAX == 64, "the masks are u64");

static bool is_locked(const sys_io_pins_t* s, uint8_t pin) {
  return (s->locked & (1ULL << pin)) != 0;
}

void sys_io_pins_init(sys_io_pins_t* s, uint8_t count) {
  s->available = count >= SYS_IO_PINS_MAX ? ~(uint64_t)0 : (1ULL << count) - 1u;
  s->locked = 0;
  memset(s->owner, SYS_DEVICE_ID_NONE, sizeof(s->owner));
}

err_h sys_io_pins_check(const sys_io_pins_t* s, uint8_t dev, uint8_t pin) {
  ERR_CHECK(pin < SYS_IO_PINS_MAX && (s->available & (1ULL << pin)) != 0, ERR_DEV_BAD_PIN, .id = dev, .pin = pin);
  return NULL;
}

// A locked pin is its owner's to write; one locked for no device is nobody's.
err_h sys_io_pins_check_free(const sys_io_pins_t* s, uint8_t dev, uint8_t pin) {
  ERR_TRY(sys_io_pins_check(s, dev, pin));
  const uint8_t owner = s->owner[pin];
  const bool mine = owner != SYS_DEVICE_ID_NONE && owner == sys_device_caller();
  ERR_CHECK(!is_locked(s, pin) || mine, ERR_DEV_PIN_LOCKED, .id = dev, .pin = pin, .owner = owner);
  return NULL;
}

err_h sys_io_pins_lock(sys_io_pins_t* s, uint8_t dev, uint8_t pin) {
  ERR_TRY(sys_io_pins_check(s, dev, pin));
  ERR_CHECK(!is_locked(s, pin), ERR_DEV_PIN_LOCKED, .id = dev, .pin = pin, .owner = s->owner[pin]);
  s->locked |= (1ULL << pin);
  s->owner[pin] = sys_device_caller();
  return NULL;
}

err_h sys_io_pins_unlock(sys_io_pins_t* s, uint8_t dev, uint8_t pin) {
  ERR_TRY(sys_io_pins_check(s, dev, pin));
  if (!is_locked(s, pin)) {
    return NULL;
  }
  ERR_CHECK(s->owner[pin] == sys_device_caller(), ERR_DEV_PIN_NOT_OWNED, .id = dev, .pin = pin,
            .owner = s->owner[pin]);
  s->locked &= ~(1ULL << pin);
  s->owner[pin] = SYS_DEVICE_ID_NONE;
  return NULL;
}
