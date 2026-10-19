#pragma once

/*
The block type numbers: the `block_type` byte of VM_OP_ADD_BLOCK and the index into the palette. They are part of the
program format, so a number is never reused for another block and never changes. Type 0 is deliberately unused: a
`block_type` nobody set is zero, and it should not resolve to a runnable block. Each block file states its own number
with VM_BLOCK_TYPE_DEFINE (vm_blocks.h); this header only holds the list so the numbers stay in one place.
*/

#define VM_BLK_NONE 0         // reserved: an unset block_type is not runnable
#define VM_BLK_EXPR 1         // Expression
#define VM_BLK_EXPR_BIT 2     // Bit Expression
#define VM_BLK_IF 3           // If
#define VM_BLK_SWITCH 4       // Switch
#define VM_BLK_FOR 5          // For
#define VM_BLK_SET 6          // Set
#define VM_BLK_CLONE 7        // Clone
#define VM_BLK_EDGE 8         // Edge
#define VM_BLK_TIMER 9        // Timer
#define VM_BLK_LATCH 12       // Latch
#define VM_BLK_PERIODIC 13    // Every
#define VM_BLK_ACTION 14      // Run Action
#define VM_BLK_ON_EVENT 15    // On Event
/* 10, 11, 16..20 are unused. The device blocks are one per contract and take numbers from 32. */
#define VM_BLK_IO 32             // IO contract: pins
#define VM_BLK_HBRIDGE 33        // HBRIDGE contract: motor channels
#define VM_BLK_POWER_MONITOR 34  // POWER_MONITOR contract: measured channels
#define VM_BLK_POWER_VREG 35     // POWER_VREG contract: an adjustable regulator
#define VM_BLK_POWER_SUPPLY 36   // POWER_SUPPLY contract: a power mux
#define VM_BLK_POWER_USB_PD 37   // POWER_USB_PD contract: a USB-PD sink
/* 38 is unused: a program does not manage the device lifecycle (the device router does). */
#define VM_BLK_SERVO 39          // the servo device's own ops: position, home, release
#define VM_BLK_DRV8962 40        // the DRV8962's own ops: current limit, fault
#define VM_BLK_TPS55289 41       // the TPS55289's own ops: protection

#define VM_BLOCK_TYPES_MAX 64  // the palette holds ids below this
