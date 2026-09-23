#pragma once
#include "vm_block_helpers.h"
#include "vm_exec.h"

/*
 *           -------------
 *  ->EN     |           | ->ENO
 *  (->ID)   |  ACTION   |
 *           -------------
 *
 *  VM_BLK_ACTION -- Requests a system action (SYS_ACTIONS.MD): a static one
 *  (safe state, freeze, device reset, ...) or a recorded packet sequence.
 *
 *  - Fires once per rising edge of its enable: a pulse from EDGE, PERIODIC or an
 *    event fires it once per pulse; a block with no enable sources fires once,
 *    when the program starts.
 *  - The action is queued (vm_exec_request_action) and runs on the actions task,
 *    never inside the pass: an action may stop, rewind or unload the VM itself,
 *    and a recorded replay would outlast the block watchdog.
 *  - ENO pulses on the pass the request was queued. The action's own result is
 *    reported by the actions task, after this pass.
 *  - ID comes from the pin, or from custom_data when the pin is unwired.
 *
 *  custom_data layout (4 bytes):
 *    [0] u8 scope      VM_ACTION_SCOPE_* (same values as SYS_ACTION_SCOPE_*)
 *    [1] u8 action_id  Fallback when ID is unwired, 1..255
 *    [2] u8 flags      VM_ACTION_F_* (runtime; 0 on the wire)
 *    [3] u8 _pad
 */

/* Same values as sys_actions' SYS_ACTION_SCOPE_*; the VM doesn't include
   sys_actions (it sits a layer above). */
//#ref-enum @alias Action Scope
typedef enum {
  VM_ACTION_SCOPE_STATIC = 0,    //@alias Static @description A built-in action (freeze, safe state, device reset, ...)
  VM_ACTION_SCOPE_RECORDED = 1,  //@alias Recorded @description A recorded packet sequence
} vm_action_scope_e;

#define VM_ACTION_F_PREV_EN (1u << 0)

typedef struct __attribute__((aligned(4))) {
  uint8_t scope;      // @enum-ref vm_action_scope_e
  uint8_t action_id;  // 1..255, used when input 0 is unwired
  uint8_t flags;      // VM_ACTION_F_* @runtime
  uint8_t _pad;
} vm_block_action_data_t;

_Static_assert(sizeof(vm_block_action_data_t) == 4, "vm_block_action_data_t must be 4 bytes");
#define VM_ACTION_CUSTOM_LEN sizeof(vm_block_action_data_t)

#define VM_ACTION_IN_ID 0u

static inline bool vm_verify_action(vm_block_h b) {
  vm_block_action_data_t d;
  memcpy(&d, vm_block_get_custom_data(b), sizeof(d));
  if (d.scope > VM_ACTION_SCOPE_RECORDED) return false;
  if (!vm_block_optional_in(b, VM_ACTION_IN_ID) && d.action_id == 0) return false;
  return true;
}

/* Enable-driven, on the rising edge of the enable level. */
static inline void vm_blk_action(vm_block_h b) {
  vm_block_action_data_t state;
  memcpy(&state, vm_block_get_custom_data(b), sizeof(state));

  const bool en = vm_block_is_enabled(b);
  const bool rising = en && !(state.flags & VM_ACTION_F_PREV_EN);
  state.flags = (uint8_t)(en ? (state.flags | VM_ACTION_F_PREV_EN) : (state.flags & ~VM_ACTION_F_PREV_EN));
  memcpy(vm_block_get_custom_data(b), &state, sizeof(state));

  if (!rising) {
    vm_block_set_eno(b, false);
    return;
  }

  uint32_t id = state.action_id;
  if (!vm_block_check(b, VM_BLOCK_GET_PARAM(id, b, VM_ACTION_IN_ID, state.action_id))) {
    vm_block_set_eno(b, false);
    return;
  }
  if (unlikely(id == 0 || id > UINT8_MAX)) {
    BLOCK_CALL(VM_BLK_ERR_NEW(ERR_INVALID_VAL_UI32, .val = id, .min = 1, .max = UINT8_MAX), b);
    vm_block_set_eno(b, false);
    return;
  }

  BLOCK_CALL(vm_exec_request_action(state.scope, (uint8_t)id), b);
  vm_block_set_eno(b, !vm_block_failed(b));
}

/* Palette entry (vm_blocks_table.c): shape and state size are checked at load
   by vm_block_verify(), so the body never re-checks them. */
//#vm-block VM_BLK_ACTION @title Run Action @category system @state vm_block_action_data_t @activation enable-rising Runs once each time its enable turns on.
//@block-description Requests a system action once per rising edge of its enable; the action runs outside the program pass.
//@rule scope is a vm_action_scope_e value. @error ERR_VM_BLK_BAD_SHAPE
//@rule With the id input unwired, action_id is not 0. @error ERR_VM_BLK_BAD_SHAPE
//@in 0 id @title Action id @description Overrides action_id. @value u32
#define VM_BLOCK_TYPE_ACTION \
  {.run = vm_blk_action, .check = vm_verify_action, .min_in = 0, .min_q = 0, .required_in = 0x0u, .state_len = VM_ACTION_CUSTOM_LEN}
