#include "sys_error.h"
#include <sdkconfig.h>
#include <stdint.h>

// -----------------------------------------------------------------------------
// Compile-time Validations & Constants
// -----------------------------------------------------------------------------

#define ERR_BLOCK_COUNT (CONFIG_SYS_ERRORS_BUF_SIZE / CONFIG_SYS_ERRORS_BLOCK_SIZE)

_Static_assert(ERR_MAX_COUNT <= 0xFFFF, "err_tag_e no longer fits the uint16 tag field of the error packet");

#define X_CHK(tag, level, struct_def)                                                                                                                                                \
  _Static_assert(sizeof(err_payload_##tag##_t) <= UINT8_MAX && sizeof(sys_err_t) + sizeof(err_payload_##tag##_t) <= CONFIG_SYS_ERRORS_BUF_SIZE / 4, #tag " payload too large for the error pool"); \
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
// Bounded Pool Allocator & Chain Traversal
// -----------------------------------------------------------------------------

// 32 allocation bits plus one length byte per possible node start. Ownership is
// exclusive: wrapping transfers the cause; push consumes; diagnostics borrow.
static uint8_t  err_buffer[CONFIG_SYS_ERRORS_BUF_SIZE] __attribute__((aligned(8)));
static uint32_t allocated;
static uint8_t  block_count[ERR_BLOCK_COUNT];
// Immutable fallback node used when the error ring pool is exhausted.
// Defined as a raw aligned buffer so clang does not diagnose a flexible array member
// nested inside an anonymous struct.
static const struct {
  err_tag_e tag;
  uint32_t  owner;
  struct err_node* next_cause;
  uint8_t   payload[8];
} exhausted __attribute__((aligned(8))) = {
  .tag = ERR_BASE_NO_MEM,
  .owner = OWNER_SYS_ERRORS_BASE,
  .next_cause = NULL,
  .payload = {0},
};
static int32_t node_index(err_h err) {
  uintptr_t offset = (uintptr_t)err - (uintptr_t)err_buffer;
  return offset < CONFIG_SYS_ERRORS_BUF_SIZE && offset % CONFIG_SYS_ERRORS_BLOCK_SIZE == 0 ? (int32_t)(offset / CONFIG_SYS_ERRORS_BLOCK_SIZE) : -1;
}

bool SE_is_valid_error_ptr(err_h err) {
  if (err == (err_h)&exhausted) return true;
  int32_t i = node_index(err);
  if (i < 0 || !__atomic_load_n(&block_count[i], __ATOMIC_ACQUIRE)) return false;
  return (uint32_t)err->tag < ERR_MAX_COUNT && sizeof(sys_err_t) + SE_get_payload_size(err->tag) <= (size_t)block_count[i] * CONFIG_SYS_ERRORS_BLOCK_SIZE;
}

// Returns the valid, unique prefix; complete distinguishes truncation/corruption.
size_t SE_collect_chain(err_h chain, err_h nodes[], bool* complete) {
  size_t count = 0;
  while (chain && count < SE_MAX_CHAIN_DEPTH && SE_is_valid_error_ptr(chain)) {
    for (size_t i = 0; i < count; ++i) {
      if (nodes[i] == chain) goto done;
    }
    nodes[count++] = chain;
    chain          = chain->next_cause;
  }
done:
  if (complete) *complete = chain == NULL;
  return count;
}

err_h SE_get_error_root(err_h error) {
  err_h  nodes[SE_MAX_CHAIN_DEPTH];
  bool   complete;
  size_t count = SE_collect_chain(error, nodes, &complete);
  return complete && count ? nodes[count - 1] : NULL;
}

err_h SE_alloc_bytes(size_t payload_size, err_tag_e tag, uint32_t owner) {
  if ((uint32_t)tag >= ERR_MAX_COUNT || payload_size != SE_get_payload_size(tag) || payload_size > CONFIG_SYS_ERRORS_BUF_SIZE - sizeof(sys_err_t)) return NULL;
  uint32_t blocks = (sizeof(sys_err_t) + payload_size + CONFIG_SYS_ERRORS_BLOCK_SIZE - 1) / CONFIG_SYS_ERRORS_BLOCK_SIZE;
  uint32_t bits   = blocks == 32 ? UINT32_MAX : (1u << blocks) - 1u;
  for (uint32_t i = 0; i + blocks <= ERR_BLOCK_COUNT; ++i) {
    uint32_t mask = bits << i;
    uint32_t old  = __atomic_load_n(&allocated, __ATOMIC_RELAXED);
    while (!(old & mask)) {
      if (__atomic_compare_exchange_n(&allocated, &old, old | mask, false, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED)) {
        err_h node = (err_h)&err_buffer[i * CONFIG_SYS_ERRORS_BLOCK_SIZE];
        memset(node, 0, blocks * CONFIG_SYS_ERRORS_BLOCK_SIZE);
        node->tag   = tag;
        node->owner = owner;
        __atomic_store_n(&block_count[i], blocks, __ATOMIC_RELEASE);
        return node;
      }
    }
  }
  return NULL;
}

err_h SE_new_error(err_tag_e tag, uint32_t owner, const void* payload, size_t size, err_h cause) {
  err_h node = SE_alloc_bytes(size, tag, owner);
  // Never convert failure to success or overwrite a live chain on exhaustion.
  if (!node) return cause ? cause : (err_h)&exhausted;
  memcpy(node->payload, payload, size);
  node->next_cause = cause;
  return node;
}

void SE_release(err_h chain) {
  if (!chain || chain == (err_h)&exhausted) return;
  uint32_t starts = 0, reservation = 0;
  while (chain) {
    int32_t i = node_index(chain);
    if (i < 0 || (starts & (1u << i))) break;
    uint32_t blocks = __atomic_load_n(&block_count[i], __ATOMIC_ACQUIRE);
    if (!blocks) break;
    starts |= 1u << i;
    reservation |= (blocks == 32 ? UINT32_MAX : (1u << blocks) - 1u) << i;
    chain = chain->next_cause;
  }
  for (uint32_t i = 0; i < ERR_BLOCK_COUNT; ++i) {
    if (starts & (1u << i)) __atomic_store_n(&block_count[i], 0, __ATOMIC_RELEASE);
  }
  __atomic_fetch_and(&allocated, ~reservation, __ATOMIC_RELEASE);
}

// -----------------------------------------------------------------------------
// Suspend / Resume Processing
// -----------------------------------------------------------------------------

static __thread uint32_t s_suspend_depth;

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

// FNV-1a over ordered tag names and payload sizes: reject mismatched client maps.
uint32_t SE_schema_id(void) {
  uint32_t hash = 2166136261u;
  for (uint32_t tag = 0; tag < ERR_MAX_COUNT; ++tag) {
    const char* name = SE_get_tag_name(tag);
    do {
      hash = (hash ^ (uint8_t)*name) * 16777619u;
    } while (*name++);
    hash = (hash ^ (uint32_t)SE_get_payload_size(tag)) * 16777619u;
  }
  return hash;
}
