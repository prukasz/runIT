#include "vm_alloc.h"

#define OWNER OWNER_VM_ALLOC

void* vm_alloc(vm_alloc_t* a, uint32_t size, uint32_t align) {
  uint32_t aligned = (a->offset + (align - 1)) & ~(align - 1);
  if (aligned + size > a->capacity) {
    SE_EMIT_ERR(ERR_VM_ALLOC_EXHAUSTED, .requested = size, .remaining = a->capacity - a->offset);
    return NULL;
  }
  a->offset = aligned + size;
  return a->base + aligned;
}
