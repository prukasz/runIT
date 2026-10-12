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
  uint32_t id;  //@alias Action @description The record's id @id action
} id_args_t;

typedef struct __attribute__((packed)) {
  uint32_t id;  //@alias Action @description The record's id @id action
  uint8_t execute;  //@alias Run while recording @description Run the frames as they come (else only record them) @choices 0=No,1=Yes
} record_start_t;

typedef struct __attribute__((packed)) {
  uint32_t id;  //@alias Action @description The record's id @id action
  uint16_t len;  //@alias Length @description The size of the image @unit bytes
} begin_t;

typedef struct __attribute__((packed)) {
  uint16_t offset;  //@alias Offset @description Bytes sent so far @unit bytes
} write_t;

typedef struct __attribute__((packed)) {
  uint32_t crc32;  //@alias CRC-32 @description Of the whole image
} commit_t;

typedef struct __attribute__((packed)) {
  uint32_t start;  //@alias From @description List the ids from this one up @id action
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
    //@op SYS_ACTIONS_OP_RUN @alias Run @description Runs a stored record, stopping at its first failing frame
    {SYS_ACTIONS_OP_RUN, op_run},
    //@op SYS_ACTIONS_OP_RECORD_START @alias Start recording @description Records this client's commands into a record
    {SYS_ACTIONS_OP_RECORD_START, op_record_start},
    //@op SYS_ACTIONS_OP_RECORD_STOP @alias Stop recording @description Stores the recording @returns sys_actions_info_t
    {SYS_ACTIONS_OP_RECORD_STOP, op_record_stop},
    //@op SYS_ACTIONS_OP_BEGIN @alias Begin upload @description Starts uploading a whole record image
    {SYS_ACTIONS_OP_BEGIN, op_begin},
    //@op SYS_ACTIONS_OP_WRITE @alias Write upload @description The next bytes of the image @tail bytes @tail-alias Data
    {SYS_ACTIONS_OP_WRITE, op_write},
    //@op SYS_ACTIONS_OP_COMMIT @alias Commit upload @description Checks the image and stores it
    {SYS_ACTIONS_OP_COMMIT, op_commit},
    //@op SYS_ACTIONS_OP_READ @alias Read record @description Reads a record back, one answer packet per frame @returns bytes @returns-alias Frame @returns-description Each answer is one frame exactly as recorded
    {SYS_ACTIONS_OP_READ, op_read},
    //@op SYS_ACTIONS_OP_INFO @alias Record info @description Size, frame count and CRC of a record @returns sys_actions_info_t
    {SYS_ACTIONS_OP_INFO, op_info},
    //@op SYS_ACTIONS_OP_LIST @alias List records @description The stored record ids from a given one up @returns u16 @returns-alias Stored records @returns-tail u32[] @returns-tail-alias Record ids
    {SYS_ACTIONS_OP_LIST, op_list},
    //@op SYS_ACTIONS_OP_REMOVE @alias Remove record @description Removes a record
    {SYS_ACTIONS_OP_REMOVE, op_remove},
    //@op SYS_ACTIONS_OP_ABORT @alias Abort @description Drops an open recording or upload
    {SYS_ACTIONS_OP_ABORT, op_abort},
    //@op SYS_ACTIONS_OP_RUN_ALL @alias Run all @description Runs a record to its end, reporting each failing frame
    {SYS_ACTIONS_OP_RUN_ALL, op_run_all},
};

ROUTER_MODULE(sys_actions_router, k_ops)
