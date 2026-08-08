#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "sys_error.h"
#include "sys_error_vm.h"
#include "vm_alloc.h"
#include "vm_obj.h"
#include "vm_obj_access.h"

/*
Object construction -- the counterpart to vm_obj_access.h, which only ever
reads and writes objects that already exist.

Two callers, deliberately sharing one implementation: the program loader
building the static object graph from an uploaded packet, and any block that
has to build a shape at runtime. Both bump-allocate from a vm_alloc_t and
neither ever frees an individual object; storage is reclaimed by resetting
the whole arena (see vm_alloc.h).

Building a nested object is two steps, in this order:
  1. create the children,
  2. create the VM_OBJ_PTR parent and vm_obj_link_direct() each child in.
A parent's payload holds vm_obj_h values, so every child must already exist
and have a final address before the parent can point at it.
*/

typedef struct vm_obj_cfg_t {
  vm_obj_t_e type;
  uint16_t item_count;  // elements, not bytes; 1 for a plain scalar, 0 rejected
  const char* name;     // NUL-terminated, <= VM_OBJ_NAME_MAX; NULL leaves it untagged

  bool mutable;        // anything may write it
  bool usr_mutable;    // user-authored logic (a Set block, a remote write) may write it
  bool upd_resetable;  // the upd flag may be cleared by a reader
  bool retentive;      // persist to NVS; rejected for VM_OBJ_PTR
} vm_obj_cfg_t;

/**
 * @brief Bump-allocate and initialise one object.
 *
 * Payload and name bytes are zeroed, so a freshly created object reads as 0 /
 * empty rather than whatever the arena last held.
 *
 * @param out Receives the new handle; set to NULL on any failure.
 * @param arena Arena to carve from.
 * @param cfg Shape and flags.
 * @return err_h NULL on success. ERR_VM_OBJ_BAD_TYPE for a width-less type,
 *         ERR_VM_OBJ_EMPTY for item_count 0 -- an object with no storage
 *         still has an address, and that address belongs to the next
 *         allocation -- ERR_VM_OBJ_TOO_LARGE if item_count * width exceeds
 *         the 16-bit payload_size field, ERR_VM_OBJ_NAME_TOO_LONG past
 *         VM_OBJ_NAME_MAX, ERR_VM_OBJ_RETENTIVE_PTR for a retentive
 *         pointer, or ERR_BASE_NO_MEM when the arena is full -- in which
 *         case vm_alloc() has separately reported the requested and
 *         remaining byte counts.
 *
 * @code
 * vm_obj_h temp;
 * SE_RET_IF_ERR(vm_obj_create(&temp, &arena, &(vm_obj_cfg_t){
 *     .type = VM_OBJ_F, .item_count = 1, .name = "temp", .mutable = true}));
 * @endcode
 */
err_h vm_obj_create(vm_obj_h* out, vm_alloc_t* arena, const vm_obj_cfg_t* cfg);

/**
 * @brief Bump-allocate the id -> object array for a program's table.
 *
 * Every slot starts NULL, so an id that the loader never fills fails closed
 * in vm_obj_by_id() instead of resolving into stale storage.
 *
 * @param table Table to point at the new array.
 * @param arena Arena to carve from.
 * @param count Number of ids.
 * @return err_h NULL on success, ERR_BASE_NO_MEM if the arena is full.
 */
err_h vm_obj_table_build(vm_obj_table_t* table, vm_alloc_t* arena, uint16_t count);

/**
 * @brief Bind `id` to `obj`, bounds-checked.
 *
 * The write counterpart to vm_obj_by_id(). Loader ids come off the wire, so
 * this is the untrusted-input boundary for the table: a `items[id] = obj`
 * with an id straight from a packet would corrupt whatever sits past the
 * array. Assigning the same id twice is rejected too -- within one load each
 * id is written exactly once, so a repeat means a malformed program rather
 * than an intentional update.
 *
 * @return err_h NULL on success, ERR_VM_OBJ_TABLE_OOB if id is past the
 *         table, ERR_VM_OBJ_TABLE_DUP if the slot is already bound.
 */
