#include "vm_obj_dyn.h"
#include <string.h>
#include "esp_heap_caps.h"

#define OWNER OWNER_VM_OBJ

/* Same caps as the program pool: internal DRAM only. A message tree is walked
   by accessors on every pass that reads it, so putting it behind the PSRAM
   cache would make access cost depend on what else is resident. */
#define VM_DYN_HEAP_CAPS (MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL)

/* Deepest release() will recurse. A tree arrives off the wire, so nothing
   stops a malformed one nesting until the stack is gone -- the same reason
   VM_ACCESSOR_MAX_DEPTH exists. Past this the remaining children are left to
   vm_obj_dyn_reset(), which frees them without recursing at all. */
#define VM_DYN_MAX_DEPTH 16

vm_obj_dyn_meta_t g_vm_dyn[VM_DYN_MAX] = {0};

err_h vm_obj_dyn_create(vm_obj_h* out, const vm_obj_head_t* head, const char* name) {
  SE_CHECK_NOT_NULL(out);
  *out = NULL;

  /* Shape is validated before a slot is taken or a byte allocated, so a
     rejected object costs neither -- the same rule the arena path follows. */
  uint32_t total = 0;
  SE_RET_IF_ERR(vm_obj_shape(head, &total));

  uint16_t slot = VM_DYN_MAX;
  for (uint16_t i = 0; i < VM_DYN_MAX; i++) {
    if (!g_vm_dyn[i].obj) {
      slot = i;
      break;
    }
  }
  if (slot == VM_DYN_MAX) {
    SE_RET_ERR(ERR_VM_DYN_FULL, .limit = VM_DYN_MAX);
  }

  // zeroed for the same reason arena objects are: payload and name must read
  // as empty rather than as whatever the heap last held
  vm_obj_h o = (vm_obj_h)heap_caps_calloc(1, total, VM_DYN_HEAP_CAPS);
  if (!o) {
    SE_RET_ERR(ERR_BASE_NO_MEM, 0);
  }

  vm_obj_init(o, head, name);
  o->head.f.dynamic = 1;  // the one field the shared init cannot set

  g_vm_dyn[slot].obj = o;
  g_vm_dyn[slot].ref_cnt = 0;  // owned by nothing until something links it

  *out = o;
  return NULL;
}

void vm_obj_dyn_retain(vm_obj_h o) {
  // no-op on an arena object, so link paths need no type test of their own
  uint16_t id = vm_obj_dyn_id(o);
  if (id != VM_DYN_NO_ID) g_vm_dyn[id].ref_cnt++;
}

static void dyn_release(vm_obj_h o, uint8_t depth) {
  uint16_t id = vm_obj_dyn_id(o);
  if (id == VM_DYN_NO_ID) return;  // arena object, or already gone

  if (g_vm_dyn[id].ref_cnt > 1) {
    g_vm_dyn[id].ref_cnt--;
    return;
  }

  /* Last reference. The slot is cleared *before* recursing, which is what stops
     a cycle: a child that links back here finds no entry and returns instead of
     re-entering a half-freed object. */
  g_vm_dyn[id].obj = NULL;
  g_vm_dyn[id].ref_cnt = 0;

  /* Children go first -- reading the payload after free() would be a
     use-after-free. Arena children fall out of dyn_release() immediately, so a
     tree holding both kinds needs no test here. */
  if ((vm_obj_t_e)o->head.d.obj_t == VM_OBJ_PTR && depth < VM_DYN_MAX_DEPTH) {
    vm_obj_h* kids = (vm_obj_h*)o->payload;
    uint16_t n = vm_obj_items_cnt(o);
    for (uint16_t i = 0; i < n; i++) {
      if (kids[i]) dyn_release(kids[i], (uint8_t)(depth + 1));
    }
  }

  heap_caps_free(o);
}

void vm_obj_dyn_release(vm_obj_h o) {
  dyn_release(o, 0);
}

void vm_obj_dyn_reset(void) {
  /* Reference counts are ignored and nothing recurses: every live object is in
     the register exactly once, so freeing each entry frees the lot. That is
     what makes a detached cycle, or an object allocated and never linked,
     unable to survive a reload -- neither is reachable from a parent, but both
     are in here. */
  for (uint16_t i = 0; i < VM_DYN_MAX; i++) {
    if (g_vm_dyn[i].obj) heap_caps_free(g_vm_dyn[i].obj);
    g_vm_dyn[i].obj = NULL;
    g_vm_dyn[i].ref_cnt = 0;
  }
}
