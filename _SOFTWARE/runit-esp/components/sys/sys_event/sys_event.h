#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "errors.h"
#include "sdkconfig.h"
#include "sys_event_errors.h"
#include "sys_event_modules.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Events: one publisher, any number of listeners (SYS_EVENT.MD). The layer below the device registry: it knows
 * no device, only numbers (a tag, a source, a channel) and C functions.
 *
 * An event is a tag (what happened, declared like an error: SYS_EVENT.MD "Events tables"), a source and a channel (who
 * and where: a device and its pin, a screen and its object, ... whatever the module defines), the time, the flags and
 * the tag's payload.
 * A source publishes what happened and doesn't know who listens. Listeners live in one static table and are matched by
 * source / tag / channel, each exact or "any".
 *
 *  - Inline listener: called in the publisher's task before
 *    sys_event_publish() returns. It may block on I/O (I2C), never on a delay.
 *  - Queued listener: called later by the task of one lane, for that lane's events (each events table picks its
 *    lane: a ring buffer and a task of its own, so one lane never waits behind another).
 *
 * A listener is a C function and a context. A listener that outlives what its context points to (a device that can be
 * removed) takes an ID as the context, not a pointer, and looks it up when it is called: sys_device_event_subscribe
 * does it for a device.
 *
 * The bus knows no receiver of its own: what goes to the app (or to another board) is a listener like any other. The
 * events that matter to the user carry SYS_EVENT_NOTIFY in their flags (set in the events table), so that listener
 * takes them with the filter SYS_EVENT_ANY_NOTIFY, without being told which; it subscribes with SYS_EVENT_RUN_FIRST, so
 * it runs before every other listener (an action stopping the board can't hide its cause).
 *
 * There is no loop guard: a listener that republishes (a device on its INT pin)
 * subscribes to one exact source and channel, and that pin's device exists
 * before it (sys_device_use), so a chain of republishes is finite and can't close into a cycle.
 */

/* ----------------------------------------------------------------------------
 * The event
 * ------------------------------------------------------------------------- */

/* A tag is [module u16][code u16]: the module is the contract of the device that publishes (its ID, 0x01..0x1F), a
 * module (its MODULE_ID_<NAME>, module_id.h) or a device type's own events (SYS_EVENT_MODULE_TYPE(type ID)); module
 * 0xFFFF is the filters' (sys_event_modules.h). The code is 1..0xFFFE (0 is not used, so tag 0 is "no event"; 0xFFFF
 * is SYS_EVENT_ANY_CODE). An events table gives the module once for all its rows (SYS_EVENTS_DECLARE). */
#define EVT_TAG(module, code) ((uint32_t)((((uint32_t)(module)) << 16) | ((uint32_t)(code) & 0xFFFFu)))
#define EVT_MODULE(tag)       ((uint16_t)((tag) >> 16))
#define EVT_CODE(tag)         ((uint16_t)((tag) & 0xFFFFu))

/* The lanes (SYS_EVENT.MD "Lanes"): each a ring buffer and a task of its own (CONFIG_SYS_EVENT_<LANE>_QUEUE_BYTES,
 * _TASK_PRIO, _TASK_STACK), so the deferred events and the queued listeners of one lane never wait behind another's.
 * An events table picks its lane (SYS_EVENTS_DECLARE), a queued listener runs in one (SYS_EVENT_RUN_QUEUED_<LANE>).
 * A lane is a row here and its three options in the Kconfig. */
#define SYS_EVENT_LANES(X) \
  X(SYS)                   \
  X(DEVICE)

#define SYS_EVENT__LANE_ENUM(L) SYS_EVENT_LANE_##L,
typedef enum { SYS_EVENT_LANES(SYS_EVENT__LANE_ENUM) SYS_EVENT_LANE_COUNT } sys_event_lane_e;

/* Flags of a publish (sys_event_publish). The event's own ones (SYS_EVENT_FLAGS_EVENT) come from its events table and
 * travel in sys_event_t.flags; the others say only how this publish is delivered and are never in an event. */
#define SYS_EVENT_NOTIFY          0x0001u  // event: it matters to the user (what the app gets); from the events table
#define SYS_EVENT_FLAGS_LANE      0x00F0u  // event: its lane (SYS_EVENT_FLAG_LANE); from the events table
#define SYS_EVENT_DEFER           0x8000u  // publish: all of it in the lane's task; never in an event
#define SYS_EVENT_FLAGS_EVENT     0x00FFu  // the bits of a publish's flags that go into the event
#define SYS_EVENT_FLAG_LANE(lane) ((uint16_t)((lane) << 4))
#define SYS_EVENT_LANE_OF(flags)  ((uint32_t)(((flags) & SYS_EVENT_FLAGS_LANE) >> 4))

_Static_assert(SYS_EVENT_LANE_COUNT <= 16, "a lane is 4 bits of the flags");

/* "Any" values of a key used as a filter. */
#define SYS_EVENT_ANY_SOURCE  0xFFFFu      // any source
#define SYS_EVENT_ANY_CHANNEL 0xFFFFu      // any channel
#define SYS_EVENT_ANY_CODE    0xFFFFu      // code of a tag: any event of that module (the code is never used)
#define SYS_EVENT_ANY_TAG     0xFFFFFFFFu  // any event
#define SYS_EVENT_ANY_NOTIFY  0xFFFF0000u  // any event with SYS_EVENT_NOTIFY in its flags (module 0xFFFF, code 0)

/* The most bytes of a tag's payload. */
#define SYS_EVENT_DATA_MAX CONFIG_SYS_EVENT_DATA_MAX

/** @brief What happened, who and where: an event's key. As a filter (a subscription, the VM's ON_EVENT block, as
 * commands and descriptors carry it) each field is exact or "any". */
typedef struct __attribute__((packed)) {
  uint32_t tag;      /**< EVT_TAG(module, code); filter: or EVT_TAG(module, SYS_EVENT_ANY_CODE), SYS_EVENT_ANY_TAG,
                          SYS_EVENT_ANY_NOTIFY */
  uint16_t source;   /**< The device, the screen, ...: what the module defines; filter: or SYS_EVENT_ANY_SOURCE */
  uint16_t channel;  /**< Pin / channel / object on that source, 0 when it has none; filter: or SYS_EVENT_ANY_CHANNEL */
} sys_event_key_t;

_Static_assert(sizeof(sys_event_key_t) == 8, "event key");

/** @brief One occurrence: the key, the time, the flags, the payload's size and, right after them, the payload (the
 * evt_data_<NAME>_t of its table row). Packed and without pointers: it is copied into the queue, passed to the
 * listeners and sent as it is (to the app, to another board). Every field lies at a multiple of its size up to 4 B
 * and the payload at 20, so an event at a 4 B boundary is read without unaligned access; sys_event keeps it there.
 * Layout: [tag u32][source u16][channel u16][t_ms u64][flags u16][len u16][payload] */
typedef struct __attribute__((packed)) {
  sys_event_key_t key;  /**< What, who, where */
  uint64_t t_ms;        /**< Set by sys_event_publish: the board's clock in ms, the stamp of an error record
                             (board_time_now_ms: UTC once a client set it, else the time since the start) */
  uint16_t flags;       /**< SYS_EVENT_NOTIFY and the lane (SYS_EVENT_FLAG_LANE): the events table's */
  uint16_t len;         /**< Payload bytes after the header, at most SYS_EVENT_DATA_MAX */
  uint8_t data[];       /**< The payload */
} sys_event_t;

_Static_assert(sizeof(sys_event_t) == 20, "event header");
_Static_assert(SYS_EVENT_DATA_MAX <= 0xFFFF, "an event's len is u16");

/**
 * @brief Whether an event passes a filter: the rule of a subscription, for code that filters events itself (the VM's
 * ON_EVENT block). Any context, an ISR too.
 * @param[in] filter A key, each field exact or "any".
 * @param[in] e      The event.
 * @return true when it passes.
 */
bool sys_event_match(const sys_event_key_t* filter, const sys_event_t* e);

/* ----------------------------------------------------------------------------
 * Events tables
 *
 * A module lists its events once, in the format of every table (cmd_table.h): P_EVENTS(X, P) of rows
 * X(P, NAME, code, flags, { payload fields }). The module of the tags and their lane come from the table, never from a
 * row, so no row can name another module's or go another way than its module's:
 *   SYS_EVENTS_DECLARE(P, LANE) the module's number, MODULE_ID_<P> (module_id.h): a module's, a contract's ID
 *   SYS_DEVICE_OWN_EVENTS(P)    a device type's own events, SYS_EVENT_MODULE_TYPE(P_TYPE), lane DEVICE
 *                               (sys_device_driver.h)
 * Both give P_EV_<NAME> = EVT_TAG(module, code), its flags P_EV_<NAME>_FLAGS (the row's and the lane), its payload
 * evt_data_P_EV_<NAME>_t, the module P_EV_MODULE and the lane P_EV_LANE:
 *   #define IO_EVENTS(X, P)                     \
 *     X(P, RISING, 0x01, 0,                {}) \
 *     X(P, FAULT,  0x02, SYS_EVENT_NOTIFY, { uint16_t mA; })
 *   SYS_EVENTS_DECLARE(IO, DEVICE)  // IO_EV_RISING = EVT_TAG(MODULE_ID_IO, 0x01), IO_EV_RISING_FLAGS, ...
 * A row's flags are SYS_EVENT_NOTIFY for events that matter to the user (an over-current, a fault), 0 for the ones that
 * only drive code (an edge, an alert pin); they go into the event's flags with the lane. Publishing with
 * SYS_EVENT_PUBLISH takes the tag, the flags and the payload size from the table.
 * ------------------------------------------------------------------------- */

#define SYS_EVENT__ENUM(P, name, code, flags, ...) \
  P##_EV_##name = EVT_TAG(P##_EV_MODULE, code), P##_EV_##name##_FLAGS = (flags) | SYS_EVENT_FLAG_LANE(P##_EV_LANE),
#define SYS_EVENT__TYPE(P, name, code, flags, ...)                                                             \
  typedef struct __attribute__((packed)) __VA_ARGS__ evt_data_##P##_EV_##name##_t;                            \
  _Static_assert(sizeof(evt_data_##P##_EV_##name##_t) <= SYS_EVENT_DATA_MAX, #P "_EV_" #name ": payload too big"); \
  _Static_assert((code) >= 1u && (code) <= 0xFFFEu, #P "_EV_" #name ": the code is not 1..0xFFFE");           \
  _Static_assert(((flags) & ~SYS_EVENT_NOTIFY) == 0u, #P "_EV_" #name ": a row's flag is NOTIFY only");

/* The table P_EVENTS under module `module`, in lane `lane` (SYS_EVENTS_DECLARE, SYS_DEVICE_OWN_EVENTS). */
#define SYS_EVENT__TABLE(P, module, lane)                                                                 \
  _Static_assert((module) >= 0x0001u && (module) <= 0xFFFEu, #P ": an event's module is 0x0001..0xFFFE"); \
  enum { P##_EV_MODULE = (module), P##_EV_LANE = SYS_EVENT_LANE_##lane };                                 \
  enum { P##_EVENTS(SYS_EVENT__ENUM, P) };                                                                \
  P##_EVENTS(SYS_EVENT__TYPE, P)

/**
 * @brief The events of table P_EVENTS(X, P) under the module's number `id` (its MODULE_ID_<NAME>; a contract's
 * events: its ID), in the lane LANE (a row of SYS_EVENT_LANES). After the module's <module>_id.h.
 * @usage SYS_EVENTS_DECLARE(PWRM, MODULE_ID_PWRM, SYS)  // PWRM_EV_BATTERY_LOW = EVT_TAG(MODULE_ID_PWRM, 0x02), ...
 */
#define SYS_EVENTS_DECLARE(P, id, LANE) SYS_EVENT__TABLE(P, id, LANE)

/* ----------------------------------------------------------------------------
 * C API
 * ------------------------------------------------------------------------- */

/**
 * @brief A listener function.
 * @param event Borrowed, at a 4 B boundary; its payload is event->len bytes. Copy it (sizeof + len) to keep it.
 * @param ctx   What the subscription was made with
 * @return An error is reported by the dispatcher; other listeners still run.
 */
typedef err_h (*sys_event_handler_f)(const sys_event_t* event, void* ctx);

#define SYS_EVENT__RUN_QUEUED(L) SYS_EVENT_RUN_QUEUED_##L,

/**
 * @brief Where a listener runs. The inline listeners run before the queued ones, each kind in the order of the table.
 */
typedef enum {
  /** In the publisher's task before publish returns, so a chain of listeners (a pin of the ESP -> an expander -> a
   * regulator) runs in one call. It may block on I/O (I2C), never on a delay, and it must not take a lock a publisher
   * may hold: no lock is recursive. A publisher picks inline or SYS_EVENT_DEFER by what its task can bear. A task
   * that publishes with a lock a listener may take holds its events (sys_event_hold) and lets them go after the lock:
   * the device registry does it around every call, so an event published inside a device op reaches its inline
   * listeners in the same task once the task's outermost sys_device_call is over, not while the op runs. */
  SYS_EVENT_RUN_INLINE,
  /** Inline, before every other listener: the table's first slot, kept for one. The one that sends the events to the
   * app (SYS_EVENT_ANY_NOTIFY), so an action stopping the board can't hide its cause, whatever subscribed before it. */
  SYS_EVENT_RUN_FIRST,
  /** SYS_EVENT_RUN_QUEUED_<LANE>: in the task of that lane, for the events of that lane only, so it never runs in two
   * tasks at once. For a module with locks of its own (the power manager) that would take them inside a device op,
   * against the lock order. A filter that spans lanes is subscribed once per lane. */
  SYS_EVENT_LANES(SYS_EVENT__RUN_QUEUED)
} sys_event_run_e;

/* SYS_EVENT_RUN_QUEUED_<LANE> of lane `lane` (a sys_event_lane_e): for a filter subscribed in a loop over the lanes. */
#define SYS_EVENT_RUN_QUEUED_IN(lane) ((sys_event_run_e)((uint32_t)SYS_EVENT_RUN_FIRST + 1u + (uint32_t)(lane)))

/**
 * @brief Adds `fn(event, ctx)` as a listener of the events that pass `filter`.
 * @param[in]  filter Which events: a key, each field exact or SYS_EVENT_ANY_*
 * @param[in]  fn     The listener function
 * @param[in]  ctx    Passed to fn; it must stay valid until sys_event_unsubscribe() and a delivery in flight are over,
 *                    so what can go away is passed as an ID
 * @param[in]  run    Where it runs
 * @param[out] handle Its handle, for sys_event_unsubscribe(): never 0, and not the handle of any listener before it
 * @return ERR_BAD_ARG for a NULL fn or handle or an unknown run, ERR_FULL when all
 *         CONFIG_SYS_EVENT_MAX_SUBS are used
 *         (for SYS_EVENT_RUN_FIRST: when the first slot has its listener).
 * @usage ERR_TRY(sys_event_subscribe((sys_event_key_t){.tag = IO_EV_FALLING, .source = id, .channel = 21}, on_edge,
 *                                    NULL, SYS_EVENT_RUN_INLINE, &h));
 */
MUST_USE err_h sys_event_subscribe(sys_event_key_t filter, sys_event_handler_f fn, void* ctx, sys_event_run_e run,
                                   uint16_t* handle);

/**
 * @brief Removes a listener. It may still get an event already being delivered (so its ctx must not dangle).
 * @param[in] handle The listener's handle
 * @return ERR_EVENT_NOT_FOUND{handle} for 0, a handle never given, or one of a listener removed already (also when
 *         another listener has its slot now: that one stays)
 */
MUST_USE err_h sys_event_unsubscribe(uint16_t handle);

/**
 * @brief Publishes an event, the one way in: stamps t_ms, runs the inline listeners (table order), then queues one
 * copy into its lane for the queued listeners of the lane if one matches. With SYS_EVENT_DEFER all of it runs later in
 * the lane's task: for a task an op may wait for (the ESP32-S3 GPIO's ADC task, sys_device_remote's link, the screen's task),
 * where an inline listener could need a lock that op holds. From an ISR it is always deferred (the clock is read there
 * by the lane's task, at the moment of the ISR); a drop there is counted and reported by that task. Use
 * SYS_EVENT_PUBLISH / SYS_EVENT_PUBLISH_DEFER; a module that passes on an event it did not declare (sys_device_remote) calls
 * this with the event's flags, its lane with them.
 * @param[in] key   What, who and where
 * @param[in] flags The event's flags (SYS_EVENT_NOTIFY and the lane, from its table) and how to deliver it
 *                  (SYS_EVENT_DEFER)
 * @param[in] data  The payload, NULL when `len` is 0
 * @param[in] len   Payload bytes, at most SYS_EVENT_DATA_MAX
 * While the task holds its events (sys_event_hold) a publish without SYS_EVENT_DEFER is kept in the hold and delivered
 * by sys_event_release(); one that does not fit there is deferred to the lane's task.
 * @return ERR_BAD_ARG for a payload that is too long or an unknown lane, ERR_EVENT_QUEUE_FULL when the queued
 *         copy didn't fit its lane.
 *         Listener failures are reported, not returned.
 */
MUST_USE err_h sys_event_publish(sys_event_key_t key, uint16_t flags, const void* data, uint32_t len);

/** @brief The events a task holds back: set up by sys_event_hold(); its fields are the module's. */
typedef struct {
  uint8_t* buf;   /**< The caller's buffer; NULL: this hold keeps nothing (an outer one of the task does) */
  uint32_t cap;   /**< Its size in bytes */
  uint32_t used;  /**< Bytes of held events: each is [size u32][sys_event_t and payload, padded to 4 B] */
} sys_event_hold_t;

/**
 * @brief From now on this task's publishes without SYS_EVENT_DEFER are kept in `buf` and not delivered, until
 * sys_event_release(hold). For code that publishes (or calls code that does) with a lock a listener may take: hold
 * before the lock, release after it. A task has one hold: a hold inside another keeps nothing and its release does
 * nothing, so the events go out when the outermost one is released. Not from an ISR.
 * @param[out] hold The hold, on the caller's stack
 * @param[in]  buf  Room for the held events, at a 4 B boundary; valid until the release
 * @param[in]  cap  Its size in bytes: an event takes 4 B, its 20 B header and its payload, padded to 4 B
 * @usage uint32_t buf[64]; sys_event_hold_t hold;
 *        sys_event_hold(&hold, buf, sizeof(buf));
 *        ...  // lock, publish, unlock
 *        sys_event_release(&hold);
 */
void sys_event_hold(sys_event_hold_t* hold, void* buf, uint32_t cap);

/**
 * @brief Ends the hold and delivers what it kept, in the order of the publishes, as sys_event_publish would have: the
 * inline listeners here, then one copy queued for the queued listeners. The hold is over before the first listener
 * runs, so a listener may hold and publish itself. A failure (a listener's, a full queue) is reported, not returned.
 * @param[in,out] hold The hold given to sys_event_hold()
 */
void sys_event_release(sys_event_hold_t* hold);

#define SYS_EVENT__PUBLISH(NAME, defer, src, ch, ...)                                                      \
  ({                                                                                                        \
    const evt_data_##NAME##_t _evt_data_ = {__VA_ARGS__};                                                   \
    sys_event_publish((sys_event_key_t){.tag = (NAME), .source = (src), .channel = (ch)},                   \
                      (uint16_t)((NAME##_FLAGS) | (defer)), &_evt_data_, sizeof(_evt_data_));               \
  })

/**
 * @brief Publishes event NAME of a table: its tag, its flags, and its payload (evt_data_<NAME>_t) from the fields
 * given, nothing for an event without one. The listeners inline run here (sys_event_publish).
 * @usage ERR_TRY(SYS_EVENT_PUBLISH(IO_EV_FALLING, c->id, pin));
 */
#define SYS_EVENT_PUBLISH(NAME, src, ch, ...) SYS_EVENT__PUBLISH(NAME, 0u, src, ch, __VA_ARGS__)

/**
 * @brief SYS_EVENT_PUBLISH with SYS_EVENT_DEFER: all of it in the lane's task, from a task an op may wait for.
 * @usage err_report(SYS_EVENT_PUBLISH_DEFER(IO_EV_ADC_INSIDE, c->id, pin, .mV = mv));
 */
#define SYS_EVENT_PUBLISH_DEFER(NAME, src, ch, ...) SYS_EVENT__PUBLISH(NAME, SYS_EVENT_DEFER, src, ch, __VA_ARGS__)

/**
 * @brief Typed view of an event's payload: its evt_data_<NAME>_t. The caller checks e->key.tag first.
 * @usage if (event->key.tag == IO_EV_ADC_INSIDE) { int32_t mv = SYS_EVENT_DATA(event, IO_EV_ADC_INSIDE)->mV; }
 */
#define SYS_EVENT_DATA(e, NAME) ((const evt_data_##NAME##_t*)(e)->data)

#ifdef __cplusplus
}
#endif

