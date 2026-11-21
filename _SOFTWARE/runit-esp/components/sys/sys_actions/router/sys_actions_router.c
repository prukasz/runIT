#include "sys_actions_router.h"

#define LIST_MAX 48u  // IDs per answer, at most

ROUTER_WRITE(ACTIONS, OP_RUN) {
  return sys_actions_run(args->id, false);
}

ROUTER_WRITE(ACTIONS, OP_RUN_ALL) {
  return sys_actions_run(args->id, true);
}

ROUTER_WRITE(ACTIONS, OP_BEGIN) {
  return sys_actions_record_begin(args->id, args->len);
}

ROUTER_WRITE_TAIL(ACTIONS, OP_WRITE) {
  ERR_CHECK(tail_len <= UINT16_MAX, ERR_BAD_ARG);
  return sys_actions_record_write(args->offset, tail, (uint16_t)tail_len);
}

ROUTER_WRITE(ACTIONS, OP_COMMIT) {
  return sys_actions_record_commit(args->crc32);
}

ROUTER_READ(ACTIONS, OP_READ) {
  return sys_actions_read(args->id, args->offset, out->data, out->cap, &out->len);
}

ROUTER_READ(ACTIONS, OP_INFO) {
  sys_actions_info_t info;
  ERR_TRY(sys_actions_info(args->id, &info));
  return cmd_reply(out, &info, sizeof(info));
}

// u16 total, then the IDs: as many as the answer holds, at most LIST_MAX.
ROUTER_READ(ACTIONS, OP_LIST) {
  ERR_CHECK(out->cap >= sizeof(uint16_t) + sizeof(uint32_t), ERR_CMD_OUT_FULL,
            .need = sizeof(uint16_t) + sizeof(uint32_t), .cap = (uint16_t)out->cap);
  uint32_t ids[LIST_MAX];
  const uint32_t fit = (out->cap - sizeof(uint16_t)) / sizeof(uint32_t);
  uint16_t count = 0;
  uint16_t total = 0;
  ERR_TRY(sys_actions_list(args->start, ids, (uint16_t)(fit < LIST_MAX ? fit : LIST_MAX), &count, &total));
  ERR_TRY(cmd_append(out, &total, sizeof(total)));
  return cmd_append(out, ids, count * sizeof(uint32_t));
}

ROUTER_WRITE(ACTIONS, OP_REMOVE) {
  return sys_actions_remove(args->id);
}

ROUTER_WRITE(ACTIONS, OP_ABORT) {
  sys_actions_abort();
  return NULL;
}

ROUTER_WRITE(ACTIONS, OP_REMOVE_ALL) {
  return sys_actions_remove_all();
}

static const router_op_t k_ops[] = {
    ROUTER_ENTRY_W(ACTIONS, OP_RUN),     ROUTER_ENTRY_W(ACTIONS, OP_BEGIN),   ROUTER_ENTRY_W(ACTIONS, OP_WRITE),
    ROUTER_ENTRY_W(ACTIONS, OP_COMMIT),  ROUTER_ENTRY_R(ACTIONS, OP_READ),    ROUTER_ENTRY_R(ACTIONS, OP_INFO),
    ROUTER_ENTRY_R(ACTIONS, OP_LIST),    ROUTER_ENTRY_W(ACTIONS, OP_REMOVE),  ROUTER_ENTRY_W(ACTIONS, OP_ABORT),
    ROUTER_ENTRY_W(ACTIONS, OP_RUN_ALL), ROUTER_ENTRY_W(ACTIONS, OP_REMOVE_ALL),
};

ROUTER_MODULE(sys_actions_router, k_ops)
ROUTER_MODULE_REGISTER(MODULE_ID_ACTIONS, sys_actions_router)
