#pragma once

#include <stdint.h>
#include <string.h>
#include "cmd_types.h"
#include "errors.h"
#include "cmd_errors.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file cmd.h
 * @brief Shared command contract for module routers, device drivers and contracts (CMD.MD):
 * the wire frame size every limit comes from, the header of every frame (cmd_hdr_t, its type byte, kinds and
 * sources), the handler signature, the direction, and the inline reply helpers.
 * Non-routing consumers (cmd_table.h, sys_device, drivers) include this header without pulling router.h.
 * @usage static err_h my_read(void* ctx, cmd_in_t in, cmd_out_t* out) { return cmd_reply(out, &v, sizeof(v)); }
 */

/* The frame size (ROUTER.MD "Frame size"): one for every link, buffer and stored frame, the same on the board and the
   client, so nothing is sized per link. */
//#ref-defines cmd_frame @alias Frame size @prefix CMD_
#define CMD_FRAME_MAX 512u  //@alias Largest frame @description Bytes of a whole frame, its header included
#define CMD_HEAD      5u    //@alias Frame header @description Bytes of a frame's header (cmd_hdr_t), before its data

// What a frame carries after its header: a request's [dir][code | id + op][args], an answer's data, a PUSH's data.
#define CMD_DATA_MAX (CMD_FRAME_MAX - CMD_HEAD)

/* ----------------------------------------------------------------------------
 * The header of every frame and every packet (CMD.MD "The header"), wire format, never renumbered: what goes on a
 * provider's line and through the data matrix is the same bytes.
 *
 *   [type u8][src u8][key u8][seq u16 LE][data]
 *
 *   type  bits 0-1 the router's flags (ROUTER_FLAG_URGENT, _NOREPLY), bit 2 an answer's status, bits 6-7 the kind
 *   src   who made it: a board, a client; CMD_SRC_NONE until the session that puts it on a line sets its board's
 *   key   a module's number: a request's router key, an answer's request's, a PUSH's stream, MODULE_ID_SESSION for CONTROL
 *   seq   a request's sender's counter, echoed by its answer; a PUSH's count of its class on its link; 0 for CONTROL
 * ------------------------------------------------------------------------- */

//#ref-defines cmd_type @alias Frame type byte @prefix CMD_TYPE_
#define CMD_TYPE_FLAGS  (3u << 0)  //@alias Flags @description Bits 0-1: ROUTER_FLAG_URGENT, ROUTER_FLAG_NOREPLY
#define CMD_TYPE_STATUS (1u << 2)  //@alias Status @description Bit 2: an answer's router_status_e (OK, ERROR)
#define CMD_TYPE_KIND   (3u << 6)  //@alias Kind @description Bits 6-7: cmd_kind_e

/** @brief What a frame carries: (kind << 6) in its type byte. */
//#ref-enum @alias Frame kind
typedef enum {
  CMD_REQUEST = 0,  //@alias Request @description A command; its answer comes back with the same key and seq
  CMD_ANSWER = 1,   //@alias Answer @description The answer to the request seq: its status in the type byte, then data
  CMD_PUSH = 2,     //@alias Push @description A stream's data (key: its module), never answered
  CMD_CONTROL = 3,  //@alias Control @description The session's own: hello, ping, no session
} cmd_kind_e;

#define CMD_TYPE(kind)    ((uint8_t)((kind) << 6))
#define CMD_KIND_OF(type) ((uint8_t)(((type) & CMD_TYPE_KIND) >> 6))

/* Who made a frame (`src`): a board's id is its place in the project (a command sets it, NVS keeps it), a client (the
   app, a test host) picks its own. 0 is none: a board with no id given, a frame no session has put on a line yet. */
//#ref-defines cmd_src @alias Frame source @prefix CMD_SRC_
#define CMD_SRC_NONE         0x00u  //@alias None @description No id: a board none was given to, a frame not yet on a line
#define CMD_SRC_BOARD_FIRST  0x01u  //@alias First board @description Boards are 0x01..0xEF
#define CMD_SRC_BOARD_LAST   0xEFu  //@alias Last board @description The last board id
#define CMD_SRC_CLIENT_FIRST 0xF0u  //@alias First client @description Clients (the app, a test host) are 0xF0..0xFC; each picks its own
#define CMD_SRC_CLIENT_LAST  0xFCu  //@alias Last client @description The last client id
#define CMD_SRC_SELF         0xFEu  //@alias This board @description cmd_req_t.src of the board's own commands (router_inject); never on a line
#define CMD_SRC_ALL          0xFFu  //@alias Every peer @description A listener's mark for every peer; never on a line

/** @brief The header of every frame and packet: CMD_HEAD bytes, packed, little-endian. */
typedef struct __attribute__((packed)) {
  uint8_t type;  /**< Bits 0-1: ROUTER_FLAG_*; bit 2: an answer's status; bits 6-7: cmd_kind_e */
  uint8_t src;   /**< Who made it (CMD_SRC_*) */
  uint8_t key;   /**< A module's number */
  uint16_t seq;  /**< Request: the sender's counter; answer: echoed; push: its class's count on its link; else 0 */
} cmd_hdr_t;

_Static_assert(sizeof(cmd_hdr_t) == CMD_HEAD, "the header is CMD_HEAD");

/* The send flags (CMD_SEND_*): cmd_types.h (utils), with the stream ports that carry them. */

/* A handler's input and answer, cmd_in_t and cmd_out_t: cmd_types.h (utils). */

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
