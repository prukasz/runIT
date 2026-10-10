#pragma once

#include <stdbool.h>
#include <stdint.h>
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
 * (only for modules that live forever: the VM, the power manager). Rules,
 * made remotely, are queued listeners that run an action.
 *
 * An event whose flags have SYS_EVENT_NOTIFY (events that matter to the user,
 * set in the contract's events table) also goes to the events connector at
 * publish time, before any listener runs: an action stopping the board
 * can't hide its cause.
 */

/* An event ID has the op layout: [contract u8][n u8] (SYS_DEVICE_CONTRACT_*). */
#define SYS_EVENT_ID(contract, n) SYS_DEVICE_OP(contract, n)

#define SYS_EVENT_ANY_SOURCE 0xFFFFu  // match: any source
#define SYS_EVENT_ANY_EVENT 0xFFFFu   // match: any event
#define SYS_EVENT_ANY_IN 0xFFu        // match: low byte of `event` = any event of that contract (n 0xFF is never used)
#define SYS_EVENT_ANY_CHANNEL 0xFFu   // match: any channel

#define SYS_EVENT_NOTIFY 0x01u  // flags: also sent on the events connector

/* One occurrence. Copied into the queue and passed as the ON_EVENT args, so packed and without pointers. */
typedef struct __attribute__((packed)) {
  uint16_t source;  // device ID
  uint16_t event;   // SYS_EVENT_ID(contract, n)
  uint8_t channel;  // pin / channel on that device, 0 when it has none
  uint8_t hops;     // republish depth (SYS_EVENT_CAUSED_BY)
  uint8_t flags;    // SYS_EVENT_NOTIFY
  int32_t value;    // level, mV, mA, ... per event
  uint32_t t_ms;    // set by sys_event_publish
} sys_event_t;

/* `.hops` of an event published because of `cause` (NULL = first hand), so a
   loop of republishes stops at CONFIG_SYS_EVENT_MAX_HOPS (ERR_EVENT_LOOP). */
#define SYS_EVENT_CAUSED_BY(cause) ((uint8_t)((cause) != NULL ? (cause)->hops + 1u : 0u))

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
 * @usage ERR_TRY(SYS_EVENT_PUBLISH(IO, FALLING, c->id, pin, level, cause));
 */
#define SYS_EVENT_PUBLISH(PFX, NAME, src, ch, val, cause)                                             \
  sys_event_publish(&(sys_event_t){.source = (src), .event = PFX##_EV_##NAME, .channel = (ch),         \
                                   .hops = SYS_EVENT_CAUSED_BY(cause), .flags = PFX##_EV_##NAME##_FLAGS, \
                                   .value = (val)})

/* ----------------------------------------------------------------------------
 * The EVENT contract (0x03xx): a device that listens to events answers ON_EVENT
 * ------------------------------------------------------------------------- */

// clang-format off
#define SYS_EVENT_CONTRACT(X)                                                          \
  X(ON_EVENT, 0x00, sys_event_t)  /* an event matched the device's subscription */
// clang-format on

#define SYS_EVENT__ID(name, n, args_t) EVENT_OP_##name = SYS_DEVICE_OP(SYS_DEVICE_CONTRACT_EVENT, n),
#define SYS_EVENT__ARGS(name, n, args_t) typedef args_t event_args_##name##_t;

enum { SYS_EVENT_CONTRACT(SYS_EVENT__ID) };
SYS_EVENT_CONTRACT(SYS_EVENT__ARGS)

#define EVENT_HANDLER(name) SYS_DEVICE_HANDLER(event_h_##name, event_args_##name##_t)
#define EVENT_ENTRY(name) {EVENT_OP_##name, event_h_##name}

/* ----------------------------------------------------------------------------
 * Errors (module ERR_MOD_SYS_EVENT)
 * ------------------------------------------------------------------------- */

// clang-format off
#define ERR_SYS_EVENT_MAP(X)                                                                                     \
  X(ERR_EVENT_LOOP,       ERR_TAG(ERR_MOD_SYS_EVENT, 1), ERR_SEV_MEDIUM, { uint16_t source; uint16_t event; uint8_t hops; }) \
  X(ERR_EVENT_QUEUE_FULL, ERR_TAG(ERR_MOD_SYS_EVENT, 2), ERR_SEV_LOW,    { uint16_t source; uint16_t event; uint32_t dropped; }) \
  X(ERR_EVENT_TABLE_FULL, ERR_TAG(ERR_MOD_SYS_EVENT, 3), ERR_SEV_MEDIUM, {})                                    \
  X(ERR_EVENT_NOT_FOUND,  ERR_TAG(ERR_MOD_SYS_EVENT, 4), ERR_SEV_MEDIUM, { uint8_t id; }) /* subscription handle or rule ID */ \
  X(ERR_EVENT_BAD_ARG,    ERR_TAG(ERR_MOD_SYS_EVENT, 5), ERR_SEV_HIGH,   {})                                    \
  X(ERR_EVENT_BAD_ID,     ERR_TAG(ERR_MOD_SYS_EVENT, 6), ERR_SEV_HIGH,   { uint8_t id; }) /* rule ID 0 or 0xFF */ \
  X(ERR_EVENT_NO_RUNNER,  ERR_TAG(ERR_MOD_SYS_EVENT, 7), ERR_SEV_MEDIUM, { uint8_t action; }) /* a rule fired, no action runner set */ \
  X(ERR_EVENT_DELIVERY,   ERR_TAG(ERR_MOD_SYS_EVENT, 8), ERR_SEV_NONE,   { uint16_t source; uint16_t event; uint8_t channel; uint8_t rule; uint16_t device; }) /* context: a listener failed */
// clang-format on

ERR_TAGS_DECLARE(ERR_SYS_EVENT_MAP)

/* ----------------------------------------------------------------------------
 * C API
 * ------------------------------------------------------------------------- */

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

/** @brief Runs an action (rules). The app sets it: sys_actions_request. */
typedef err_h (*sys_event_action_f)(uint8_t action_id, bool urgent);

/** @brief Starts the event task. Boot, after sys_connector_init (notify needs the events connector). */
MUST_USE err_h sys_event_init(void);

/** @brief Sets what runs a rule's action. Boot, by the app. */
void sys_event_set_action_runner(sys_event_action_f fn);

/**
 * @brief Adds a listener.
 * @param handle Its handle, for sys_event_unsubscribe().
 * @return ERR_EVENT_BAD_ARG with no target, ERR_EVENT_TABLE_FULL when all
 *         CONFIG_SYS_EVENT_MAX_SUBS are used (rules included).
 */
MUST_USE err_h sys_event_subscribe(const sys_event_sub_t* sub, uint8_t* handle);

/** @brief Removes a listener. It may still get an event already being delivered (a device target can't dangle). */
MUST_USE err_h sys_event_unsubscribe(uint8_t handle);

/**
 * @brief Publishes an event: sets t_ms, sends it to the events connector if
 * it has SYS_EVENT_NOTIFY, runs the inline listeners (table order), then
 * queues one copy if a queued listener matches. From an ISR everything is
 * deferred to the event task; an ISR drop is counted and reported by the task.
 * @return ERR_EVENT_LOOP when hops reached CONFIG_SYS_EVENT_MAX_HOPS,
 *         ERR_EVENT_QUEUE_FULL when the queued copy didn't fit. Listener
 *         failures are reported, not returned.
 */
MUST_USE err_h sys_event_publish(const sys_event_t* event);

/**
 * @brief Publishes from a background task (a driver's sampling task, a timer):
 * like an ISR event, the whole delivery (notify, inline listeners, queued ones)
 * runs later in the event task, never in the caller's. Use it from any task
 * that an op may wait for: an inline listener there could need the device lock
 * that op holds (SYS_DEVICE.MD "Concurrency"). t_ms is the time of the call.
 * @return ERR_EVENT_LOOP, ERR_EVENT_QUEUE_FULL (the event is dropped).
 */
MUST_USE err_h sys_event_post(const sys_event_t* event);

/* ----------------------------------------------------------------------------
 * Rules: remote listeners "event -> action" (sys_event_router)
 * ------------------------------------------------------------------------- */

#define SYS_EVENT_RULE_ALL 0xFFu      // unsubscribe: every rule
#define SYS_EVENT_RULE_URGENT 0x01u   // rule flags: the action runs on the urgent task

typedef struct {
  uint8_t id;  // 1..254, chosen by the client
  uint16_t source;
  uint16_t event;
  uint8_t channel;
  uint8_t action;  // action ID (sys_actions)
  uint8_t flags;   // SYS_EVENT_RULE_URGENT
} sys_event_rule_t;

/** @brief Adds rule `rule->id`, replacing a rule with that ID. Always queued. */
MUST_USE err_h sys_event_rule_set(const sys_event_rule_t* rule);

/** @brief Removes rule `id`; SYS_EVENT_RULE_ALL removes every rule (none = no error). */
MUST_USE err_h sys_event_rule_remove(uint8_t id);

/**
 * @brief Rules with ID >= `start`, by ascending ID: calls `fn` until it returns false.
 * @param total Number of rules; may be NULL.
 */
MUST_USE err_h sys_event_rule_list(uint8_t start, bool (*fn)(const sys_event_rule_t* rule, void* ctx), void* ctx,
                                   uint8_t* total);

#ifdef __cplusplus
}
#endif
