#include "sys_device_timer.h"

/* The callback's ctx is [op u16][gen u8][owner u8] as a number, not a pointer: the timers' task holds nothing of a
 * device that can go away, and the generation tells the owner from a device created later under its ID. */
_Static_assert(sizeof(uintptr_t) >= sizeof(uint32_t), "a timer's ctx holds [op u16][gen u8][owner u8]");

#define CTX_PACK(owner, gen, op) ((void*)(uintptr_t)(((uint32_t)(op) << 16) | ((uint32_t)(gen) << 8) | (owner)))
#define CTX_OWNER(ctx)           ((uint8_t)((uintptr_t)(ctx) & 0xFFu))
#define CTX_GEN(ctx)             ((uint8_t)(((uintptr_t)(ctx) >> 8) & 0xFFu))
#define CTX_OP(ctx)              ((uint16_t)((uintptr_t)(ctx) >> 16))

// The timers' task: the owner's step (skipped when the owner is busy, suspended or gone).
static void on_expiry(void* ctx) {
  err_report(sys_device_step(CTX_OWNER(ctx), CTX_GEN(ctx), CTX_OP(ctx)));
}

// The owner's resource: stopped before the registry frees the struct the timer is in.
static err_h release(void* arg) {
  sys_device_timer_t* t = arg;
  board_timer_stop(&t->timer);
  return NULL;
}

err_h sys_device_timer_init(uint8_t owner, sys_device_timer_t* t, uint16_t op) {
  ERR_CHECK((op >> 8) == SYS_DEVICE_CONTRACT_OWN, ERR_DEV_BAD_ARG);  // a step is an own op (sys_device_step)
  board_timer_init(&t->timer, on_expiry, CTX_PACK(owner, sys_device_gen(owner), op));
  return sys_device_res_add(owner, &t->res, release, t);
}
