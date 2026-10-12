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
 *   0x08 OPTIONS   { u8 autostart }                 -
 *   0x10 SECTION   { u32 id }                       a frame of the stream, refused as a command
 *   0x11 MARK      { u8 bank, u32 crc32, u32 bytes } first frame of the project record, does nothing when run */

typedef struct __attribute__((packed)) {
  uint32_t len;  //@alias Length @description The size of the stream @unit bytes
  uint8_t flags;  //@alias Options @description Bit 0 autostart at every boot, bit 1 run after the commit
} begin_t;

typedef struct __attribute__((packed)) {
  uint32_t offset;  //@alias Offset @description Bytes sent so far @unit bytes
} write_t;

typedef struct __attribute__((packed)) {
  uint32_t crc32;  //@alias CRC-32 @description Of the whole stream
} commit_t;

typedef struct __attribute__((packed)) {
  uint8_t autostart;  //@alias Autostart @description Run the project at every boot @choices 0=No,1=Yes
} options_t;

typedef struct __attribute__((packed)) {
  uint32_t id;  //@alias Record @description The record the following frames belong to (0: the program) @id action
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

ROUTER_OP(op_section, section_t) {
  return ERR_NEW(ERR_PROJECT_NOT_IN_STREAM);
}

ROUTER_OP(op_mark, sys_project_mark_t) {
  return NULL;
}

static const router_entry_t k_ops[] = {
    //@op SYS_PROJECT_OP_BEGIN @alias Begin save @description Starts saving a project as one stream
    {SYS_PROJECT_OP_BEGIN, op_begin},
    //@op SYS_PROJECT_OP_WRITE @alias Write stream @description The next bytes of the project stream @tail bytes @tail-alias Data
    {SYS_PROJECT_OP_WRITE, op_write},
    //@op SYS_PROJECT_OP_COMMIT @alias Commit save @description Checks the stream, stores the project and optionally runs it
    {SYS_PROJECT_OP_COMMIT, op_commit},
    //@op SYS_PROJECT_OP_ABORT @alias Abort save @description Drops an open save; the stored project stays
    {SYS_PROJECT_OP_ABORT, op_abort},
    //@op SYS_PROJECT_OP_INFO @alias Project info @description Whether a project is stored, its stream CRC and length @returns sys_project_info_t
    {SYS_PROJECT_OP_INFO, op_info},
    //@op SYS_PROJECT_OP_RUN @alias Run project @description Runs the stored project
    {SYS_PROJECT_OP_RUN, op_run},
    //@op SYS_PROJECT_OP_ERASE @alias Erase project @description Removes the stored project and its program records
    {SYS_PROJECT_OP_ERASE, op_erase},
    //@op SYS_PROJECT_OP_OPTIONS @alias Set options @description The autostart option
    {SYS_PROJECT_OP_OPTIONS, op_options},
    //@op SYS_PROJECT_OP_SECTION @alias Section @description A frame of the save stream: opens a section (not a command)
    {SYS_PROJECT_OP_SECTION, op_section},
    //@op SYS_PROJECT_OP_MARK @alias Mark @description First frame of the project record (does nothing when run)
    {SYS_PROJECT_OP_MARK, op_mark},
};

ROUTER_MODULE(sys_project_router, k_ops)
