#pragma once

#include "cmd_errors.h"
#include "errors.h"
#include "utils.h"

/**
 * @file cmd.h
 * @brief What a command handler is (CMD.MD), with no transport and no routing: the handler, the direction, the answer
 * and the frame size every limit comes from. The router and the device registry both build on it and don't know each
 * other, so a device compiles without the router.
 * @usage static err_h my_read(void* ctx, cmd_in_t in, cmd_out_t* out) { return cmd_reply(out, &v, sizeof(v)); }
 */

#ifdef __cplusplus
extern "C" {
#endif

/* The frame size (CMD.MD "Frame size"): one for every link, buffer and stored frame, the same on the board and the
   client, so nothing is sized per link. */
//#ref-defines cmd_frame @alias Frame size @prefix CMD_
#define CMD_FRAME_MAX 512u  //@alias Largest frame @description Bytes of a whole frame, its header included
#define CMD_HEAD      5u    //@alias Frame header @description Bytes a frame keeps for its header in front of its data

// What a frame carries after its header: a request's command, an answer's data, a stream frame's [connector][payload].
#define CMD_DATA_MAX (CMD_FRAME_MAX - CMD_HEAD)

/** @brief A handler's input: the router passes [code][args], the device registry the args only. */
typedef struct {
  const uint8_t* data;
  uint32_t len;
} cmd_in_t;

/** @brief A handler's answer buffer. */
typedef struct {
  uint8_t* data;
  uint32_t cap;  /**< What the answer may hold: what cmd_reply() fills at most; longer data is paged by its command
                      (CMD.MD "The handler") */
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
 * mirror, CMD.MD). A write answers no data unless its command says otherwise.
 */
//#ref-enum @alias Command Direction
typedef enum {
  CMD_DIR_READ = 0,   //@alias Read @description Asks for a value; changes nothing
  CMD_DIR_WRITE = 1,  //@alias Write @description Sets a value or runs an action
} cmd_dir_e;

/**
 * @brief The side of a command that `dir` asks for (NULL `read` / `write`: the command has none).
 * @return NULL when found; ERR_CMD_DIRECTION for a missing side or a dir that is neither read nor write.
 */
MUST_USE err_h cmd_side(cmd_f read, cmd_f write, uint8_t dir, cmd_f* fn);

/**
 * @brief Writes the whole answer.
 * @return NULL when written; ERR_CMD_OUT_FULL (more than out->cap).
 * @usage return cmd_reply(out, &value, sizeof(value));
 */
MUST_USE err_h cmd_reply(cmd_out_t* out, const void* data, uint32_t len);

/**
 * @brief Appends to the answer (out->len grows): an answer built in pieces.
 * @return NULL when appended; ERR_CMD_OUT_FULL.
 * @usage ERR_TRY(cmd_append(out, &head, sizeof(head))); ERR_TRY(cmd_append(out, name, name_len));
 */
MUST_USE err_h cmd_append(cmd_out_t* out, const void* data, uint32_t len);

#ifdef __cplusplus
}
#endif
