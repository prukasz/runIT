#pragma once

#include <stdint.h>
#include <string.h>
#include "errors.h"
#include "cmd_errors.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file cmd.h
 * @brief Shared command contract for module routers, device drivers and contracts (CMD.MD):
 * the wire frame size every limit comes from, the handler signature, the direction, and the inline reply helpers.
 * Non-routing consumers (cmd_table.h, sys_device, drivers) include this header without pulling router.h.
 * @usage static err_h my_read(void* ctx, cmd_in_t in, cmd_out_t* out) { return cmd_reply(out, &v, sizeof(v)); }
 */

/* The frame size (ROUTER.MD "Frame size"): one for every link, buffer and stored frame, the same on the board and the
   client, so nothing is sized per link. */
//#ref-defines cmd_frame @alias Frame size @prefix CMD_
#define CMD_FRAME_MAX 512u  //@alias Largest frame @description Bytes of a whole frame, its header included
#define CMD_HEAD      5u    //@alias Frame header @description Bytes a frame keeps for its header in front of its data

// What a frame carries after its header: a request's command, an answer's data, a PUSH frame's [stream id or channel][payload].
#define CMD_DATA_MAX (CMD_FRAME_MAX - CMD_HEAD)

/** @brief A handler's input: the payload only (an op's args). The router strips [dir][code], the device registry the
 * op; a module router (a root entry) gets its whole command, its key first. */
typedef struct {
  const uint8_t* data;
  uint32_t len;
} cmd_in_t;

/** @brief A handler's answer buffer. */
typedef struct {
  uint8_t* data;
  uint32_t cap;  /**< What the answer may hold: what cmd_reply() fills at most; longer data is paged by its command
                      (ROUTER.MD "The handler") */
  uint32_t len;  /**< Written by the handler; 0 = empty answer */
} cmd_out_t;

/**
 * @brief A command handler, the same for the router's modules and the devices' ops.
 * @param ctx  NULL from the router root; the device's struct from the registry.
 * @param in   The input.
 * @param out  The answer.
 * @return NULL on success, otherwise the error chain.
 */
typedef MUST_USE err_h (*cmd_f)(void* ctx, cmd_in_t in, cmd_out_t* out);

#define CMD_F_PARAMS void *ctx, cmd_in_t in, cmd_out_t *out  // for the macros that write a cmd_f

/**
 * @brief Which side of a command a request runs: the byte after the root key of every request (ROUTER.MD "Wire
 * format"). A command has a read side, a write side or both; with both, the read answers what the write takes (the
 * mirror, ROUTER.MD). A write answers no data unless its command says otherwise.
 */
//#ref-enum @alias Command Direction
typedef enum {
  CMD_DIR_READ = 0,   //@alias Read @description Asks for a value; changes nothing
  CMD_DIR_WRITE = 1,  //@alias Write @description Sets a value or runs an action
} cmd_dir_e;

/**
 * @brief Writes the whole answer.
 * @param[out] out  The answer
 * @param[in]  data The bytes
 * @param[in]  len  Number of bytes
 * @return NULL when written; ERR_CMD_OUT_FULL (more than out->cap).
 * @usage return cmd_reply(out, &value, sizeof(value));
 */
static inline MUST_USE err_h cmd_reply(cmd_out_t* out, const void* data, uint32_t len) {
  ERR_CHECK(len <= out->cap, ERR_CMD_OUT_FULL, .need = (uint16_t)len, .cap = (uint16_t)out->cap);
  if (len > 0) {
    memcpy(out->data, data, len);
  }
  out->len = len;
  return NULL;
}

/**
 * @brief Appends to the answer (out->len grows): an answer built in pieces.
 * @param[in,out] out  The answer
 * @param[in]     data The bytes
 * @param[in]     len  Number of bytes
 * @return NULL when appended; ERR_CMD_OUT_FULL.
 * @usage ERR_TRY(cmd_append(out, &head, sizeof(head))); ERR_TRY(cmd_append(out, name, name_len));
 */
static inline MUST_USE err_h cmd_append(cmd_out_t* out, const void* data, uint32_t len) {
  ERR_CHECK(out->len + len <= out->cap, ERR_CMD_OUT_FULL, .need = (uint16_t)(out->len + len),
            .cap = (uint16_t)out->cap);
  if (len > 0) {
    memcpy(out->data + out->len, data, len);
  }
  out->len += len;
  return NULL;
}

#ifdef __cplusplus
}
#endif
