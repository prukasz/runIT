#pragma once
#include "vm_block_device.h"

/*
 *             ---------------
 *  ->EN       |             | ->ENO
 *             |  LIFECYCLE  |
 *             ---------------
 *
 *  VM_BLK_LIFECYCLE -- the lifecycle ops every device has (SYS_DEVICE.MD): reset, suspend, resume. The operation is
 *  in the state (`op`, SYS_DEVICE_OP_*). The device is `device_id`, or 0xFFFF for every device (suspend all newest
 *  first, resume all oldest first, as the router sweeps). The engine and ENO are in vm_block_device.h.
 *
 *  Operation (op)        Inputs    Kind
 *  RESET                 -         EDGE   (outputs off, defaults)
 *  SUSPEND               -         EDGE   (outputs safe; the device refuses writes until RESUME)
 *  RESUME                -         EDGE
 *
 *  Each fires once per rising edge of the enable: a pulse from EDGE, PERIODIC or an event. REMOVE and CREATE are not
 *  here (they change the set of devices, a program does not).
 */

//#block VM_BLK_LIFECYCLE
//@alias Device control
//@category device
//@description Resets, suspends or resumes a device, or every device at once.
//@activation enabled
//@when Runs once on each rising edge of its enable.
//@face {op} {device_id}
//@contract DEVICE
//@data vm_block_device_data_t
/* Load-time check of the block's state and shape; the body, called every pass. */
bool vm_verify_lifecycle(vm_block_h b);
void vm_blk_lifecycle(vm_block_h b);
