#pragma once

/**
 * @file cmd_types.h
 * @brief The types of a command that every layer names: a command as it came in (cmd_req_t: the errors component's
 * record and sink, the router's queue, the session) and a handler's input and answer (cmd_in_t, cmd_out_t: the
 * routers, the device registry, every driver and contract). Plain fields, no dependency, so the lowest layer can take
 * them. What works on them is cmd.h's (the frame size, the handler type cmd_f, cmd_reply); a command's flags and the
 * values of `src` are the router queue's (router_queue.h: ROUTER_FLAG_*, ROUTER_SRC_*).
 */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* How a frame or a packet is sent (0: the normal queue, waiting for room): a stream port's packet carries them
   (data_stream_port.h), a provider's line and a UART's queue honour them. */
//#ref-defines cmd_send_flag @alias Send flags @prefix CMD_SEND_
#define CMD_SEND_URGENT (1u << 0)  //@alias Urgent @description Priority queue, sent before normal frames (matches ROUTER_FLAG_URGENT)
#define CMD_SEND_SPARE  (1u << 1)  //@alias Keep spare @description Keep half of the queue free for replies and control frames
#define CMD_SEND_NOWAIT (1u << 2)  //@alias No wait @description Fail immediately when TX queue is full instead of waiting

/**
 * @brief A command as it came in: its request [key][dir][code][args] and its origin. Whoever submits it to the router
 * fills it (the session from a frame's header, a watch, the board's own code); an error the command raises names it
 * (err_record_cmd, err_report_ex).
 */
typedef struct cmd_req {
  const uint8_t* req;  /**< The request, may be NULL when req_len is 0 */
  uint32_t req_len;    /**< Bytes in req */
  uint16_t seq;        /**< The sender's counter, in every answer */
  uint8_t src;         /**< Who sent it: a board or client id; ROUTER_SRC_SELF (0xFE) for the board's own commands */
  uint8_t flags;       /**< ROUTER_FLAG_* */
} cmd_req_t;

/**
 * @brief A handler's input: the payload only (an op's args). The router strips [dir][code], the device registry the
 * op; a module router (a root entry) gets its whole command, its key first.
 */
typedef struct {
  const uint8_t* data;  /**< The payload, may be NULL when len is 0 */
  uint32_t len;         /**< Bytes in data */
} cmd_in_t;

/** @brief A handler's answer buffer. */
typedef struct {
  uint8_t* data;  /**< The answer's bytes */
  uint32_t cap;   /**< What the answer may hold: what cmd_reply() fills at most; longer data is paged by its command
                       (ROUTER.MD "The handler") */
  uint32_t len;   /**< Written by the handler; 0 = empty answer */
} cmd_out_t;

#ifdef __cplusplus
}
#endif
