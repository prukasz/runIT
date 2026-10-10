#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "errors.h"
#include "utils.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief What a command handler is, with no transport and no routing: the
 * handler type, the answer helper and the errors every handler raises for a
 * bad payload (CMD.MD). The router (key tables, links, dispatch) and the
 * device registry (op tables) both build on it and don't know each other, so a
 * device compiles without the router.
 *
 * @param ctx     What the caller hands down: the router's ctx (NULL from the
 *                root), or a device's struct from the registry.
 * @param in      The input the handler is called with (what that is is the
 *                caller's convention: the router passes its key first, the
 *                device registry passes the args only).
 * @param in_len  Bytes in `in`.
 * @param out     Answer buffer.
 * @param out_cap Size of `out`.
 * @param out_len Bytes written to `out` (set on success; untouched = empty answer).
 * @return NULL on success, otherwise the error chain.
 */
typedef MUST_USE err_h (*cmd_f)(void* ctx, const uint8_t* in, uint32_t in_len, uint8_t* out, uint32_t out_cap, uint32_t* out_len);

// The parameters of a cmd_f, for the macros that declare or write one.
#define CMD_OUT_PARAMS uint8_t *out, uint32_t out_cap, uint32_t *out_len
#define CMD_F_PARAMS void *ctx, const uint8_t *in, uint32_t in_len, CMD_OUT_PARAMS

/* ----------------------------------------------------------------------------
 * Errors (module ERR_MOD_CMD)
 * ------------------------------------------------------------------------- */

// clang-format off
#define ERR_CMD_MAP(X)                                                                                    \
  X(ERR_CMD_BAD_LEN,  ERR_TAG(ERR_MOD_CMD, 1), ERR_SEV_HIGH, { uint16_t got; uint16_t need; }) /* payload size */ \
  X(ERR_CMD_OUT_FULL, ERR_TAG(ERR_MOD_CMD, 2), ERR_SEV_HIGH, { uint16_t need; uint16_t cap; }) /* answer does not fit the link */ \
  X(ERR_CMD_BAD_ARG,  ERR_TAG(ERR_MOD_CMD, 3), ERR_SEV_HIGH, {}) /* invalid argument value */
// clang-format on

ERR_TAGS_DECLARE(ERR_CMD_MAP)

/**
 * @brief Writes the answer to `out`.
 * @return ERR_CMD_OUT_FULL if it does not fit.
 * @usage return cmd_reply(out, out_cap, out_len, &value, sizeof(value));
 */
MUST_USE err_h cmd_reply(uint8_t* out, uint32_t out_cap, uint32_t* out_len, const void* data, uint32_t len);

#ifdef __cplusplus
}
#endif
