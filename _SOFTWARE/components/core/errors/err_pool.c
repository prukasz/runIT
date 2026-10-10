#include "err_pool.h"
#include <assert.h>
#include <stdbool.h>
#include "utils.h"

#define BLOCKS CONFIG_ERRORS_POOL_BLOCKS
#define BLOCK_SIZE CONFIG_ERRORS_POOL_BLOCK_SIZE

_Static_assert(BLOCK_SIZE % 4 == 0, "ERRORS_POOL_BLOCK_SIZE must be a multiple of 4");
_Static_assert(BLOCK_SIZE > ERR_HDR_LEN, "ERRORS_POOL_BLOCK_SIZE leaves no room for data");

// A free block holds only the link to the next free block.
typedef struct free_blk {
  struct free_blk* next;
} free_blk_t;

static uint8_t s_pool[BLOCKS][BLOCK_SIZE] __attribute__((aligned(4)));
static free_blk_t* s_free;  // blocks returned by err_pool_free
static uint32_t s_next_new;  // blocks [s_next_new, BLOCKS) were never handed out
static uint32_t s_used[(BLOCKS + 31) / 32];  // 1 bit per block, catches double free
static U_SPINLOCK_DEFINE(s_lock);

err_h err_pool_alloc(void) {
  uint8_t* blk = NULL;

  U_CRITICAL_ENTER_SAFE(s_lock);
  if (s_free != NULL) {
    blk = (uint8_t*)s_free;
    s_free = s_free->next;
  } else if (s_next_new < BLOCKS) {
    blk = s_pool[s_next_new++];
  }
  if (blk != NULL) {
    uint32_t i = (uint32_t)(blk - &s_pool[0][0]) / BLOCK_SIZE;
    s_used[i / 32] |= 1u << (i % 32);
  }
  U_CRITICAL_EXIT_SAFE(s_lock);

  return (err_h)blk;
}

void err_pool_free(err_h e) {
  if (e == NULL) {
    return;
  }

  // Unsigned wrap makes a pointer below the pool fail the range check too.
  uintptr_t off = (uintptr_t)e - (uintptr_t)s_pool;
  bool in_pool = off < sizeof(s_pool) && off % BLOCK_SIZE == 0;
  assert(in_pool && "not an error pool block");
  if (!in_pool) {
    return;
  }

  uint32_t i = (uint32_t)(off / BLOCK_SIZE);
  uint32_t bit = 1u << (i % 32);
  bool was_used;

  U_CRITICAL_ENTER_SAFE(s_lock);
  was_used = (s_used[i / 32] & bit) != 0;
  if (was_used) {
    s_used[i / 32] &= ~bit;
    free_blk_t* blk = (free_blk_t*)(void*)e;
    blk->next = s_free;
    s_free = blk;
  }
  U_CRITICAL_EXIT_SAFE(s_lock);

  assert(was_used && "error pool double free");
}
