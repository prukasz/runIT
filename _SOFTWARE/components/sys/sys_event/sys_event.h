#pragma once

#include "errors.h"
#include "sys_device.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Events: one publisher, any number of listeners (SYS_EVENT.MD).
 *
 * A source (a device, the power manager) publishes what happened and doesn't
 * know who listens. Listeners live in one static table and are matched by
 * source / event / channel, each exact or "any".
 *
 *  - Inline listener: called in the publisher's task before
 *    sys_event_publish() returns. It may block on I/O (I2C), never on a delay.
 *  - Queued listener: called later by the event task.
 *
 * A listener's target is a device (its EVENT contract op ON_EVENT, through
 * sys_device_call: a removed device just answers NOT_FOUND) or a C function
 * (only for modules that live forever: the VM, the power manager).
 *
 * An event goes to the events connector (the app) at publish time, before any
 * listener runs (an action stopping the board can't hide its cause), when its
 * flags have SYS_EVENT_NOTIFY (events that matter to the user, set in the
 * contract's events table). Nothing else reaches the app on its own: a program
 * that wants another event there sends it (VM).
 *
 * There is no loop guard: a listener that republishes (a device on its INT pin)
 * subscribes to one exact source and channel, and that pin's device exists
 * before it (sys_device_use), so a chain of republishes can't close into a cycle.
 */

/* ----------------------------------------------------------------------------
 * The event
 * ------------------------------------------------------------------------- */

/* An event ID has the op layout: [contract u8][n u8] (SYS_DEVICE_CONTRACT_*). */
#define SYS_EVENT_ID(contract, n) SYS_DEVICE_OP(contract, n)

/* Frame on the events stream: [ROUTER_KEY_EVENT 0x07][SYS_EVENT_OP_NOTIFY][sys_event_t]. The command shape (like the
 * VM telemetry), so the app reads it with the decoder it has for commands. */
#define SYS_EVENT_OP_NOTIFY 0x01u

/* Match values of a subscription. */
#define SYS_EVENT_ANY_SOURCE 0xFFFFu  // any source
#define SYS_EVENT_ANY_EVENT 0xFFFFu   // any event
#define SYS_EVENT_ANY_IN 0xFFu        // low byte of `event`: any event of that contract (n 0xFF is never used)
#define SYS_EVENT_ANY_CHANNEL 0xFFu   // any channel

/* flags */
#define SYS_EVENT_NOTIFY 0x01u  // sent on the events connector (the only events the app gets)
#define SYS_EVENT_DEFER 0x02u   // set by the publisher, not by a table: everything is delivered in the event task
                                // (the task clears it first: a listener or the stream never sees it)

/* One occurrence. Copied into the queue, passed as the ON_EVENT args and sent as the events stream record, so packed
   and without pointers. */
typedef struct __attribute__((packed)) {
  uint16_t source;  // device ID
  uint16_t event;   // SYS_EVENT_ID(contract, n)
  uint8_t channel;  // pin / channel on that device, 0 when it has none
  uint8_t flags;    // SYS_EVENT_NOTIFY | SYS_EVENT_DEFER
  int32_t value;    // level, mV, mA, ... per event
  uint32_t t_ms;    // set by sys_event_publish
} sys_event_t;

/* ----------------------------------------------------------------------------
 * Events tables
 *
 * A contract lists its events once, next to its ops: X(NAME, n, flags), e.g.
 *   #define SYS_IO_EVENTS(X) X(RISING, 0x00, 0) X(FALLING, 0x01, 0)
 *   #define SYS_IO__EV(name, n, flags) IO_EV_##name = SYS_EVENT_ID(SYS_DEVICE_CONTRACT_IO, n), \
 *                                      IO_EV_##name##_FLAGS = (flags),
 *   enum { SYS_IO_EVENTS(SYS_IO__EV) };
 * flags = SYS_EVENT_NOTIFY for events that matter to the user (an over-current,
 * a fault), 0 for the ones that only drive code (an edge, an alert pin).
 * Publishing with SYS_EVENT_PUBLISH takes the flags from the table.
 * ------------------------------------------------------------------------- */

/**
 * @brief Publishes event NAME of the table with prefix PFX (IO_EV_NAME, IO_EV_NAME_FLAGS).
 * @usage ERR_TRY(SYS_EVENT_PUBLISH(IO, FALLING, c->id, pin, level));
 */
#define SYS_EVENT_PUBLISH(PFX, NAME, src, ch, val)                                             \
  sys_event_publish(&(sys_event_t){.source = (src), .event = PFX##_EV_##NAME, .channel = (ch), \
                                   .flags = PFX##_EV_##NAME##_FLAGS, .value = (val)})

/* ----------------------------------------------------------------------------
 * The EVENT contract (0x03xx): a device that listens to events answers ON_EVENT
 * ------------------------------------------------------------------------- */

// The contract's only op (no table: one op doesn't need the X-macro). Args = sys_event_t.
#define EVENT_OP_ON_EVENT SYS_DEVICE_OP(SYS_DEVICE_CONTRACT_EVENT, 0x00)

#define EVENT_HANDLER(name) SYS_DEVICE_HANDLER(event_h_##name, sys_event_t)
#define EVENT_ENTRY(name) {EVENT_OP_##name, event_h_##name}

/* ----------------------------------------------------------------------------
 * Errors (module ERR_MOD_SYS_EVENT). Tags 1 (LOOP), 6 (BAD_ID) and 7 (NO_RUNNER)
 * are retired: hops, remote watches and rules are gone (2026-10-11).
 * ------------------------------------------------------------------------- */

// clang-format off
#define ERR_SYS_EVENT_MAP(X)                                                                                     \
  X(ERR_EVENT_QUEUE_FULL, ERR_TAG(ERR_MOD_SYS_EVENT, 2), ERR_SEV_LOW,    { uint16_t source; uint16_t event; uint32_t dropped; }) \
  X(ERR_EVENT_TABLE_FULL, ERR_TAG(ERR_MOD_SYS_EVENT, 3), ERR_SEV_MEDIUM, {})                                    \
  X(ERR_EVENT_NOT_FOUND,  ERR_TAG(ERR_MOD_SYS_EVENT, 4), ERR_SEV_MEDIUM, { uint8_t id; }) /* subscription handle */ \
  X(ERR_EVENT_BAD_ARG,    ERR_TAG(ERR_MOD_SYS_EVENT, 5), ERR_SEV_HIGH,   {})                                    \
  X(ERR_EVENT_DELIVERY,   ERR_TAG(ERR_MOD_SYS_EVENT, 8), ERR_SEV_NONE,   { uint16_t source; uint16_t event; uint8_t channel; uint16_t device; }) /* context: a listener failed */
// clang-format on

ERR_TAGS_DECLARE(ERR_SYS_EVENT_MAP)

/* ----------------------------------------------------------------------------
 * C API
 * ------------------------------------------------------------------------- */

/** @brief Starts the event task. Boot, after sys_connector_init (notify needs the events connector). */
MUST_USE err_h sys_event_init(void);

/**
 * @brief Listener callback (target FN).
 * @param event Borrowed; copy it to keep it.
 * @return An error is reported by the dispatcher; other listeners still run.
 */
typedef err_h (*sys_event_handler_f)(const sys_event_t* event, void* ctx);

typedef struct {
  /* Match: exact or SYS_EVENT_ANY_* / SYS_EVENT_ANY_IN. */
  uint16_t source;
  uint16_t event;
  uint8_t channel;
  bool inline_call;  // in the publisher's task (else: the event task)
  /* Target: a device (its ON_EVENT), or fn when device is SYS_DEVICE_ID_NONE. */
  uint16_t device;
  sys_event_handler_f fn;
  void* ctx;
} sys_event_sub_t;

/**
 * @brief Adds a listener.
 * @param handle Its handle, for sys_event_unsubscribe().
 * @return ERR_EVENT_BAD_ARG with no target, ERR_EVENT_TABLE_FULL when all
 *         CONFIG_SYS_EVENT_MAX_SUBS are used.
 */
MUST_USE err_h sys_event_subscribe(const sys_event_sub_t* sub, uint8_t* handle);

/** @brief Removes a listener. It may still get an event already being delivered (a device target can't dangle). */
MUST_USE err_h sys_event_unsubscribe(uint8_t handle);

/**
 * @brief Publishes an event: sets t_ms, sends it to the events connector if it has SYS_EVENT_NOTIFY, runs the inline
 * listeners (table order), then queues one copy if a queued listener matches.
 *
 * From an ISR, or with SYS_EVENT_DEFER in the flags, all of it is deferred to the event task. A task that an op may
 * wait for must set DEFER: an inline listener run there could need the device lock that op holds (SYS_DEVICE.MD
 * "Concurrency"). An ISR drop is counted and reported by the task.
 * @return ERR_EVENT_QUEUE_FULL when the queued (or deferred) copy didn't fit. Listener failures are reported, not
 *         returned.
 */
MUST_USE err_h sys_event_publish(const sys_event_t* event);

/**
 * @brief The match rule of a subscription: @p source, @p event and @p channel against an event, each exact or
 * SYS_EVENT_ANY_* / SYS_EVENT_ANY_IN. For code that filters events itself (the VM's ON_EVENT block).
 */
bool sys_event_matches(uint16_t source, uint16_t event, uint8_t channel, const sys_event_t* e);

#ifdef __cplusplus
}
#endif
