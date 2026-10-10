#include "sys_actions_router.h"
#include <string.h>
#include "sys_actions.h"

/*   key  op            request                              response
 *   0x01 RUN           { u8 id }                            -
 *   0x02 RECORD_START  { u8 id, u8 execute }, name          -
 *   0x03 RECORD_STOP   -                                    info
 *   0x04 BEGIN         { u8 id, u16 len }, name             -
 *   0x05 WRITE         { u16 offset }, data                 -
 *   0x06 COMMIT        { u32 crc32 }                        -
 *   0x07 READ          { u8 id, u16 offset, u16 len }       the bytes
 *   0x08 INFO          { u8 id }                            info
 *   0x09 LIST          { u8 start }                         [u16 total] then { u8 id, name } by ascending ID from
 *                                                           `start`, as many as fit (ask again from the last ID + 1)
 *   0x0A REMOVE        { u8 id }                            -
 *   0x0B ABORT         -                                    -
 *   0x0C RENAME        { u8 id }, name                      -
 *
 *   name = NUL-ended, nothing after the NUL ("\0" alone = no name);
 *   info = { u16 bytes, u16 count, u32 crc32 }, name */

typedef struct __attribute__((packed)) {
  uint8_t id;
} id_args_t;

typedef struct __attribute__((packed)) {
  uint8_t id;
  uint8_t execute;
} record_start_t;

typedef struct __attribute__((packed)) {
  uint8_t id;
  uint16_t len;
} begin_t;

typedef struct __attribute__((packed)) {
  uint16_t offset;
} write_t;

typedef struct __attribute__((packed)) {
  uint32_t crc32;
} commit_t;

typedef struct __attribute__((packed)) {
  uint8_t id;
  uint16_t offset;
  uint16_t len;
} read_t;

typedef struct __attribute__((packed)) {
  uint16_t bytes;
  uint16_t count;
  uint32_t crc32;
} info_head_t;

// The name tail: NUL-ended with nothing after it (the C API checks the name itself).
static err_h name_arg(const uint8_t* tail, uint32_t tail_len, const char** name) {
  ERR_CHECK(tail[tail_len - 1] == '\0' && strlen((const char*)tail) == tail_len - 1, ERR_CMD_BAD_ARG);
  *name = (const char*)tail;
  return NULL;
}

// Appends `len` bytes to out at *pos; ERR_CMD_OUT_FULL when they don't fit.
static err_h put(uint8_t* out, uint32_t cap, uint32_t* pos, const void* data, uint32_t len) {
  ERR_CHECK(*pos + len <= cap, ERR_CMD_OUT_FULL, .need = (uint16_t)(*pos + len), .cap = (uint16_t)cap);
  memcpy(out + *pos, data, len);
  *pos += len;
  return NULL;
}

static err_h reply_info(const sys_actions_info_t* i, uint8_t* out, uint32_t out_cap, uint32_t* out_len) {
  const info_head_t head = {.bytes = i->bytes, .count = i->count, .crc32 = i->crc32};
  uint32_t pos = 0;
  ERR_TRY(put(out, out_cap, &pos, &head, sizeof(head)));
  ERR_TRY(put(out, out_cap, &pos, i->name, (uint32_t)strlen(i->name) + 1));
  *out_len = pos;
  return NULL;
}

ROUTER_OP(op_run, id_args_t) {
  return sys_actions_run(a->id);
}

ROUTER_OP_TAIL(op_record_start, record_start_t) {
  const char* name;
  ERR_TRY(name_arg(tail, tail_len, &name));
  const router_cmd_t* origin = router_origin();  // the client to record
  ERR_CHECK(origin != NULL, ERR_ACTION_WRONG_TASK);
  return sys_actions_record_start(a->id, name, a->execute != 0, origin->link, origin->peer);
}

ROUTER_OP_NOARGS(op_record_stop) {
  sys_actions_info_t i;
  ERR_TRY(sys_actions_record_stop(&i));
  return reply_info(&i, out, out_cap, out_len);
}

ROUTER_OP_TAIL(op_begin, begin_t) {
  const char* name;
  ERR_TRY(name_arg(tail, tail_len, &name));
  return sys_actions_upload_begin(a->id, name, a->len);
}

ROUTER_OP_TAIL(op_write, write_t) {
  ERR_CHECK(tail_len <= UINT16_MAX, ERR_CMD_BAD_ARG);
  return sys_actions_upload_write(a->offset, tail, (uint16_t)tail_len);
}

ROUTER_OP(op_commit, commit_t) {
  return sys_actions_upload_commit(a->crc32);
}

ROUTER_OP(op_read, read_t) {
  ERR_CHECK(a->len <= out_cap, ERR_CMD_OUT_FULL, .need = a->len, .cap = (uint16_t)out_cap);
  ERR_TRY(sys_actions_read(a->id, a->offset, out, a->len));
  *out_len = a->len;
  return NULL;
}

ROUTER_OP(op_info, id_args_t) {
  sys_actions_info_t i;
  ERR_TRY(sys_actions_info(a->id, &i));
  return reply_info(&i, out, out_cap, out_len);
}

typedef struct {
  uint8_t* out;
  uint32_t cap;
  uint32_t pos;
} list_page_t;

// Adds { id, name } while it fits; the first entry that doesn't ends the page.
static bool list_put(uint8_t id, const char* name, void* ctx) {
  list_page_t* p = ctx;
  uint32_t n = (uint32_t)strlen(name) + 1;
  if (p->pos + 1 + n > p->cap) {
    return false;
  }
  p->out[p->pos] = id;
  memcpy(p->out + p->pos + 1, name, n);
  p->pos += 1 + n;
  return true;
}

ROUTER_OP(op_list, id_args_t) {
  ERR_CHECK(out_cap >= sizeof(uint16_t), ERR_CMD_OUT_FULL, .need = sizeof(uint16_t), .cap = (uint16_t)out_cap);
  list_page_t page = {.out = out, .cap = out_cap, .pos = sizeof(uint16_t)};
  uint16_t total;
  ERR_TRY(sys_actions_list(a->id, list_put, &page, &total));
  memcpy(out, &total, sizeof(total));  // LE on the ESP32
  *out_len = page.pos;
  return NULL;
}

ROUTER_OP(op_remove, id_args_t) {
  return sys_actions_remove(a->id);
}

ROUTER_OP_NOARGS(op_abort) {
  sys_actions_abort();
  return NULL;
}

ROUTER_OP_TAIL(op_rename, id_args_t) {
  const char* name;
  ERR_TRY(name_arg(tail, tail_len, &name));
  return sys_actions_rename(a->id, name);
}

static const router_entry_t k_ops[] = {
    {SYS_ACTIONS_OP_RUN, op_run},       {SYS_ACTIONS_OP_RECORD_START, op_record_start},
    {SYS_ACTIONS_OP_RECORD_STOP, op_record_stop}, {SYS_ACTIONS_OP_BEGIN, op_begin},
    {SYS_ACTIONS_OP_WRITE, op_write},   {SYS_ACTIONS_OP_COMMIT, op_commit},
    {SYS_ACTIONS_OP_READ, op_read},     {SYS_ACTIONS_OP_INFO, op_info},
    {SYS_ACTIONS_OP_LIST, op_list},     {SYS_ACTIONS_OP_REMOVE, op_remove},
    {SYS_ACTIONS_OP_ABORT, op_abort},   {SYS_ACTIONS_OP_RENAME, op_rename},
};

ROUTER_MODULE(sys_actions_router, k_ops)
