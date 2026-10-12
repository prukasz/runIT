#pragma once

#include "errors.h"
#include "sdkconfig.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Bounded pool of fixed-size blocks for error nodes. One node = one
 * block, freed in any order. Safe to call from tasks and ISRs.
 */

/**
 * @brief Takes one block. Contents are not cleared.
 * @return The block, or NULL when the pool is empty.
 */
err_h err_pool_alloc(void);

/**
 * @brief Returns one block (only this node, not its `next`). NULL is ignored.
 * Asserts on a pointer that is not a pool block, and on double free.
 */
void err_pool_free(err_h e);

#ifdef __cplusplus
}
#endif
