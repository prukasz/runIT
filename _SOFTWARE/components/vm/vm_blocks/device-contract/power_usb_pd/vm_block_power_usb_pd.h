#pragma once
#include "vm_block_device.h"

/*
 *             ----------------
 *  ->EN       |              | ->ENO
 *  (->ARG0)   | POWER_USB_PD | ->RESULT0 (voltage)
 *  (->ARG1)   |              | ->RESULT1 (current)
 *             ----------------
 *
 *  VM_BLK_POWER_USB_PD -- the POWER_USB_PD contract (SYS_POWER.MD): a USB-PD sink (AP33772S). The operation is in the
 *  state (`op`, PD_OP_*). The engine, the kinds of operation and ENO are in vm_block_device.h.
 *
 *  Operation (op)        Inputs                             Results                      Kind
 *  SET_SETTINGS          voltage mV, current mA             -                            CHANGE
 *  GET_LIMITS            -                                  0: voltage mV, 1: current mA READ
 *
 *  GET_LIMITS is the highest-voltage offer of the source. Not here: LIST_OPTIONS (it answers an array).
 */

/* Load-time check of the block's state and shape; the body, called every pass. */
bool vm_verify_power_usb_pd(vm_block_h b);
void vm_blk_power_usb_pd(vm_block_h b);
