#pragma once
#include <stddef.h>
#include <stdint.h>
#include "sys_error.h"     // err_h return type for vm_alloc()'s prototype
#include "sys_error_vm.h"  // ERR_VM_*/OWNER_VM_* -- not used in this header itself, kept
                            // so anything that only includes this header still gets them

/*
Linear (bump) allocator. One backing buffer, handed out sequentially, never
freed piecemeal -- matches everything else here: a loaded program's storage
is sized once and built once. To replace a program, reset and rebuild from
scratch; there is no per-object free.

The allocator doesn't own or care where `mem` came from (static buffer,
one-shot malloc, pool) -- it only manages carving it up.
*/

typedef struct {
  uint8_t* base;
  uint32_t capacity;
  uint32_t offset;
} vm_alloc_t;

static inline void vm_alloc_init(vm_alloc_t* a, void* mem, uint32_t capacity) {
  a->base = (uint8_t*)mem;
  a->capacity = capacity;
  a->offset = 0;
}

static inline void vm_alloc_reset(vm_alloc_t* a) {
  a->offset = 0;
}

// bump-allocate `size` bytes aligned to `align` (must be a power of 2).
// NULL if the arena doesn't have room -- caller must check, nothing here
// grows the buffer. Exhaustion is reported via sys_errors (OWNER_VM_ALLOC)
// so a failed program load is traceable/telemetered, not just a silent NULL.
// Only called at program-load time (not per scan cycle), so it's defined in
// vm_alloc.c with an ambient `#define OWNER OWNER_VM_ALLOC` rather than
// inline here -- see that file.
void* vm_alloc(vm_alloc_t* a, uint32_t size, uint32_t align);

// 4-byte alignment is the default everywhere else in this system (object
// payloads, accessor/block nodes) -- shorthand for the common case.
static inline void* vm_alloc4(vm_alloc_t* a, uint32_t size) {
  return vm_alloc(a, size, 4);
}

static inline uint32_t vm_alloc_used(const vm_alloc_t* a) {
  return a->offset;
}

static inline uint32_t vm_alloc_remaining(const vm_alloc_t* a) {
  return a->capacity - a->offset;
}
