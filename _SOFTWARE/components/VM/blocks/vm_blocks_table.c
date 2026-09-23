#include "vm_blocks.h"
#include "vm_block_branch.h"
#include "vm_block_expr.h"
#include "vm_block_for.h"
#include "vm_block_clone.h"
#include "vm_block_set.h"
#include "vm_block_edge.h"
#include "vm_block_timer.h"
#include "vm_block_io_set_level.h"
#include "vm_block_io_toggle.h"
#include "vm_block_latch.h"
#include "vm_block_periodic.h"
#include "vm_block_action.h"
#include "vm_block_on_event.h"

/*
The palette, entire.

`const`, so it lives in flash and costs no RAM; a designated initialiser per
entry, so the index *is* the `block_type` on the wire; and one definition in
one file, because a firmware has exactly one palette. Each entry is defined by
its block's own header (VM_BLOCK_TYPE_<NAME>: body, extra check, pin shape,
state size), so adding a block type is its header plus one line here.

It lives beside the blocks rather than in core/exec/ on purpose. The supervisor
must not know what is in the palette -- it dispatches through the declarations
in vm_blocks.h and nothing more -- so the dependency runs blocks -> exec, never
back. That is what keeps `core/` free of every driver a real block pulls in.

C lets a later designated initialiser silently override an earlier one, so two
types claiming one id would compile clean and the last would win. The component
builds with -Woverride-init to make that a warning instead.
*/
const vm_block_type_t g_vm_block_types[] = {
    /* [VM_BLK_NONE] stays empty: an unset block_type must not be runnable. */
    [VM_BLK_EXPR] = VM_BLOCK_TYPE_EXPR,
    [VM_BLK_EXPR_BIT] = VM_BLOCK_TYPE_EXPR_BIT,
    [VM_BLK_IF] = VM_BLOCK_TYPE_IF,
    [VM_BLK_SWITCH] = VM_BLOCK_TYPE_SWITCH,
    [VM_BLK_FOR] = VM_BLOCK_TYPE_FOR,
    [VM_BLK_SET] = VM_BLOCK_TYPE_SET,
    [VM_BLK_CLONE] = VM_BLOCK_TYPE_CLONE,
    [VM_BLK_EDGE] = VM_BLOCK_TYPE_EDGE,
    [VM_BLK_TIMER] = VM_BLOCK_TYPE_TIMER,
    [VM_BLK_IO_SET_LEVEL] = VM_BLOCK_TYPE_IO_SET_LEVEL,
    [VM_BLK_IO_TOGGLE] = VM_BLOCK_TYPE_IO_TOGGLE,
    [VM_BLK_LATCH] = VM_BLOCK_TYPE_LATCH,
    [VM_BLK_PERIODIC] = VM_BLOCK_TYPE_PERIODIC,
    [VM_BLK_ACTION] = VM_BLOCK_TYPE_ACTION,
    [VM_BLK_ON_EVENT] = VM_BLOCK_TYPE_ON_EVENT,
};

const uint16_t g_vm_block_types_cnt = (uint16_t)(sizeof(g_vm_block_types) / sizeof(g_vm_block_types[0]));
