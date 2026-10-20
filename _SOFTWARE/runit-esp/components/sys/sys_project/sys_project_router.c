#include "sys_project_router.h"
#include "sys_project.h"

/*   key  op        request                          response
 *   0x01 BEGIN     { u32 len, u8 flags }            -   (flags: 1 autostart, 2 run after COMMIT)
 *   0x02 WRITE     { u32 offset }, data             -
 *   0x03 COMMIT    { u32 crc32 }                    -
 *   0x04 ABORT     -                                -
 *   0x05 INFO      -                                { u8 stored, u8 autostart, u8 bank, u32 crc32, u32 bytes }
 *   0x06 RUN       -                                -   (RUN_ALL of the project)
 *   0x07 ERASE     -                                -
 *   0x08 OPTIONS   { u8 autostart }                 -   (read: { u8 autostart })
 *   0x10 SECTION   { u32 id }                       a frame of the stream, refused as a command
 *   0x11 MARK      { u8 bank, u32 crc32, u32 bytes } first frame of the project record, does nothing when run */

typedef struct __attribute__((packed)) {
  uint32_t len;   /**< Total stream length in bytes */
  uint8_t flags;  /**< SYS_PROJECT_FLAG_* options */
} begin_t;

typedef struct __attribute__((packed)) {
  uint32_t offset;  /**< Byte offset */
} write_t;

typedef struct __attribute__((packed)) {
  uint32_t crc32;  /**< CRC-32 of stream */
} commit_t;

typedef struct __attribute__((packed)) {
  uint8_t autostart;  /**< 1 = autostart enabled, 0 = disabled */
} options_t;

typedef struct __attribute__((packed)) {
  uint32_t id;  /**< Target record ID (0 = program) */
} section_t;

ROUTER_OP(op_begin, begin_t) {
  return sys_project_begin(args->len, args->flags);
}

ROUTER_OP_TAIL(op_write, write_t) {
  return sys_project_write(args->offset, tail, tail_len);
}

ROUTER_OP(op_commit, commit_t) {
  return sys_project_commit(args->crc32);
}

ROUTER_OP_NOARGS(op_abort) {
  sys_project_abort();
  return NULL;
}

ROUTER_OP_NOARGS(op_info) {
  sys_project_info_t i;
  ERR_TRY(sys_project_info(&i));
  return cmd_reply(out, out_cap, out_len, &i, sizeof(i));
}

ROUTER_OP_NOARGS(op_run) {
  return sys_project_run();
}

ROUTER_OP_NOARGS(op_erase) {
  return sys_project_erase();
}

ROUTER_OP(op_options, options_t) {
  return sys_project_set_autostart(args->autostart != 0);
}

ROUTER_OP_NOARGS(op_options_get) {
  const options_t o = {.autostart = sys_project_autostart()};
  return cmd_reply(out, out_cap, out_len, &o, sizeof(o));
}

ROUTER_OP(op_section, section_t) {
  return ERR_NEW(ERR_PROJECT_NOT_IN_STREAM);
}

ROUTER_OP(op_mark, sys_project_mark_t) {
  return NULL;
}

static const router_op_t k_ops[] = {
    ROUTER_W(SYS_PROJECT_OP_BEGIN, op_begin),
    ROUTER_W(SYS_PROJECT_OP_WRITE, op_write),
    ROUTER_W(SYS_PROJECT_OP_COMMIT, op_commit),
    ROUTER_W(SYS_PROJECT_OP_ABORT, op_abort),
    ROUTER_R(SYS_PROJECT_OP_INFO, op_info),
    ROUTER_W(SYS_PROJECT_OP_RUN, op_run),
    ROUTER_W(SYS_PROJECT_OP_ERASE, op_erase),
    ROUTER_RW(SYS_PROJECT_OP_OPTIONS, op_options_get, op_options),
    ROUTER_W(SYS_PROJECT_OP_SECTION, op_section),
    ROUTER_W(SYS_PROJECT_OP_MARK, op_mark),
};

ROUTER_MODULE(sys_project_router, k_ops)
