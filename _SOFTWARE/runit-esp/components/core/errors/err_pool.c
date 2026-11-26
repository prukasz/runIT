/**
 * @file err_pool.c
 * @brief The pool of error nodes (err_internal.h): static blocks and one bit per block, set while the block is out. A
 * block is taken by setting its bit (compare and swap) and returned by clearing it, which also catches a double free.
 * No lock: safe from tasks and ISRs on both cores.
 */

#include <assert.h>
#include "err_internal.h"
#include "sdkconfig.h"

#define BLOCKS     CONFIG_ERRORS_POOL_BLOCKS
#define BLOCK_SIZE CONFIG_ERRORS_POOL_BLOCK_SIZE
#define WORDS      ((BLOCKS + 31) / 32)

_Static_assert(BLOCK_SIZE % 4 == 0, "ERRORS_POOL_BLOCK_SIZE must be a multiple of 4");
_Static_assert(BLOCK_SIZE > ERR_HDR_LEN, "ERRORS_POOL_BLOCK_SIZE leaves no room for data");

static uint8_t s_blocks[BLOCKS][BLOCK_SIZE] __attribute__((aligned(4)));
static uint32_t s_used[WORDS];  // bit i of word w: block w * 32 + i is out; changed with atomics only

// The bits of word `w` that stand for a block: the last word may have fewer than 32.
static uint32_t blocks_of(uint32_t w) {
  return (w == WORDS - 1u && BLOCKS % 32u != 0u) ? (1u << (BLOCKS % 32u)) - 1u : UINT32_MAX;
}

err_h err_pool_alloc(void) {
  for (uint32_t w = 0; w < WORDS; w++) {
    uint32_t used = __atomic_load_n(&s_used[w], __ATOMIC_RELAXED);
    uint32_t free_bits;
    while ((free_bits = ~used & blocks_of(w)) != 0u) {
      const uint32_t bit = free_bits & (0u - free_bits);  // the lowest free one
      // A failed swap leaves the word's new value in `used`. Acquire: after the last owner's writes to the block.
      if (__atomic_compare_exchange_n(&s_used[w], &used, used | bit, false, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED)) {
        return (err_h)(void*)s_blocks[w * 32u + (uint32_t)__builtin_ctz(bit)];
      }
    }
  }
  return NULL;
}

void err_pool_free(err_h e) {
  if (e == NULL) {
    return;
  }

  // Unsigned wrap makes a pointer below the pool fail the range check too.
  uintptr_t off = (uintptr_t)e - (uintptr_t)s_blocks;
  bool in_pool = off < sizeof(s_blocks) && off % BLOCK_SIZE == 0;
  assert(in_pool && "not an error pool block");
  if (!in_pool) {
    return;
  }

  const uint32_t i = (uint32_t)(off / BLOCK_SIZE);
  const uint32_t bit = 1u << (i % 32u);
  const bool was_used = (__atomic_fetch_and(&s_used[i / 32u], ~bit, __ATOMIC_RELEASE) & bit) != 0u;

  assert(was_used && "error pool double free");
  (void)was_used;
}
