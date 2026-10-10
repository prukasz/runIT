#pragma once
#include "vm_block_helpers.h"

/*
 * Shared by the flow routers IF and SWITCH: both are enable-driven and stateless, and their outputs act as
 * enable gates for the blocks below. `taken` is the output that is driven loud; every other one is cleared quietly.
 */

#define VM_BRANCH_NONE 0xFFu  // no output taken (disabled, a failed read, or a selector out of range)

static inline void vm_branch_drive(vm_block_h b, uint8_t taken) {
  for (uint8_t pin = 0; pin < b->cfg.q_cnt; ++pin) {
    vm_block_drive_gate(b, pin, pin == taken);
  }
}
