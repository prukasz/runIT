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
 *   STATE:     the run mode changed; channel = the new mode (vm_run_mode_e), `cause` why (vm_event_cause_e). Only the
 *              modes seen from outside: STOPPED, RUNNING, FROZEN, SCAN, BLOCK (not STEP, BLOCK_STEP: each NEXT would
 *              be two). STOPPED once the pass let go of every VM handle, so a listener may take it as quiescent
 *   LOADED:    the program was replaced or reset (OPEN, RESET); `gen` its generation, also in STATUS. Who keeps object
 *              or block IDs of the program drops them; the VM's API takes the generation and refuses an old one, so a
 *              read between the load and this event can't reach the new program's objects
 *   HUNG:      block (channel) ran longer than CONFIG_VM_EXEC_BLOCK_WD_MS; once per episode
 *   RECOVERED: that block's call finished within the limit again: the episode of its HUNG is over
 *   OVERRUN:   a pass took longer than the pass limit; once per episode, a pass within it ends the episode
 *
 * A latched device fault is STATE STOPPED with the cause FAULT: the device and the root cause are in the error chain
 * the device's policy reports. NOTIFY: the app and the screen learn of a stop or a hung block without asking STATUS;
 * LOADED only drives code.
 */
#define VM_EVENTS(X, P)                                             \
  X(P, STATE,     0x01, SYS_EVENT_NOTIFY, { uint8_t cause; })       \
  X(P, LOADED,    0x02, 0,                { uint16_t gen; })        \
  X(P, HUNG,      0x03, SYS_EVENT_NOTIFY, { uint16_t block; })      \
  X(P, RECOVERED, 0x04, SYS_EVENT_NOTIFY, { uint16_t block; })      \
  X(P, OVERRUN,   0x05, SYS_EVENT_NOTIFY, { uint32_t pass_ms; })

SYS_EVENTS_DECLARE(VM, MODULE_ID_VM, SYS)

/** @brief Why the run mode changed (STATE's `cause`). */
typedef enum vm_event_cause_e {
  VM_EVENT_CAUSE_COMMAND = 0,  // an EXEC command
  VM_EVENT_CAUSE_LOAD    = 1,  // a program loaded or reset
  VM_EVENT_CAUSE_FAULT   = 2,  // a critical device error latched the VM
  VM_EVENT_CAUSE_SAFE    = 3,  // the board's safe state (STOP, the dead-man, power)
  VM_EVENT_CAUSE_HUNG    = 4,  // the watchdog's response to a hung block or an overrun
} vm_event_cause_e;
