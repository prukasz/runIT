#pragma once

#include "errors.h"
#include "sdkconfig.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Shared between the errors component's own files only. */

/** @brief The info of a tag from the registered tables, or NULL for an unknown tag. */
const err_tag_info_t* err_tag_info(uint32_t tag);

/** @brief A registered module by ID, or NULL. */
const err_module_t* err_module(uint16_t id);

/** @brief err_log with a context line first (the command that failed), at the chain's level. NULL context = err_log. */
void err_log_ctx(err_h chain, const char* context);

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
