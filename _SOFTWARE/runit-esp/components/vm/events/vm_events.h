#pragma once

#include "sys_event.h"
#include "vm_id.h"

/*
 * Events of the VM (module MODULE_ID_VM, lane SYS): what happened to the program and its supervisor, for the app, the
 * screen and the modules that keep something of the program. Source = 0 (the board's one VM).
 *
 * All are published with SYS_EVENT_PUBLISH_DEFER: the mode changes under the VM's critical section, on the router's
 * task or the VM's, and the watchdog samples on the esp_timer task, so no listener runs there, and a listener may call
 * the VM's API. Deferred, they reach a listener some time after: none of them is the program's logic. The program does
 * not read the VM's own events (its first pass is a flag of the VM, its signals are its variables); the VM's queue
 * hears them like any other, an ON_EVENT block need not match them.
 *
 *   STATE:     the run mode seen from outside changed; channel = the new mode (vm_run_mode_e), `cause` why
 *              (vm_event_cause_e). Only STOPPED, RUNNING, FROZEN, SCAN, BLOCK: a STEP counts as SCAN, a BLOCK_STEP as
 *              BLOCK (each NEXT would be two). Sampled by the VM task between passes, so a mode held for less than its
 *              idle tick (10 ms) may not be seen. STOPPED only once the pass let go of every VM handle
 *   LOADED:    the program was replaced or reset (OPEN, RESET); `gen` counts the loads since the start. Who keeps
 *              object or block IDs of the program drops them
 *   HUNG:      block (channel) ran longer than CONFIG_VM_EXEC_BLOCK_WD_MS; once per episode (ERR_VM_EXEC_BLOCK_HUNG)
 *   RECOVERED: that block's call finished within the limit again: the episode of its HUNG is over
 *
 * A latched device fault is STATE STOPPED with the cause FAULT: the device and the root cause are in the error chain
 * the device's policy reports. NOTIFY: the app and the screen learn of a stop or a hung block without asking STATUS;
 * LOADED only drives code.
 */
#define VM_EVENTS(X, P)                                             \
  X(P, STATE,     0x01, SYS_EVENT_NOTIFY, { uint8_t cause; })       \
  X(P, LOADED,    0x02, 0,                { uint16_t gen; })        \
  X(P, HUNG,      0x03, SYS_EVENT_NOTIFY, { uint16_t block; })      \
  X(P, RECOVERED, 0x04, SYS_EVENT_NOTIFY, { uint16_t block; })

SYS_EVENTS_DECLARE(VM, MODULE_ID_VM, SYS)

/** @brief Why the run mode changed (STATE's `cause`). */
//#ref-enum @alias VM Event Cause
typedef enum vm_event_cause_e {
  VM_EVENT_CAUSE_COMMAND = 0,  //@alias Command @description An EXEC command
  VM_EVENT_CAUSE_LOAD    = 1,  //@alias Load @description A program loaded or reset
  VM_EVENT_CAUSE_FAULT   = 2,  //@alias Device fault @description A critical device error latched the VM (the board's SAFE, reason FAULT)
  VM_EVENT_CAUSE_SAFE    = 3,  //@alias Safe state @description The board's safe state (STOP, the dead-man, power)
} vm_event_cause_e;
