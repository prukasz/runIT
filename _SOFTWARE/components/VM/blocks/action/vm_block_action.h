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

/* Load-time check of the block's state; the pin shape is checked from the //@in / //@out directives below. */
bool vm_verify_action(vm_block_h b);
/* The body, called every pass. */
void vm_blk_action(vm_block_h b);

//#vm-block VM_BLK_ACTION @id 14
//@title Run Action
//@category system
//@activation enable-rising Runs once each time its enable turns on.
//@data vm_block_action_data_t
//@header {title} | {id}
//@block-description Requests a system action once per rising edge of its enable; the action runs outside the program pass.
//@rule scope is a vm_action_scope_e value. @error ERR_VM_BLK_BAD_SHAPE
//@rule With the id input unwired, action_id is not 0. @error ERR_VM_BLK_BAD_SHAPE
//@in 0 id @title Action id @description Overrides action_id. @value u32 @overrides action_id @macro VM_ACTION_IN_ID