err_h vm_obj_table_set(vm_obj_table_t* table, uint16_t id, vm_obj_h obj);

/**
 * @brief Detach the table, leaving it empty.
 *
 * Called on the upload path before the arena it points into is reset. An
 * empty table means every vm_obj_by_id() returns NULL, so accessor
 * resolution and any late-arriving event fail closed for the whole window
 * between teardown and the next successful load, rather than resolving into
 * the previous program's storage.
 */
void vm_obj_table_reset(vm_obj_table_t* table);

/* ===========================================================================
   Accessor construction

   Same shape as the object side: build the table, create each accessor, bind
   it to an id. An accessor is allocated together with its index array in one
   slice, then each index is filled in by position.

   `VM_IDX_REF` takes an already-built accessor rather than an id on purpose:
   requiring the target to exist before the referencing index is written makes
   reference cycles impossible to construct, which is stronger than
   VM_ACCESSOR_MAX_DEPTH catching them later at resolve time.
   =========================================================================== */

/** @brief Bump-allocate the id -> accessor array; every slot starts NULL. */
err_h vm_accessor_table_build(vm_accessor_table_t* table, vm_alloc_t* arena, uint16_t count);

/** @brief Bind `id` to `acc`, bounds-checked, rejecting a second assignment.
 *  @return err_h ERR_VM_ACC_TABLE_OOB, ERR_VM_ACC_TABLE_DUP. */
err_h vm_accessor_table_set(vm_accessor_table_t* table, uint16_t id, vm_accessor_t* acc);

/** @brief Detach the table, leaving it empty (fail-closed during teardown). */
void vm_accessor_table_reset(vm_accessor_table_t* table);

/**
 * @brief Allocate an accessor plus its index array.
 *
 * Indices start as `VM_IDX_LITERAL 0`; fill them with the setters below.
 * `idx_count == 0` is a whole-object accessor and allocates no index array.
 *
 * @return err_h ERR_BASE_NO_MEM when the arena is full.
 */
err_h vm_accessor_create(vm_accessor_t** out, vm_alloc_t* arena, uint16_t root_obj_id, uint8_t idx_count);

/** @brief Set index `pos` to a fixed position. */
err_h vm_accessor_set_literal(vm_accessor_t* acc, uint8_t pos, uint32_t value);

/** @brief Set index `pos` to read its position live from `ref`. */
err_h vm_accessor_set_ref(vm_accessor_t* acc, uint8_t pos, const vm_accessor_t* ref);

/**
 * @brief Set index `pos` to match a child tag.
 *
 * Copies `name` into @p arena NUL-terminated -- the wire form is not
 * terminated and the frame buffer does not outlive the load, so the accessor
 * cannot simply point at it.
 *
 * @return err_h ERR_VM_OBJ_NAME_TOO_LONG past VM_OBJ_NAME_MAX, ERR_BASE_NO_MEM.
 */
err_h vm_accessor_set_name(vm_accessor_t* acc, uint8_t pos, vm_alloc_t* arena, const char* name, uint8_t name_len);

/**
 * @brief Pre-resolve an accessor whose address cannot move, so every later
 *        access is four loads instead of a walk.
 *
 * Qualifies: a whole-object accessor, or one literal index on a root object
 * whose element is in range. Both are fixed for the life of the program --
 * see the VM_ACC_F_CACHED note in vm_obj_access.h for why a pointer slot
 * counts as fixed and a two-level chain does not.
 *
 * Call after the root object exists and its indices are set. The loader does
 * this itself at the end of vm_loader_add_accessor(); code building accessors
 * directly has to call it. Skipping it costs speed and nothing else.
 *
 * @return true if the accessor is now cached, false if its shape does not
 *         qualify or the root object is missing -- never an error, since
 *         resolving the long way is always available.
 */
bool vm_accessor_cache_build(vm_accessor_t* acc);
