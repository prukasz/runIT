#include "sys_actions_router.h"

#include "sys_actions.h"

#define LIST_MAX 48u  // IDs per answer, at most

ROUTER_OP(op_run, sys_actions_id_args_t) {
  return sys_actions_run(args->id, false);
}

ROUTER_OP(op_run_all, sys_actions_id_args_t) {
  return sys_actions_run(args->id, true);
}

ROUTER_OP(op_begin, sys_actions_begin_args_t) {
  return sys_actions_record_begin(args->id, args->len);
}

ROUTER_OP_TAIL(op_write, sys_actions_write_args_t) {
  ERR_CHECK(tail.len <= UINT16_MAX, ERR_CMD_BAD_ARG);
  return sys_actions_record_write(args->offset, tail.data, (uint16_t)tail.len);
}

ROUTER_OP(op_commit, sys_actions_commit_args_t) {
  return sys_actions_record_commit(args->crc32);
}

ROUTER_OP(op_read, sys_actions_read_args_t) {
  return sys_actions_read(args->id, args->offset, out->data, out->cap, &out->len);
}

ROUTER_OP(op_info, sys_actions_id_args_t) {
  sys_actions_info_t i;
  ERR_TRY(sys_actions_info(args->id, &i));
  return cmd_reply(out, &i, sizeof(i));  // {bytes, count, crc32}, LE on the ESP32
}

ROUTER_OP(op_list, sys_actions_list_args_t) {
  ERR_CHECK(out->cap >= sizeof(uint16_t) + sizeof(uint32_t), ERR_CMD_OUT_FULL,
            .need = sizeof(uint16_t) + sizeof(uint32_t), .cap = (uint16_t)out->cap);
  uint32_t ids[LIST_MAX];
  uint32_t fit = (out->cap - sizeof(uint16_t)) / sizeof(uint32_t);
  uint16_t count, total;
  ERR_TRY(sys_actions_list(args->start, ids, (uint16_t)(fit < LIST_MAX ? fit : LIST_MAX), &count, &total));
  ERR_TRY(cmd_append(out, &total, sizeof(total)));  // LE on the ESP32
  return cmd_append(out, ids, count * sizeof(uint32_t));
}

ROUTER_OP(op_remove, sys_actions_id_args_t) {
  return sys_actions_remove(args->id);
}

ROUTER_OP_NOARGS(op_abort) {
  sys_actions_abort();
  return NULL;
}

ROUTER_OP_NOARGS(op_remove_all) {
  return sys_actions_remove_all();
}

static const router_op_t k_ops[] = {
    ROUTER_W(SYS_ACTIONS_OP_RUN, op_run),
    ROUTER_W(SYS_ACTIONS_OP_BEGIN, op_begin),
    ROUTER_W(SYS_ACTIONS_OP_WRITE, op_write),
    ROUTER_W(SYS_ACTIONS_OP_COMMIT, op_commit),
    ROUTER_R(SYS_ACTIONS_OP_READ, op_read),
    ROUTER_R(SYS_ACTIONS_OP_INFO, op_info),
    ROUTER_R(SYS_ACTIONS_OP_LIST, op_list),
    ROUTER_W(SYS_ACTIONS_OP_REMOVE, op_remove),
    ROUTER_W(SYS_ACTIONS_OP_ABORT, op_abort),
    ROUTER_W(SYS_ACTIONS_OP_RUN_ALL, op_run_all),
    ROUTER_W(SYS_ACTIONS_OP_REMOVE_ALL, op_remove_all),
};

ROUTER_MODULE(sys_actions_router, k_ops)
