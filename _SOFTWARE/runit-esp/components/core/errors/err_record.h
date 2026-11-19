#pragma once

/**
 * @file err_record.h
 * @brief The error record (ERRORS.MD "Error record"): a chain as bytes, with what caused it - the same bytes in an
 * ERROR answer, on the errors stream and in the history - and back.
 */

#include <stdint.h>
#include "cmd_types.h"
#include "errors.h"

#ifdef __cplusplus
extern "C" {
#endif

//#ref-enum @alias Error Record Source
typedef enum {
  ERR_RECORD_SRC_INTERNAL = 0,  //@alias Internal @description No request (task, timer, boot); later device ID/task may follow
  ERR_RECORD_SRC_COMMAND = 1,  //@alias Command @description Raised while running a command
  ERR_RECORD_SRC_BOARD = 2,    //@alias Board @description About another board as a whole, no request: the board's id (u8) follows the stamp
} err_record_src_e;

//#ref-defines err_record_flag @alias Error Record Flags @prefix ERR_RECORD_
#define ERR_RECORD_REQ_CUT     0x80u  //@alias Request cut @description Bit 7 of request length: request was cut to fit, more was sent
#define ERR_RECORD_STAMP_BYTES 8u  //@alias Time stamp bytes @description The u64 LE UTC time in ms (the board's clock) right after the source byte; counts from the start while no client has set the clock (before 2020: not a date)

/** @brief An error record's command header (after the stamp): who sent the command, and which one. */
typedef struct __attribute__((packed)) {
  uint8_t from;     /**< cmd_req_t.src: a board or client id; CMD_SRC_SELF for the board itself */
  uint16_t seq;     /**< cmd_req_t.seq: the sender's counter */
  uint8_t flags;    /**< ROUTER_FLAG_* */
  uint8_t req_len;  /**< Bits 0-6: request bytes that follow, bit 7: cut (ERR_RECORD_REQ_CUT) */
} err_record_cmd_t;

_Static_assert(sizeof(err_record_cmd_t) == 5, "record command header");

/**
 * @brief Encodes the record of an internal error (no command) into `buf`: [ERR_RECORD_SRC_INTERNAL][stamp], then the
 * chain, outermost node first: [count u8, bit 7 = cut] and per node [tag u32 LE][len u8][payload] (the packed
 * err_data_<TAG>_t; len 0 for an unknown tag). Nodes that don't fit are left out and bit 7 is set. Does not free the
 * chain.
 * @return Bytes written (0 only when cap is too small for the header).
 */
uint32_t err_record(err_h chain, uint8_t* buf, uint32_t cap);

/**
 * @brief err_record for a chain raised by a command: [ERR_RECORD_SRC_COMMAND][stamp][err_record_cmd_t][request], then
 * the chain. The request is cut to CONFIG_ERRORS_RECORD_REQ_MAX and further when `cap` is small. The bytes of an
 * ERROR answer and of the errors stream.
 * @param cmd    The command (cmd_types.h), or NULL: then the record of an internal error (err_record).
 * @param chain  The chain, outermost node first. Borrowed: not freed.
 * @param buf    The record's buffer.
 * @param cap    Bytes in buf.
 * @return Bytes written (0 only when cap is too small for the header).
 */
uint32_t err_record_cmd(const cmd_req_t* cmd, err_h chain, uint8_t* buf, uint32_t cap);

/**
 * @brief err_record for an error about another board as a whole: [ERR_RECORD_SRC_BOARD][stamp][board u8], then the chain.
 * @param board  The board's id.
 * @return Bytes written (0 only when cap is too small for the header).
 */
uint32_t err_record_board(uint8_t board, err_h chain, uint8_t* buf, uint32_t cap);

/**
 * @brief Makes a record's chain a chain again, its nodes outermost first; a tag this board doesn't know keeps its
 * payload as is (cut to ERR_DATA_MAX). The source, the command header or the board are skipped. With the pool short, the
 * outermost nodes are left out.
 * @param data   The record.
 * @param len    Bytes in `data`.
 * @param chain  Out: the chain (NULL when it has no node).
 * @return NULL when parsed; ERR_RECORD_CUT (a record cut short).
 */
MUST_USE err_h err_record_parse(const uint8_t* data, uint32_t len, err_h* chain);

#ifdef __cplusplus
}
#endif
