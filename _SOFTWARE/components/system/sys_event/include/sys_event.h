#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <sdkconfig.h>
#include "sys_error.h"

/*
 * Events: one publisher, any number of listeners (SYS_EVENT.MD).
 *
 * A source (device adapter, power manager, BLE, feature) publishes what
 * happened; it doesn't know who listens. Listeners live in one static
 * subscription table and are matched by domain / device / channel / event,
 * each field exact or SYS_EVENT_ANY.
 *
 *  - Inline listener: C handler called in the publisher's task, before
 *    sys_event_publish() returns. It may block on I/O (I2C), but never on a
 *    delay or timeout.
 *  - Queued listener: C handler and/or routes and actions, run later by the
 *    event task. Packets can create only queued route / action listeners.
 */

/* Wildcard for every match field of a subscription. */
#define SYS_EVENT_ANY 0xFF

//#ref-enum @alias Event Domain
typedef enum sys_event_domain_e {
  SYS_EVENT_DOMAIN_IO = 0,      //@alias IO @description Pin interrupts and analog alerts. Channel = pin, event = the edge or window crossed, value = level or mV
  SYS_EVENT_DOMAIN_POWER = 1,   //@alias Power @description Regulator, monitor and board power events. Event = power event; board events use device 255
  SYS_EVENT_DOMAIN_HBRIDGE = 2, //@alias H-Bridge @description Motor driver faults. Channel = bridge channel, event = fault reason, value = current in mA
  SYS_EVENT_DOMAIN_BLE = 3,     //@alias Bluetooth @description Connection events. Event = BLE event
  SYS_EVENT_DOMAIN_FEATURE = 4, //@alias Feature @description Events raised by features. Device = feature ID
} sys_event_domain_e;

/* One occurrence. Copied into the queue, so it holds no pointers. */
typedef struct sys_event_t {
  uint8_t domain;    /* sys_event_domain_e */
  uint8_t device_id; /* source device (or feature) ID */
  uint8_t channel;   /* pin / channel on that device, 0 when it has one */
  uint8_t event;     /* domain-specific event enum */
  int32_t value;     /* level, mV, mA, ... per domain */
  uint8_t hops;      /* republish depth, see SYS_EVENT_CAUSED_BY */
} sys_event_t;

/* In a handler that republishes: `.hops` of the new event, so a loop of
   republishes stops at CONFIG_SYS_EVENT_MAX_HOPS (ERR_EVENT_LOOP). */
#define SYS_EVENT_CAUSED_BY(cause) ((uint8_t)((cause)->hops + 1u))

/**
 * @brief Listener callback.
 * @param event Borrowed; copy it to keep it.
 * @return An error is reported by the dispatcher; other listeners still run.
 */
typedef err_h (*sys_event_handler_f)(const sys_event_t* event, void* ctx);

/* Route handler (registered by the application, e.g. the VM event buffer). */
typedef void (*sys_event_route_f)(const sys_event_t* event);

/* Scoped action executor (sys_actions): scope 0x00 static, 0x01 dynamic. */
typedef err_h (*sys_event_action_executor_f)(uint8_t scope, uint8_t id);

#define SYS_EVENT_ROUTE_BIT(idx) ((uint8_t)(1u << (idx)))

typedef struct sys_event_subscription_t {
  /* Match: exact value or SYS_EVENT_ANY. */
  uint8_t domain;
  uint8_t device_id;
  uint8_t channel;
  uint8_t event;
  /* Delivery. Inline takes only a handler; queued takes a handler and/or routes / actions. */
  bool inline_call;
  bool user;                 /* made from a packet: the only kind the unsubscribe packet may remove */
  uint8_t route_mask;        /* SYS_EVENT_ROUTE_BIT(CONFIG_SYS_EVENT_ROUTE_*) */
  uint8_t static_action_id;  /* 0 = none */
  uint8_t dynamic_action_id; /* 0 = none */
  sys_event_handler_f handler;
  void* ctx;
} sys_event_subscription_t;

/* Unsubscribe every user subscription (sys_event_unsubscribe with user = true). */
#define SYS_EVENT_SUB_ALL_USER 0xFF

SE_MUST_USE err_h sys_event_init(void);

/** @brief Register the action executor (runit, at boot). */
void sys_event_register_action_executor(sys_event_action_executor_f executor);

/** @brief Register fn at route slot route_idx (runit, at boot). A slot nobody registers is skipped. */
SE_MUST_USE err_h sys_event_register_route(uint8_t route_idx, sys_event_route_f fn);

/**
 * @brief Add a listener.
 * @param out_id Subscription ID, for sys_event_unsubscribe().
 * @return ERR_EVENT_SUB_INVALID for a listener with nothing to call,
 *         ERR_EVENT_TABLE_FULL when all CONFIG_SYS_EVENT_MAX_SUBSCRIPTIONS are used.
 */
SE_MUST_USE err_h sys_event_subscribe(const sys_event_subscription_t* sub, uint8_t* out_id);

/**
 * @brief Remove a listener. It may still get an event already being delivered.
 * @param user true on behalf of a packet: only user subscriptions can be
 *        removed (ERR_EVENT_SUB_PROTECTED otherwise), and SYS_EVENT_SUB_ALL_USER
 *        removes all of them.
 */
SE_MUST_USE err_h sys_event_unsubscribe(uint8_t id, bool user);

/**
 * @brief Publish an event: inline listeners run now, in table order, then one
 * copy is queued if any queued listener matches.
 *
 * From an ISR the whole delivery is deferred to the event task (inline
 * listeners then run there); an ISR drop is counted and reported by the task.
 *
 * @return ERR_EVENT_LOOP when event->hops reached CONFIG_SYS_EVENT_MAX_HOPS,
 *         ERR_EVENT_QUEUE_FULL when the queued copy didn't fit. Listener
 *         failures are reported, not returned.
 */
SE_MUST_USE err_h sys_event_publish(const sys_event_t* event);
