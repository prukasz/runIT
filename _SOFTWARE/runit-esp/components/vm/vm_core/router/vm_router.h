#pragma once

#include "router.h"
#include "vm_id.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Remote interface of the VM (MODULE_ID_VM, VM_CORE.MD "Remote"): build, run and watch a program.
 *
 * Every record layout is a packed struct of vm_wire.h. The same ops carry the program to the board and the board's
 * values back: the telemetry, PUSHes out of the port of MODULE_ID_VM, is `[VM_OP_DATA][n]{records}` for values and
 * `[VM_OP_ADD_OBJECTS][n]{records}` for the heap objects the app never uploaded (vm_sub.c), so the app parses both
 * directions with one decoder.
 *
 * Ops never renumber. A failing load op (OPEN to ADD_BLOCK, and the first start) discards the whole program (fail
 * closed, VM_CORE.MD), so the upload restarts at OPEN.
 */
#define VM_OP_RESET         0x01u  /**< -                              unload everything, leave the VM stopped */
#define VM_OP_OPEN          0x02u  /**< vm_wire_open_t                 replace the program with an empty one */
#define VM_OP_ADD_OBJECTS   0x03u  /**< u8 n, n x (vm_wire_obj_t, name) */
#define VM_OP_DATA          0x04u  /**< u8 n, n x (vm_wire_data_t, data)  stopped: load; running: runtime write */
#define VM_OP_ADD_ACCESSORS 0x05u  /**< u8 n, n x (vm_wire_acc_t, index steps) */
#define VM_OP_ADD_BLOCK     0x06u  /**< vm_wire_block_t, pins, state   one block per frame, in execution order */
#define VM_OP_SUBSCRIBE     0x07u  /**< u8 n, n x vm_wire_sub_t        n = 0 clears */
#define VM_OP_EXEC          0x08u  /**< vm_wire_exec_t                 vm_exec_command_e */
#define VM_OP_SPEED         0x09u  /**< vm_wire_speed_t                slow motion + program time per stepped pass */
#define VM_OP_STATUS        0x0Au  /**< -  ->  vm_wire_status_t        run mode, load state, fault latch, pass stats */

ROUTER_MODULE_DECLARE(vm_router);

/**
 * @brief Starts the VM: the event subscriptions (the program's events, the board's SAFE), telemetry, retained values,
 * the supervisor task (stopped until a program starts it). Boot step, after sys_event_init, the telemetry stream and
 * the router setup. At SAFE the VM stops (a device fault, reason FAULT, also latches: `EXEC` ACK_FAULT clears it).
 */
MUST_USE err_h vm_init(void);

#ifdef __cplusplus
}
#endif
