#include "sys_io_claim.h"

#include "sys_device_driver.h"
#include "sys_device_event.h"

/* A device's own pins: take / drive in one order for every driver; the release is the registry's (a resource of the
 * owner). */

// The owner's resource: undoes what was done to the pin (unsubscribe, unlock, reset; not the reset with `keep`).
static err_h release(void* arg) {
  sys_io_claim_t* p = arg;
  err_h err = NULL;
  if (p->subscribed) {
    ERR_KEEP_FIRST(err, sys_event_unsubscribe(p->sub));
    p->subscribed = false;
  }
  if (p->locked) {
    ERR_KEEP_FIRST(err, SYS_IO_WRITE(p->pin, UNLOCK));
    p->locked = false;
  }
  if (p->used) {
    if (!p->keep) {
      ERR_KEEP_FIRST(err, sys_io_release(p->pin));
    }
    p->used = false;
  }
  return err;
}

// Declares the use of the pin's device and makes the pin a resource of `owner`, before anything is done to it.
static err_h take(uint8_t owner, sys_io_claim_t* p, sys_io_pin_t pin) {
  p->pin = pin;
  ERR_TRY(sys_device_use(owner, pin.device));
  return sys_device_res_add(owner, &p->res, release, p);
}

// Subscribes the pin's `event` to the owner's own op `op`, inline: in the publisher's task (an I2C read, no delay).
static err_h subscribe(uint8_t owner, sys_io_claim_t* p, uint32_t event, uint16_t op) {
  if (event == 0) {
    return NULL;
  }
  ERR_CHECK((op >> 8) == SYS_DEVICE_CONTRACT_OWN, ERR_EVENT_BAD_ARG);
  const sys_event_key_t key = {.tag = event, .source = p->pin.device, .channel = p->pin.pin};
  ERR_TRY(sys_device_event_subscribe_op(key, owner, op, &p->sub));
  p->subscribed = true;
  return NULL;
}

err_h sys_io_claim(uint8_t owner, sys_io_claim_t* p, sys_io_pin_t pin, sys_io_mode_e mode, uint32_t event,
                   uint16_t op) {
  p->pin = pin;  // SYS_IO_PIN_NONE stays NONE: the struct's pin is what the driver checks
  if (!sys_io_pin_valid(pin)) {
    return NULL;
  }
  ERR_TRY(take(owner, p, pin));
  ERR_TRY(sys_io_prepare(pin, mode));
  p->used = true;
  return subscribe(owner, p, event, op);
}

err_h sys_io_claim_output(uint8_t owner, sys_io_claim_t* p, sys_io_pin_t pin, bool level) {
  ERR_TRY(sys_io_claim(owner, p, pin, SYS_IO_MODE_OUTPUT_PUSH_PULL, 0, 0));
  if (!sys_io_pin_valid(pin)) {
    return NULL;
  }
  ERR_TRY(SYS_IO_WRITE(pin, LEVEL, .level = level));
  ERR_TRY(SYS_IO_WRITE(pin, LOCK));
  p->locked = true;
  return NULL;
}

err_h sys_io_claim_input(uint8_t owner, sys_io_claim_t* p, sys_io_pin_t pin, uint32_t event, uint16_t op,
                         uint8_t mode) {
  p->pin = pin;
  if (!sys_io_pin_valid(pin)) {
    return NULL;
  }
  ERR_TRY(take(owner, p, pin));
  ERR_TRY(sys_io_prepare_input(pin));
  p->used = true;
  ERR_TRY(subscribe(owner, p, event, op));
  ERR_TRY(SYS_IO_WRITE(pin, INTR, .cfg = {.mode = mode}));
  ERR_TRY(SYS_IO_WRITE(pin, LOCK));
  p->locked = true;
  return NULL;
}

err_h sys_io_claim_peripheral(uint8_t owner, sys_io_claim_t* p, sys_io_pin_t pin) {
  p->pin = pin;
  if (!sys_io_pin_valid(pin)) {
    return NULL;
  }
  ERR_TRY(take(owner, p, pin));
  ERR_TRY(SYS_IO_WRITE(pin, LOCK));
  p->locked = true;  // not `used`: the release unlocks it and leaves its mode to the peripheral
  return NULL;
}

err_h sys_io_claim_drive(const sys_io_claim_t* p, bool level) {
  return p->locked ? SYS_IO_WRITE(p->pin, LEVEL, .level = level) : NULL;  // the pin's device lets its owner write
}
