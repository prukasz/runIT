#include "sys_actions_router.h"

#include "sys_actions.h"

/*   key  op            request                              response
 *   0x01 RUN           { u32 id }                           -
 *   0x02 RECORD_START  { u32 id, u8 execute }               -
 *   0x03 RECORD_STOP   -                                    info
 *   0x04 BEGIN         { u32 id, u16 len }                  -
 *   0x05 WRITE         { u16 offset }, data                 -
 *   0x06 COMMIT        { u32 crc32 }                        -
 *   0x07 READ          { u32 id }                           one answer per recorded frame, exactly as recorded
 *   0x08 INFO          { u32 id }                           info
 *   0x09 LIST          { u32 start }                        u16 total, then u32 IDs ascending from `start`, as many
 *                                                           as fit (ask again from the last ID + 1)
 *   0x0A REMOVE        { u32 id }                           -
 *   0x0B ABORT         -                                    -
 *   (0x0C was RENAME: names are gone)
 *   0x0D RUN_ALL       { u32 id }                           - like RUN but every frame runs; the first failure comes back
 *
 *   info = { u16 bytes, u16 count, u32 crc32 } */

#define LIST_MAX 48u  // IDs per answer, at most

typedef struct __attribute__((packed)) {
  uint32_t id;  /**< Action record ID */
} id_args_t;

typedef struct __attribute__((packed)) {
  uint32_t id;      /**< Action record ID */
  uint8_t execute;  /**< 1 = run while recording, 0 = record only */
} record_start_t;

typedef struct __attribute__((packed)) {
  uint32_t id;   /**< Action record ID */
  uint16_t len;  /**< Length in bytes */
} begin_t;

typedef struct __attribute__((packed)) {
  uint16_t offset;  /**< Byte offset in upload */
} write_t;

typedef struct __attribute__((packed)) {
  uint32_t crc32;  /**< CRC-32 of image */
} commit_t;

typedef struct __attribute__((packed)) {
  uint32_t start;  /**< Action record ID to start listing from */
} list_args_t;

static err_h reply_info(const sys_actions_info_t* i, uint8_t* out, uint32_t out_cap, uint32_t* out_len) {
  return cmd_reply(out, out_cap, out_len, i, sizeof(*i));  // {bytes, count, crc32}, LE on the ESP32
}

ROUTER_OP(op_run, id_args_t) {
  return sys_actions_run(args->id, false);
}

ROUTER_OP(op_run_all, id_args_t) {
  return sys_actions_run(args->id, true);
}

ROUTER_OP(op_record_start, record_start_t) {
  const router_cmd_t* origin = router_origin();  // the client to record
  ERR_CHECK(origin != NULL, ERR_ACTION_WRONG_TASK);
  return sys_actions_record_start(args->id, args->execute != 0, origin->link, origin->peer);
}

ROUTER_OP_NOARGS(op_record_stop) {
  sys_actions_info_t i;
  ERR_TRY(sys_actions_record_stop(&i));
  return reply_info(&i, out, out_cap, out_len);
}

ROUTER_OP(op_begin, begin_t) {
  return sys_actions_upload_begin(args->id, args->len);
}

ROUTER_OP_TAIL(op_write, write_t) {
  ERR_CHECK(tail_len <= UINT16_MAX, ERR_CMD_BAD_ARG);
  return sys_actions_upload_write(args->offset, tail, (uint16_t)tail_len);
}

ROUTER_OP(op_commit, commit_t) {
  return sys_actions_upload_commit(args->crc32);
}

typedef struct {
  uint8_t* out;
  uint32_t cap;
  uint32_t* out_len;
} dump_t;

// One answer packet per frame, the frame as recorded; the last one is the op's own answer.
static err_h dump_frame(const uint8_t* frame, uint16_t len, bool last, void* ctx) {
  dump_t* d = ctx;
  ERR_CHECK(len <= d->cap, ERR_CMD_OUT_FULL, .need = len, .cap = (uint16_t)d->cap);
  memcpy(d->out, frame, len);
  if (last) {
    *d->out_len = len;
    return NULL;
  }
  return router_answer_part(d->out, len);
}

ROUTER_OP(op_read, id_args_t) {
  dump_t d = {.out = out, .cap = out_cap, .out_len = out_len};
  return sys_actions_dump(args->id, dump_frame, &d);
}

ROUTER_OP(op_info, id_args_t) {
  sys_actions_info_t i;
  ERR_TRY(sys_actions_info(args->id, &i));
  return reply_info(&i, out, out_cap, out_len);
}

ROUTER_OP(op_list, list_args_t) {
  ERR_CHECK(out_cap >= sizeof(uint16_t) + sizeof(uint32_t), ERR_CMD_OUT_FULL,
            .need = sizeof(uint16_t) + sizeof(uint32_t), .cap = (uint16_t)out_cap);
  uint32_t ids[LIST_MAX];
  uint32_t fit = (out_cap - sizeof(uint16_t)) / sizeof(uint32_t);
  uint16_t count, total;
  ERR_TRY(sys_actions_list(args->start, ids, (uint16_t)(fit < LIST_MAX ? fit : LIST_MAX), &count, &total));
  memcpy(out, &total, sizeof(total));  // LE on the ESP32
  memcpy(out + sizeof(total), ids, count * sizeof(uint32_t));
  *out_len = (uint32_t)(sizeof(total) + count * sizeof(uint32_t));
  return NULL;
}

ROUTER_OP(op_remove, id_args_t) {
  return sys_actions_remove(args->id);
}

ROUTER_OP_NOARGS(op_abort) {
  sys_actions_abort();
  return NULL;
}

static const router_entry_t k_ops[] = {
    {SYS_ACTIONS_OP_RUN, op_run},
    {SYS_ACTIONS_OP_RECORD_START, op_record_start},
    {SYS_ACTIONS_OP_RECORD_STOP, op_record_stop},
    {SYS_ACTIONS_OP_BEGIN, op_begin},
    {SYS_ACTIONS_OP_WRITE, op_write},
    {SYS_ACTIONS_OP_COMMIT, op_commit},
    {SYS_ACTIONS_OP_READ, op_read},
    {SYS_ACTIONS_OP_INFO, op_info},
    {SYS_ACTIONS_OP_LIST, op_list},
    {SYS_ACTIONS_OP_REMOVE, op_remove},
    {SYS_ACTIONS_OP_ABORT, op_abort},
    {SYS_ACTIONS_OP_RUN_ALL, op_run_all},
};

ROUTER_MODULE(sys_actions_router, k_ops)
