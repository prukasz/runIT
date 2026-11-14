#include "sys_device_timer.h"

/* The callback's ctx packs sys_device_target_t ([op u16][gen u8][id u8]) into void*: the timers' task holds
 * nothing of a device that can go away, and the generation tells the owner from a device created later under its ID. */

// The timers' task: the owner's step (skipped when the owner is busy, suspended or gone).
static void on_expiry(void* ctx) {
  const sys_device_target_t t = sys_device_target_unpack(ctx);
  err_report(sys_device_step(t.id, t.gen, t.op));
}

// The owner's resource: stopped before the registry frees the struct the timer is in.
static err_h release(void* arg) {
  sys_device_timer_t* t = arg;
  board_timer_stop(&t->timer);
  return NULL;
}

err_h sys_device_timer_init(uint8_t owner, sys_device_timer_t* t, uint16_t op) {
  ERR_CHECK((op >> 8) == SYS_DEVICE_CONTRACT_OWN, ERR_DEV_BAD_ARG);  // a step is an own op (sys_device_step)
  board_timer_init(&t->timer, on_expiry,
                   sys_device_target_pack((sys_device_target_t){.id = owner, .gen = sys_device_gen(owner), .op = op}));
  return sys_device_res_add(owner, &t->res, release, t);
}
