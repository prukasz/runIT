#pragma once
#include "vm_block_helpers.h"
#include "vm_exec.h"

/*
 *           -------------
 *  ->EN     |           | ->ENO
 *  (->ID)   |  ACTION   |
 *           -------------
 *
 *  VM_BLK_ACTION -- Requests a record of the store (SYS_ACTIONS.MD) by its u32 ID: the frames it holds run
 *  as the router would run them (a user record, a system one, a device's defaults).
 *
 *  - Fires once per rising edge of its enable: a pulse from EDGE, PERIODIC or an
 *    event fires it once per pulse; a block with no enable sources fires once,
 *    when the program starts.
 *  - The record is queued (vm_exec_request_action) and runs on the router's task,
 *    never inside the pass: a record may stop, rewind or unload the VM itself,
 *    and a long replay would outlast the block watchdog.
 *  - ENO pulses on the pass the request was queued. The record's own result is
 *    reported by the router's task, after this pass.
 *  - ID comes from the pin, or from custom_data when the pin is unwired.
 */

#define VM_ACTION_F_PREV_EN (1u << 0)

typedef struct __attribute__((aligned(4))) {
  uint32_t action_id;  // The record to run, used when the Action id input is unwired.
  uint8_t flags;
  uint8_t _pad[3];
} vm_block_action_data_t;

_Static_assert(sizeof(vm_block_action_data_t) == 8, "vm_block_action_data_t must be 8 bytes");
#define VM_ACTION_CUSTOM_LEN sizeof(vm_block_action_data_t)

typedef enum vm_in_action_e {
  VM_IN_ACTION_ID = 0,  // The record to run; overrides the Action setting.
} vm_in_action_e;

/* Load-time check of the block's state; the pin shape comes from the pin enums above. */
bool vm_verify_action(vm_block_h b);
/* The body, called every pass. */
void vm_blk_action(vm_block_h b);
