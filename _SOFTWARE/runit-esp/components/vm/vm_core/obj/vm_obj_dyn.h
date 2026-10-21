#pragma once
#include <sdkconfig.h>
#include "errors.h"
#include "vm_obj.h"

/*
 * Heap objects: the trees a block builds at run time (a CLONE's copy, a parsed message), as opposed to the program's
 * arena objects. A heap object is an ordinary vm_obj_t with a small header in front of it (its slot in the register and
 * its owner), allocated and freed together.
 *
 * Ownership is a tree: a heap object hangs in at most one PTR cell, and dropping it from that cell frees it with
 * everything below it. Arena objects are program-lifetime and may sit in any number of cells. Sharing a heap tree means
 * copying it (CLONE).
 *
 * Building a tree (a block parsing a message):
 *   vm_obj_dyn_new() each node (mutable), vm_obj_dyn_adopt() it into its parent, and
 *   publish the root by linking it into a cell (an output's cell: vm_obj_link_direct), which checks the whole tree once.
 *   On failure before publishing, vm_obj_dyn_release(root) frees what was built.
 */

#define VM_OWNERSHIP_CYCLE 0u  // the cell is inside the tree being linked
#define VM_OWNERSHIP_DEPTH 1u  // nested deeper than CONFIG_VM_DYN_MAX_DEPTH
#define VM_OWNERSHIP_OWNED 2u  // the object already hangs in a cell

/* The register: every live heap object by slot (its ID is VM_OBJ_ID_DYN_BIT | slot), NULL for a free slot. At most
 * CONFIG_VM_DYN_MAX at once: a ceiling, so a runaway or malformed message is refused cleanly before DRAM runs out.
 * Public for the inline lookups; vm_obj_dyn.c owns every change. */
extern vm_obj_h g_vm_dyn[CONFIG_VM_DYN_MAX];

// What sits in front of a heap object.
typedef struct {
  vm_obj_h owner;  // the object whose PTR cell holds it (heap or arena); NULL until it is linked
  uint16_t slot;   // in g_vm_dyn
  uint16_t _pad;
} vm_obj_dyn_hdr_t;

_Static_assert(sizeof(vm_obj_dyn_hdr_t) % 4 == 0, "the object behind the header stays 4-aligned");

static inline vm_obj_dyn_hdr_t* vm_obj_dyn_hdr(vm_obj_h o) {
  return (vm_obj_dyn_hdr_t*)((uint8_t*)o - sizeof(vm_obj_dyn_hdr_t));
}

/** @brief Whether this object is a heap object (an arena object never is). */
static inline bool vm_obj_is_dynamic(vm_obj_h o) {
  return o != NULL && o->head.f.dynamic != 0;
}

/** @brief This object's slot in the register; VM_ID_NONE for an arena object or NULL. */
static inline uint16_t vm_obj_dyn_get_id(vm_obj_h o) {
  return vm_obj_is_dynamic(o) ? vm_obj_dyn_hdr(o)->slot : VM_ID_NONE;
}

/** @brief The heap object in slot @p id; NULL for an empty slot or one out of range. */
static inline vm_obj_h vm_obj_dyn_get_by_id(uint16_t id) {
  return id < CONFIG_VM_DYN_MAX ? g_vm_dyn[id] : NULL;
}

/**
 * @brief Allocates a heap object from a head (the shape, flags as given) and registers it, with no owner.
 * @return ERR_VM_OBJ_* for a bad shape (vm_obj_shape), ERR_VM_DYN_FULL when CONFIG_VM_DYN_MAX are live, ERR_VM_NO_MEM.
 */
MUST_USE err_h vm_obj_dyn_create(vm_obj_h* out, const vm_obj_head_t* head, const char* name);

/**
 * @brief A new node of a tree a block builds: `count` elements of `type` (a PTR for a folder, whose cells start empty),
 * named `name` (`name_len` up to VM_OBJ_NAME_MAX, 0 for none), mutable, values zero. No owner until it is adopted or
 * linked.
 * @return As vm_obj_dyn_create, ERR_VM_OBJ_NAME_TOO_LONG.
 */
MUST_USE err_h vm_obj_dyn_new(vm_obj_h* out, vm_obj_t_e type, uint16_t count, const char* name, uint8_t name_len);

/**
 * @brief Puts `child` (a heap object with no owner) into cell `index` of `parent` (a PTR heap object), while a tree is
 * being built. No depth or loop check here: linking the finished root into its cell checks the whole tree once.
 * @return ERR_VM_OBJ_OOB / ERR_VM_OBJ_NOT_PTR for a bad cell, ERR_VM_OBJ_OWNERSHIP (OWNED) for a child that has an owner.
 */
MUST_USE err_h vm_obj_dyn_adopt(vm_obj_h parent, uint16_t index, vm_obj_h child);

/**
 * @brief Checks that `child` may go into a cell of `owner`, and makes `owner` its owner. Arena children pass as they
 * are. A heap child must have no owner, must not contain `owner` (a loop), and the tree with it must stay within
 * CONFIG_VM_DYN_MAX_DEPTH heap levels. Called by every link (slot_store); nothing is changed when it fails.
 */
MUST_USE err_h vm_obj_dyn_take(vm_obj_h owner, vm_obj_h child);

/** @brief Frees a heap object and everything below it (the arena children stay). No-op on an arena object or NULL.
 *  For a tree that lost its cell, or one never linked. */
void vm_obj_dyn_release(vm_obj_h o);

/** @brief Frees every heap object, whatever links them. Called by vm_store_reset() before the arena goes. */
void vm_obj_dyn_reset(void);
