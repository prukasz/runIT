#include "vm_blocks.h"
#include "vm_blocks_registry.generated.h"  // every block header, and VM_BLOCK_TABLE_ENTRIES

/*
The palette, entire.

`const`, so it lives in flash and costs no RAM; a designated initialiser per
entry, so the index *is* the `block_type` on the wire; and one definition in
one file, because a firmware has exactly one palette. Each entry is defined by
the block's `//#vm-block` directives (body, extra check, pin shape, state size):
the entries and includes are generated (vm_blocks_registry.generated.h), so adding
a block type is its folder and a run of generate-all.py -- nothing here changes.

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
    VM_BLOCK_TABLE_ENTRIES
};

const uint16_t g_vm_block_types_cnt = (uint16_t)(sizeof(g_vm_block_types) / sizeof(g_vm_block_types[0]));
