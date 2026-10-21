#include "vm_obj_dyn.h"
#include "vm_err.h"
#include "vm_errors.h"
#include "esp_heap_caps.h"
#include "vm_obj_build.h"

/*
 * Heap objects (vm_obj_dyn.h): allocation with the header in front, the tree rule checked when a tree is linked, and
 * freeing a tree with everything below it.
 */

/* Same caps as the program pool: internal DRAM only. A message tree is walked by accessors on every pass that reads
   it, so putting it behind the PSRAM cache would make access cost depend on what else is resident. */
#define VM_DYN_HEAP_CAPS (MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL)

vm_obj_h g_vm_dyn[CONFIG_VM_DYN_MAX];

static err_h ownership_err(uint8_t reason) {
  return ERR_NEW(ERR_VM_OBJ_OWNERSHIP, .reason = reason, .limit = CONFIG_VM_DYN_MAX_DEPTH);
}

/* Heap levels from `o` down, `level` counting `o`: within CONFIG_VM_DYN_MAX_DEPTH. A tree built with adopt() is not
   checked until now, so this bound is also what stops a loop made while building. */
static err_h check_height(vm_obj_h o, uint8_t level) {
  if (level > CONFIG_VM_DYN_MAX_DEPTH) return ownership_err(VM_OWNERSHIP_DEPTH);
  if ((vm_obj_t_e)o->head.d.obj_t != VM_OBJ_PTR) return NULL;
  vm_obj_h* kids = (vm_obj_h*)o->payload;
  const uint16_t n = vm_obj_get_items_cnt(o);
  for (uint16_t i = 0; i < n; i++) {
    if (vm_obj_is_dynamic(kids[i])) ERR_TRY(check_height(kids[i], (uint8_t)(level + 1)));
  }
  return NULL;
}

/* Frees `o` and the heap objects it owns. Arena children stay. The depth bound only matters for a tree that was never
   linked (linking checks the depth): what it leaves is freed by the next reset. */
static void dyn_free(vm_obj_h o, uint8_t depth) {
  if ((vm_obj_t_e)o->head.d.obj_t == VM_OBJ_PTR && depth < CONFIG_VM_DYN_MAX_DEPTH) {
    vm_obj_h* kids = (vm_obj_h*)o->payload;
    const uint16_t n = vm_obj_get_items_cnt(o);
    for (uint16_t i = 0; i < n; i++) {
      // children first: reading the payload after free() would be a use-after-free
      if (vm_obj_is_dynamic(kids[i]) && vm_obj_dyn_hdr(kids[i])->owner == o) dyn_free(kids[i], (uint8_t)(depth + 1));
    }
  }
  vm_obj_dyn_hdr_t* h = vm_obj_dyn_hdr(o);
  g_vm_dyn[h->slot] = NULL;
  heap_caps_free(h);
}

err_h vm_obj_dyn_create(vm_obj_h* out, const vm_obj_head_t* head, const char* name) {
  ERR_CHECK(out != NULL, ERR_VM_BAD_ARG);
  *out = NULL;

  // The shape is checked before a slot is taken or a byte allocated, as on the arena path
  uint32_t total = 0;
  ERR_TRY(vm_obj_shape(head, &total));

  uint16_t slot = 0;
  while (slot < CONFIG_VM_DYN_MAX && g_vm_dyn[slot] != NULL) slot++;
  if (slot == CONFIG_VM_DYN_MAX) return ERR_NEW(ERR_VM_DYN_FULL, .limit = CONFIG_VM_DYN_MAX);

  // zeroed like arena objects: payload and name read as empty, cells as unwired
  vm_obj_dyn_hdr_t* h = (vm_obj_dyn_hdr_t*)heap_caps_calloc(1, sizeof(*h) + total, VM_DYN_HEAP_CAPS);
  if (!h) return ERR_NEW(ERR_VM_NO_MEM, .size = (uint32_t)(sizeof(*h) + total));
  h->slot = slot;

  vm_obj_h o = (vm_obj_h)(h + 1);
  vm_obj_init(o, head, name);
  o->head.f.dynamic = 1;  // the one field the shared init cannot set
  g_vm_dyn[slot] = o;
  *out = o;
  return NULL;
}

