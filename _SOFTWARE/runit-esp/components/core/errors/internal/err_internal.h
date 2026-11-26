#pragma once

/**
 * @file err_internal.h
 * @brief What only the errors component's own files share (PRIV_INCLUDE_DIRS): the node's layout, a tag's parts, the
 * tag registry's lookup and the pool. A host test that must reach inside a node (a cyclic chain, the pool) includes
 * it too; nothing else does.
 */

#include "errors.h"
#include "sdkconfig.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief One error in a chain (err_h). `next` points to the cause (the deeper error), NULL at the root cause. `tag` is
 * ERR_TAG(module, code); its severity is the tag's (err_sev). `data` is the tag's payload, stored right after the
 * node, ERR_DATA_MAX bytes. `info` (where enabled) is NULL only for the static ERR_POOL_EMPTY node and for a node
 * parsed from a record.
 *
 * Packed - no padding anywhere, `data` starts right after `tag`. aligned(4): a node is a pool block.
 */
struct __attribute__((packed, aligned(4))) err {
#if CONFIG_ERRORS_INFO
  const err_info_t* info;
#endif
  err_h next;
  uint32_t tag;
  uint8_t data[];
};

_Static_assert(sizeof(struct err) == ERR_HDR_LEN, "ERR_HDR_LEN is not the node's header");

/** @brief The module of a tag (ERR_TAG): its high 16 bits. */
#define ERR_MODULE(tag) ((tag) >> 16)

/** @brief The code of a tag within its module (ERR_TAG): its low 16 bits. */
#define ERR_CODE(tag) ((uint16_t)(tag))

/* ----------------------------------------------------------------------------
 * The tag registry (err_tags.c)
 * ------------------------------------------------------------------------- */

/**
 * @brief The info of a tag from the registered tables.
 * @param tag  The tag.
 * @return Its name, payload size and severity; NULL for a tag no registered module has.
 */
const err_tag_info_t* err_tag_info(uint32_t tag);

/**
 * @brief A registered module.
 * @param id  The module's number.
 * @return Its table, NULL when no module with that number is registered.
 */
const err_module_t* err_module(uint16_t id);

/* ----------------------------------------------------------------------------
 * The pool (err_pool.c): CONFIG_ERRORS_POOL_BLOCKS blocks of CONFIG_ERRORS_POOL_BLOCK_SIZE bytes, static. One node is
 * one block, freed in any order. Safe from tasks and ISRs, on both cores (atomics, no lock).
 * ------------------------------------------------------------------------- */

/**
 * @brief Takes one block. Its contents are not cleared (err__new fills the whole node).
 * @return The block, aligned to 4; NULL when the pool is empty.
 */
err_h err_pool_alloc(void);

/**
 * @brief Returns one block: only this node, not its `next`. A pointer that is not a pool block and a block already
 * free assert; where asserts are off they are ignored, and the pool stays whole.
 * @param e  The block, may be NULL (ignored).
 */
void err_pool_free(err_h e);

#ifdef __cplusplus
}
#endif
