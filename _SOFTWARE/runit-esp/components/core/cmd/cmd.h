#pragma once

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

/**
 * @brief Which side of a command a request runs: the byte after the root key of every request (ROUTER.MD "Wire
 * format"). A command has a read side, a write side or both; with both, the read answers what the write takes (the
 * mirror, CMD.MD). A write answers no data unless its command says otherwise.
 */
//#ref-enum @alias Command Direction
typedef enum {
  CMD_DIR_READ = 0,   //@alias Read @description Asks for a value; changes nothing
  CMD_DIR_WRITE = 1,  //@alias Write @description Sets a value or runs an action
} cmd_dir_e;

// The parameters of a cmd_f, for the macros that declare or write one.
#define CMD_OUT_PARAMS uint8_t *out, uint32_t out_cap, uint32_t *out_len
#define CMD_F_PARAMS   void *ctx, const uint8_t *in, uint32_t in_len, CMD_OUT_PARAMS

/* ----------------------------------------------------------------------------
 * Errors (module ERR_MOD_CMD)
 * ------------------------------------------------------------------------- */

#define ERR_CMD_MAP(X)                                                                                    \
  X(ERR_CMD_BAD_LEN,   ERR_TAG(ERR_MOD_CMD, 1), ERR_SEV_HIGH, { uint16_t got; uint16_t need; }) /* payload size */ \
  X(ERR_CMD_OUT_FULL,  ERR_TAG(ERR_MOD_CMD, 2), ERR_SEV_HIGH, { uint16_t need; uint16_t cap; }) /* answer does not fit the link */ \
  X(ERR_CMD_BAD_ARG,   ERR_TAG(ERR_MOD_CMD, 3), ERR_SEV_HIGH, {}) /* invalid argument value */ \
  X(ERR_CMD_DIRECTION, ERR_TAG(ERR_MOD_CMD, 4), ERR_SEV_HIGH, { uint8_t dir; }) /* the command has no such side, or dir is neither read nor write */

ERR_TAGS_DECLARE(ERR_CMD_MAP)

/**
 * @brief The side of a command that `dir` asks for (`read` or `write`, NULL = the command has none).
 * @return ERR_CMD_DIRECTION for a missing side or a dir that is neither CMD_DIR_READ nor CMD_DIR_WRITE.
 */
MUST_USE err_h cmd_side(cmd_f read, cmd_f write, uint8_t dir, cmd_f* fn);

/**
 * @brief Writes the answer to `out`.
 * @return ERR_CMD_OUT_FULL if it does not fit.
 * @usage return cmd_reply(out, out_cap, out_len, &value, sizeof(value));
 */
MUST_USE err_h cmd_reply(uint8_t* out, uint32_t out_cap, uint32_t* out_len, const void* data, uint32_t len);

/**
 * @brief Appends `len` bytes to `out` at `*pos` and advances `*pos`: for an answer built in pieces.
 * @return ERR_CMD_OUT_FULL if they do not fit `cap`.
 * @usage uint32_t pos = 0; ERR_TRY(cmd_append(out, out_cap, &pos, &head, sizeof(head))); *out_len = pos;
 */
MUST_USE err_h cmd_append(uint8_t* out, uint32_t cap, uint32_t* pos, const void* data, uint32_t len);

/**
 * @brief A name that is the whole tail of a command: NUL-terminated, nothing after the NUL.
 * @param tail     The tail bytes, `tail_len` >= 1.
 * @param name     Set to `tail`, as a C string.
 * @return ERR_CMD_BAD_ARG if the last byte is not the only NUL.
 */
MUST_USE err_h cmd_name_arg(const uint8_t* tail, uint32_t tail_len, const char** name);

#ifdef __cplusplus
}
#endif