err_h vm_obj_dyn_new(vm_obj_h* out, vm_obj_t_e type, uint16_t count, const char* name, uint8_t name_len) {
  ERR_CHECK(out != NULL, ERR_VM_BAD_ARG);
  *out = NULL;
  if (name_len > VM_OBJ_NAME_MAX) return ERR_NEW(ERR_VM_OBJ_NAME_TOO_LONG, .len = name_len);
  ERR_CHECK(name_len == 0 || name != NULL, ERR_VM_BAD_ARG);
  const uint32_t size = (uint32_t)count << vm_type_shift(type);
  if (size > UINT16_MAX) return ERR_NEW(ERR_VM_RANGE, .val = count, .min = 1, .max = UINT16_MAX >> vm_type_shift(type));

  const vm_obj_head_t head = {
      .payload_size = (uint16_t)size,
      .d            = {.obj_t = type, .name_size = name_len},
      .f            = {.mutable = 1},
  };
  return vm_obj_dyn_create(out, &head, name_len ? name : NULL);
}

err_h vm_obj_dyn_adopt(vm_obj_h parent, uint16_t index, vm_obj_h child) {
  ERR_CHECK(vm_obj_is_dynamic(parent) && vm_obj_is_dynamic(child), ERR_VM_BAD_ARG);
  const uint16_t parent_id = VM_OBJ_ID_DYN_BIT | vm_obj_dyn_get_id(parent);
  if ((vm_obj_t_e)parent->head.d.obj_t != VM_OBJ_PTR) return vm_obj_not_ptr_err(parent_id, parent->head.d.obj_t);
  if (index >= vm_obj_get_items_cnt(parent)) return vm_obj_oob_err(parent_id, index);
  if (vm_obj_dyn_hdr(child)->owner != NULL) return ownership_err(VM_OWNERSHIP_OWNED);

  vm_obj_h* cell = &((vm_obj_h*)parent->payload)[index];
  vm_obj_h prev = *cell;
  *cell = child;
  vm_obj_dyn_hdr(child)->owner = parent;
  if (vm_obj_is_dynamic(prev)) dyn_free(prev, 0);
  return NULL;
}

err_h vm_obj_dyn_take(vm_obj_h owner, vm_obj_h child) {
  if (!vm_obj_is_dynamic(child)) return NULL;  // arena: program lifetime, any number of cells
  vm_obj_dyn_hdr_t* h = vm_obj_dyn_hdr(child);
  if (h->owner != NULL) return ownership_err(VM_OWNERSHIP_OWNED);

  // The heap levels above the cell: the owner and its heap owners. Meeting the child means the cell is inside it.
  uint8_t above = 0;
  for (vm_obj_h o = owner; vm_obj_is_dynamic(o); o = vm_obj_dyn_hdr(o)->owner) {
    if (o == child) return ownership_err(VM_OWNERSHIP_CYCLE);
    if (++above >= CONFIG_VM_DYN_MAX_DEPTH) return ownership_err(VM_OWNERSHIP_DEPTH);
  }
  ERR_TRY(check_height(child, (uint8_t)(above + 1)));
  h->owner = owner;
  return NULL;
}

void vm_obj_dyn_release(vm_obj_h o) {
  if (vm_obj_is_dynamic(o)) dyn_free(o, 0);
}

void vm_obj_dyn_reset(void) {
  /* Every live object is in the register exactly once, so freeing each entry frees the lot, without following any
     link: a tree never linked, or one a failed build left, cannot outlive a reload. */
  for (uint16_t i = 0; i < CONFIG_VM_DYN_MAX; i++) {
    if (g_vm_dyn[i] != NULL) heap_caps_free(vm_obj_dyn_hdr(g_vm_dyn[i]));
    g_vm_dyn[i] = NULL;
  }
}
