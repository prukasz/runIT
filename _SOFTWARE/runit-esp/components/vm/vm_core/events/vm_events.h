#pragma once

#include "sys_event.h"
#include "vm_id.h"

/*
 * Events of the VM (module MODULE_ID_VM, lane SYS): what happened to the program and its supervisor. Source = 0 (the
 * board's one VM). The VM hears its own events like any other (vm_event_init), so an ON_EVENT block of the program sees
 * them in the next pass: STATE on its RUNNING channel is the program's "first pass".
 *
 * The first payload field is the one an ON_EVENT block gets as VALUE (vm_event_t: the first 4 payload bytes).
 *
 *   STATE:     the run mode changed; channel = the new mode (vm_run_mode_e), `cause` why (vm_event_cause_e). STOPPED is
 *              published only once the pass let go of every VM handle, so a listener may take it as quiescent
 *   LOADED:    the program was replaced or reset (OPEN, RESET): who keeps object or block IDs of it drops them
 *   FAULT:     a critical device error latched the VM: `device` and the chain's root tag. ACK_FAULT is a STATE
 *   HUNG:      block `block` (channel too) ran longer than CONFIG_VM_EXEC_BLOCK_WD_MS; published deferred (the
 *              watchdog's sampler runs on the esp_timer task). Once per episode
 *   RECOVERED: that block's call finished within the limit again: the episode of its HUNG is over
 *   OVERRUN:   a pass took longer than the pass limit; once per episode, a pass within it ends the episode
 *   SIGNAL:    the program's own event (an EMIT block): channel = the signal's number, `value` what it carries
 *
 * NOTIFY: the app and the screen learn of a stop, a fault or a hung block without asking STATUS. LOADED and SIGNAL
 * only drive code.
 */
#define VM_EVENTS(X, P)                                                            \
  X(P, STATE,     0x01, SYS_EVENT_NOTIFY, { uint8_t cause; })                      \
  X(P, LOADED,    0x02, 0,                { uint16_t blocks; })                    \
  X(P, FAULT,     0x03, SYS_EVENT_NOTIFY, { uint16_t device; uint32_t root_tag; }) \
  X(P, HUNG,      0x04, SYS_EVENT_NOTIFY, { uint16_t block; uint16_t ms; })        \
  X(P, RECOVERED, 0x05, SYS_EVENT_NOTIFY, { uint16_t block; })                     \
  X(P, OVERRUN,   0x06, SYS_EVENT_NOTIFY, { uint32_t pass_ms; })                   \
  X(P, SIGNAL,    0x07, 0,                { int32_t value; })

SYS_EVENTS_DECLARE(VM, MODULE_ID_VM, SYS)

/** @brief Why the run mode changed (STATE's `cause`). */
typedef enum vm_event_cause_e {
  VM_EVENT_CAUSE_COMMAND = 0,  // an EXEC command
  VM_EVENT_CAUSE_LOAD    = 1,  // a program loaded or reset
  VM_EVENT_CAUSE_FAULT   = 2,  // a critical device error (FAULT)
  VM_EVENT_CAUSE_SAFE    = 3,  // the board's safe state (STOP, the dead-man, power)
  VM_EVENT_CAUSE_HUNG    = 4,  // the watchdog's response to a hung block or an overrun
} vm_event_cause_e;
