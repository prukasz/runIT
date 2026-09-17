#include "sys_error.h"
#include <string.h>

// -----------------------------------------------------------------------------
// Compile-time Validations & Constants
// -----------------------------------------------------------------------------

#define ERR_BUF_SIZE 2048

_Static_assert(ERR_MAX_COUNT <= 0xFFFF, "err_tag_e no longer fits the uint16 tag field of the error packet");

#define X_CHK(tag, level, struct_def)                                                                                                   \
  _Static_assert(sizeof(sys_err_t) + sizeof(err_payload_##tag##_t) <= ERR_BUF_SIZE / 4, #tag " payload too large for the error ring"); \
  SYS_ERROR_MAP(X_CHK)
#undef X_CHK

#define X_OWNER_CHK(tag, id, name)                                                                            \
  _Static_assert((id) <= 0xFFFF, #tag " owner id no longer fits the uint16 owner field of the error packet"); \
  SYS_OWNER_MAP(X_OWNER_CHK)
#undef X_OWNER_CHK

// -----------------------------------------------------------------------------
// Metadata & Name Lookup
// -----------------------------------------------------------------------------

const char* SE_get_owner_name(uint32_t owner) {
  switch (owner) {
#define X_OWNER_CASE(tag, id, name) \
  case id:                          \
    return name;
    SYS_OWNER_MAP(X_OWNER_CASE)
#undef X_OWNER_CASE
    default:
      return "OWNER_UNKNOWN";
  }
}

const char* SE_get_tag_name(err_tag_e tag) {
  switch (tag) {
#define X_TAG_CASE(tag_name, level, struct_def) \
  case tag_name:                                \
    return #tag_name;
    SYS_ERROR_MAP(X_TAG_CASE)
#undef X_TAG_CASE
    default:
      return "TAG_UNKNOWN";
  }
}

size_t SE_get_payload_size(err_tag_e tag) {
  switch (tag) {
#define X_TAG_SIZE(tag_name, level, struct_def) \
  case tag_name:                                \
    return sizeof(err_payload_##tag_name##_t);
    SYS_ERROR_MAP(X_TAG_SIZE)
#undef X_TAG_SIZE
    default:
      return 0;
  }
}

sys_device_err_level_e SE_get_tag_level(err_tag_e tag) {
  switch (tag) {
#define X_TAG_LEVEL(tag_name, level, struct_def) \
  case tag_name:                                 \
    return level;
    SYS_ERROR_MAP(X_TAG_LEVEL)
#undef X_TAG_LEVEL
    default:
      return SYS_DEV_ERR_LOW;
  }
}

// -----------------------------------------------------------------------------
// Ring Buffer Allocator & Chain Traversal
// -----------------------------------------------------------------------------

static uint8_t  err_buffer[ERR_BUF_SIZE] __attribute__((aligned(8)));
static uint32_t head_idx = 0;

bool SE_is_valid_error_ptr(err_h err) {
  if (err == NULL) return false;
  uintptr_t p     = (uintptr_t)err;
  uintptr_t start = (uintptr_t)err_buffer;
  uintptr_t end   = start + ERR_BUF_SIZE - sizeof(sys_err_t);

  return (p >= start && p <= end && (p & 7u) == 0);
}

err_h SE_get_error_root(err_h error) {
  if (!SE_is_valid_error_ptr(error)) {
    return NULL;
  }

  err_h    root  = error;
  uint32_t depth = 0;

  while (root->next_cause) {
    err_h next = root->next_cause;

    // Detect runaway depth or multi-node cycles
    if (++depth >= SE_MAX_CHAIN_DEPTH) {
      return NULL;
    }

    // Detect self-referencing loops
    if (next == root) {
      return NULL;
    }

    // Prevent Out-Of-Bounds (OOB) memory reads if pointer was corrupted / overwritten
    if (!SE_is_valid_error_ptr(next)) {
      return NULL;
    }

    root = next;
  }

  return root;
}

err_h SE_alloc_bytes(size_t payload_size, err_tag_e tag, uint32_t owner) {
  uint32_t total_size = sizeof(sys_err_t) + payload_size;
  total_size          = (total_size + 7) & ~7u;  // align to 8 bytes

  uint32_t old_head, new_head, alloc_idx;
  do {
    old_head = __atomic_load_n(&head_idx, __ATOMIC_RELAXED);

    if (old_head + total_size > ERR_BUF_SIZE) {
      alloc_idx = 0;
      new_head  = total_size;
    } else {
      alloc_idx = old_head;
      new_head  = old_head + total_size;
    }
  } while (!__atomic_compare_exchange_n(&head_idx, &old_head, new_head, false, __ATOMIC_RELAXED, __ATOMIC_RELAXED));

  err_h err = (err_h)&err_buffer[alloc_idx];
  memset(err, 0, sizeof(sys_err_t));  // payload is zero-filled by the compound-literal assignment
  err->tag        = tag;
  err->owner      = owner;
  err->next_cause = NULL;

  return err;
}

// -----------------------------------------------------------------------------
// Suspend / Resume Processing
// -----------------------------------------------------------------------------

static volatile int8_t s_suspend_depth = 0;

// Nesting-safe: a suspended section may call into another function that
// also suspends/resumes without prematurely re-enabling error reporting.
void SE_suspend(void) {
  s_suspend_depth++;
}

void SE_resume(void) {
  if (s_suspend_depth > 0) {
    s_suspend_depth--;
  }
}

bool SE_is_suspended(void) {
  return s_suspend_depth > 0;
}

