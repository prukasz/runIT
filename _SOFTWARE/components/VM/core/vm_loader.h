#pragma once
#include <stdint.h>
#include "sys_error.h"
#include "sys_error_vm.h"
#include "vm_alloc.h"
#include "vm_obj_access.h"
#include "vm_obj_build.h"

/*
Program loading -- turns an uploaded description into the live object graph.

Every argument here comes off the wire, so this is the trust boundary: ids,
counts, sizes and offsets are all validated before they reach vm_obj_create()
or the object table. The wire parsing itself lives in the decoder
(dec_vm_loader.h); this file owns the storage and the rules.

Load sequence, which is also the teardown-safety story:

  vm_loader_reset()      -> table detached, arena reset. Everything resolves
                            to NULL, so accessors and late callbacks fail
                            closed for the whole window before the next load.
  vm_loader_open(n, sz)  -> reserve the id table and cap the arena at sz
  vm_loader_add_obj(...) -> once per object, any order of ids
  vm_loader_set_data(...) -> payload bytes, or child ids for VM_OBJ_PTR
                            parents; may be called repeatedly for slices

Objects must exist before anything links to them, so all add_obj packets
have to arrive before the set_data that references them as children. That is
the only ordering constraint between the two -- data for a plain scalar can
interleave freely.
*/

/**
 * @brief Wire class byte for program upload.
 *
 * Lives here rather than in the decoder because this class is 1:1 with this
 * component -- the same reason SYS_ACTIONS_CLASS_HEADER lives in
 * sys_actions.h. dec_vm_loader.h includes this header and uses it.
 */
#define VM_LOADER_CLASS_HEADER 0x04

/**
 * @brief Bytes reserved for one loaded program: objects, accessors, blocks,
 *        both tables -- everything a program owns.
 *
 * This is a static .bss array, reserved whether or not a program is loaded, so
 * it comes straight off the DIRAM the heap also draws from. Size it from a
 * target program rather than by feel:
 *
 *   block          sizeof(vm_block_data_t) 20 B + 4 B per input + 4 B per
 *                  output + its own variables (custom_data)
 *   object         4 B head + payload, 4-byte aligned, + tag bytes if named
 *   accessor       8 B + 8 B per chain index (shared between pins that read
 *                  the same thing, so this is a worst case)
 *   tables         4 B per object id + 4 B per accessor id
 *
 * Worked example -- 100 blocks, 5 pins each, one 8-byte output apiece, 16 B of
 * own variables, no accessor sharing:
 *
 *   blocks     100 x (20 + 20 + 4 + 16) =  6.0 kB
 *   outputs    100 x (4 + 8)            =  1.2 kB
 *   constants  100 x (4 + 8)            =  1.2 kB
 *   accessors  500 x (8 + 8)            =  8.0 kB
 *   tables     200 ids + 500 ids        =  2.8 kB
 *                                         -------
 *                                         19.2 kB
 *
 * 48 kB leaves that roughly 2.5x of headroom for deeper chains, larger
 * payloads and blocks that keep more state, while still leaving the bulk of
 * DIRAM to the heap and BLE. A program that does not fit fails the load with
 * ERR_VM_LOAD_TOO_BIG reporting requested-vs-available, rather than
 * misbehaving -- so raising this is a deliberate act with a number attached.
 */
#ifndef VM_LOADER_POOL_SIZE
#define VM_LOADER_POOL_SIZE (48 * 1024)
#endif

typedef enum vm_load_state_e {
  VM_LOAD_EMPTY = 0,  // no storage; every id resolves to NULL
  VM_LOAD_OPEN = 1,   // storage reserved, objects may be added and filled
} vm_load_state_e;

/* Flag bits as they travel on the wire, kept separate from vm_obj_head_t's
   bitfield layout -- that layout is compiler-defined and a client has no way
   to reproduce it reliably. `tagged` is derived from the name length and
   `upd` always starts clear, so neither appears here. */
#define VM_LOAD_F_MUTABLE 0x01
#define VM_LOAD_F_USR_MUTABLE 0x02
#define VM_LOAD_F_UPD_RESETABLE 0x04
#define VM_LOAD_F_RETENTIVE 0x08

/** @brief Tear down whatever is loaded. Safe to call at any time, including
 *  on a failed or abandoned upload -- it leaves the VM in the fail-closed
 *  state rather than half-built. */
void vm_loader_reset(void);

/**
 * @brief Reserve storage for a program.
 *
 * Resets first, so an upload that starts over mid-stream cannot end up mixing
 * two programs. The arena is capped at @p total_size rather than the whole
 * pool, so a program that under-declares its own size fails at the object
 * that overruns instead of quietly borrowing space it never asked for.
 *
 * @return err_h ERR_VM_LOAD_TOO_BIG if total_size exceeds the pool,
 *         ERR_BASE_NO_MEM if the id table does not fit inside it.
 */
err_h vm_loader_open(uint16_t obj_cnt, uint16_t acc_cnt, uint32_t total_size);

/**
 * @brief Create one object and bind it to @p id.
 *
 * @param flags VM_LOAD_F_* bits.
 * @param name Tag bytes, not NUL-terminated on the wire; NULL when name_len is 0.
 * @return err_h the validation chain from vm_obj_create()/vm_obj_table_set()
 *         -- unknown type, oversize payload, over-long name, retentive
 *         pointer, duplicate or out-of-range id.
 */
err_h vm_loader_add_obj(uint16_t id, uint16_t item_count, uint8_t type, uint8_t flags, const char* name,
                        uint8_t name_len);

/**
 * @brief Fill part of an object's payload.
 *
 * For a VM_OBJ_PTR parent, @p data is a list of little-endian uint16 child
 * ids -- never raw pointers, which mean nothing off-device -- and each is
 * resolved through the table and linked. For every other type @p data is raw
 * payload bytes.
 *
 * @param start_idx First element to write, in elements (not bytes).
 * @param len Length of @p data in bytes.
 * @return err_h ERR_VM_LOAD_DATA_RANGE if the write would run past the
 *         object, or the link error for an unresolvable child id.
 */
err_h vm_loader_set_data(uint16_t id, uint16_t start_idx, const uint8_t* data, uint16_t len);

/**
 * @brief Create one accessor and bind it to @p acc_id.
 *
 * Index records are walked from @p idx_data, each `{u8 kind, payload}`:
 * `VM_IDX_LITERAL` a u32 position, `VM_IDX_REF` a u16 accessor id,
 * `VM_IDX_NAME` a u8 length then that many unterminated bytes.
 *
 * A `VM_IDX_REF` target must already exist, which is what makes a reference
 * cycle impossible to build rather than merely caught at resolve time.
 *
 * @param idx_count Number of index records present in @p idx_data.
 * @param idx_len Length of @p idx_data in bytes; records are bounds-checked
 *                against it so a malformed count cannot overread.
 * @return err_h ERR_VM_ACC_BAD_KIND, ERR_VM_ACC_TABLE_OOB/_DUP,
 *         ERR_VM_LOAD_SHORT_RECORD, or the accessor build chain.
 */
err_h vm_loader_add_accessor(uint16_t acc_id, uint16_t root_obj_id, uint8_t idx_count, const uint8_t* idx_data,
                             uint16_t idx_len);

/** @brief Current state -- decoders use it to reject out-of-sequence packets. */
vm_load_state_e vm_loader_state(void);

/** @brief Bytes consumed so far, for diagnostics after a load. */
uint32_t vm_loader_used(void);
