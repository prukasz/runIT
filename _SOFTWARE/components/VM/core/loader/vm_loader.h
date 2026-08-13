#pragma once
#include <stdint.h>
#include "sys_error.h"
#include "sys_error_vm.h"
#include "vm_block_build.h"
#include "vm_obj_access.h"
#include "vm_obj_build.h"

/*
Program loading -- turns an uploaded description into the live object graph.

Every argument here comes off the wire, so this is the trust boundary: ids,
counts, sizes and offsets are all validated before they reach vm_obj_create()
or a registry. The wire parsing itself lives in the decoder
(dec_vm_loader.h); vm_store.h owns the storage, and this file owns the rules
and the load-order state machine.

Load sequence, which is also the teardown-safety story:

  vm_loader_reset()      -> registries detached, arena reset. Everything resolves
                            to NULL, so accessors and late callbacks fail
                            closed for the whole window before the next load.
  vm_loader_open(...)    -> reserve the id registries and cap the arena
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
 *         ERR_BASE_NO_MEM if the id registries do not fit inside it.
 */
err_h vm_loader_open(uint16_t obj_cnt, uint16_t acc_cnt, uint16_t blk_cnt, uint32_t total_size);

/**
 * @brief Create one object and bind it to @p id.
 *
 * @param flags VM_LOAD_F_* bits.
 * @param name Tag bytes, not NUL-terminated on the wire; NULL when name_len is 0.
 * @return err_h the validation chain from vm_obj_create()
 *         -- unknown type, oversize payload, over-long name, retentive
 *         pointer, duplicate or out-of-range id.
 */
err_h vm_loader_add_obj(uint16_t id, uint16_t payload_size, uint8_t type, uint8_t flags, const char* name,
                        uint8_t name_len);

/**
 * @brief Fill part of an object's payload.
 *
 * For a VM_OBJ_PTR parent, @p data is a list of little-endian uint16 child
 * ids -- never raw pointers, which mean nothing off-device -- and each is
 * resolved through the registry and linked. For every other type @p data is raw
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
 * @return err_h ERR_VM_ACC_BAD_KIND, ERR_VM_REG_OOB/_DUP,
 *         ERR_VM_LOAD_SHORT_RECORD, or the accessor build chain.
 */
err_h vm_loader_add_accessor(uint16_t acc_id, uint16_t root_obj_id, uint8_t idx_count, const uint8_t* idx_data,
                             uint16_t idx_len);

/**
 * @brief Create one block and bind it to @p blk_id.
 *
 * Last in the load order by necessity: a block names accessors (inputs, EN)
 * and objects (outputs, ENO) by id, and every one of them must already be
 * bound. Those ids are resolved to pointers here and the block keeps only the
 * pointers, so nothing downstream can hold an id that stopped being valid.
 *
 * The id is checked against the registry *before* the block is built, so a
 * duplicate or out-of-range id costs no arena -- a rejected program leaves
 * the space its retry needs.
 *
 * @return err_h ERR_VM_REG_OOB / ERR_VM_REG_DUP for the id,
 *         ERR_VM_BLK_BAD_SHAPE for an unbuildable pin count,
 *         ERR_VM_BLK_BAD_REF naming the slot whose id did not resolve, or
 *         ERR_BASE_NO_MEM.
 */
err_h vm_loader_add_block(uint16_t blk_id, const vm_block_cfg_t* cfg);

/** @brief Current state -- decoders use it to reject out-of-sequence packets. */
vm_load_state_e vm_loader_state(void);

/** @brief Bytes consumed so far, for diagnostics after a load. */
uint32_t vm_loader_used(void);
