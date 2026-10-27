#pragma once

#include "errors.h"
#include "sys_device.h"
#include "sys_event_errors.h"

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
//#ref-defines event_op @alias Event frames @prefix SYS_EVENT_OP_
#define SYS_EVENT_OP_NOTIFY 0x01u  //@alias Notify @description an event on the events stream: [ROUTER_KEY_EVENT][NOTIFY][sys_event_t]

/* Match values of a subscription. */
#define SYS_EVENT_ANY_SOURCE  0xFFFFu  // any source
#define SYS_EVENT_ANY_EVENT   0xFFFFu   // any event
#define SYS_EVENT_ANY_IN      0xFFu        // low byte of `event`: any event of that contract (n 0xFF is never used)
#define SYS_EVENT_ANY_CHANNEL 0xFFu   // any channel

/* One occurrence. Copied into the queue, passed as the ON_EVENT args and sent as the events stream record, so packed
   and without pointers. */
typedef struct __attribute__((packed)) {
  uint16_t source;  // device ID
  uint16_t event;   // SYS_EVENT_ID(contract, n)
  uint8_t channel;  // pin / channel on that device, 0 when it has none
  uint8_t flags;    // the events table's: SYS_EVENT_NOTIFY (sys_device.h) or 0
  int32_t value;    // level, mV, mA, ... per event
  uint64_t t_ms;    // set by sys_event_publish: the board's clock in ms, UTC once a client set it (board_time_now_ms)
} sys_event_t;

/* ----------------------------------------------------------------------------
 * Events tables
 *
 * A contract lists its events once, next to its ops: the table P_EVENTS of rows X(P, NAME, n, flags), and
 * SYS_EVENTS_DECLARE(P) (sys_device.h) after its SYS_DEVICE_CONTRACT_DECLARE (the ID from P_CONTRACT), e.g.
 *   #define IO_EVENTS(X, P) X(P, RISING, 0x00, 0) X(P, FALLING, 0x01, 0)
 *   SYS_EVENTS_DECLARE(IO)  // IO_EV_RISING = 0x0100, IO_EV_RISING_FLAGS = 0, ...
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

// The contract's only op, a write (OP_WRITE(EVENT, ON_EVENT), OP_ENTRY_W(EVENT, ON_EVENT)). Args = sys_event_t.
#define EVENT_OPERATIONS(X, P) X(P, ON_EVENT, 0x00, sys_device_none_t, sys_event_t)

SYS_DEVICE_CONTRACT_DECLARE(EVENT, SYS_DEVICE_CONTRACT_EVENT)

/* ----------------------------------------------------------------------------
 * C API
 * ------------------------------------------------------------------------- */

/** @brief Starts the event task. Boot, after sys_data_connector_init (notify needs the events connector). */
MUST_USE err_h sys_event_init(void);

/**
 * @brief A function listener.
 * @param event Borrowed; copy it to keep it.
 * @return An error is reported by the dispatcher; other listeners still run.
 */
typedef err_h (*sys_event_handler_f)(const sys_event_t* event, void* ctx);

/* A subscription matches `source`, `event` and `channel`, each exact or SYS_EVENT_ANY_* / SYS_EVENT_ANY_IN. Where the
 * listener runs follows from what it is, so a listener can't run where it would deadlock:
 *  - a device (its ON_EVENT) runs inline, in the publisher's task before publish returns: the registry lock is
 *    recursive, so it is safe in any publisher, a device op included. It may block on I/O (I2C), never on a delay.
 *  - a function runs in the event task: a module with locks of its own (the power manager) would take them inside a
 *    device op, against the lock order. Only for modules that live forever (the VM, the power manager). */

/**
 * @brief Adds device `device` as a listener: its ON_EVENT (sys_device_write) gets the matching events, inline.
 * @param handle Its handle, for sys_event_unsubscribe().
 * @return ERR_EVENT_BAD_ARG for SYS_DEVICE_ID_NONE, ERR_EVENT_TABLE_FULL when all CONFIG_SYS_EVENT_MAX_SUBS are used.
 */
MUST_USE err_h sys_event_subscribe_device(uint16_t source, uint16_t event, uint8_t channel, uint16_t device,
                                          uint8_t* handle);

/**
 * @brief Adds `fn(event, ctx)` as a listener, run by the event task.
 * @return ERR_EVENT_BAD_ARG for a NULL fn, ERR_EVENT_TABLE_FULL when all CONFIG_SYS_EVENT_MAX_SUBS are used.
 */
MUST_USE err_h sys_event_subscribe_fn(uint16_t source, uint16_t event, uint8_t channel, sys_event_handler_f fn,
                                      void* ctx, uint8_t* handle);

/** @brief Removes a listener. It may still get an event already being delivered (a device target can't dangle). */
MUST_USE err_h sys_event_unsubscribe(uint8_t handle);

/**
 * @brief Publishes an event: sets t_ms, sends it to the events connector if it has SYS_EVENT_NOTIFY, runs the device
 * listeners (table order), then queues one copy for the function listeners if one matches. From an ISR all of it is
 * deferred to the event task; a drop there is counted and reported by the task.
 * @return ERR_EVENT_QUEUE_FULL when the queued copy didn't fit. Listener failures are reported, not returned.
 */
MUST_USE err_h sys_event_publish(const sys_event_t* event);

/**
 * @brief sys_event_publish with all of it in the event task. For a task an op may wait for (the ESP32-S3 GPIO's ADC
 * task): a device listener run there could need the registry lock that op holds (SYS_DEVICE.MD "Concurrency").
 * @return ERR_EVENT_QUEUE_FULL when it didn't fit.
 */
MUST_USE err_h sys_event_publish_deferred(const sys_event_t* event);

/**
 * @brief The match rule of a subscription: @p source, @p event and @p channel against an event, each exact or
 * SYS_EVENT_ANY_* / SYS_EVENT_ANY_IN. For code that filters events itself (the VM's ON_EVENT block).
 */
bool sys_event_matches(uint16_t source, uint16_t event, uint8_t channel, const sys_event_t* e);

#ifdef __cplusplus
}
#endif
