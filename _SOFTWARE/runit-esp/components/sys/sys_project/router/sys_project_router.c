#include "sys_project_router.h"
#include "sys_project.h"

ROUTER_OP(op_begin, sys_project_begin_args_t) {
  return sys_project_begin(args->len, args->boot);
}

ROUTER_OP_TAIL(op_write, sys_project_write_args_t) {
  return sys_project_write(args->offset, tail.data, tail.len);
}

ROUTER_OP(op_commit, sys_project_commit_args_t) {
  return sys_project_commit(args->crc32);
}

ROUTER_OP_NOARGS(op_abort) {
  sys_project_abort();
  return NULL;
}

ROUTER_OP_NOARGS(op_info) {
  sys_project_info_t i;
  ERR_TRY(sys_project_info(&i));
  return cmd_reply(out, &i, sizeof(i));
}

ROUTER_OP_NOARGS(op_run) {
  return sys_project_run(true);
}

ROUTER_OP_NOARGS(op_load) {
  return sys_project_run(false);
}

ROUTER_OP_NOARGS(op_erase) {
  return sys_project_erase();
}

ROUTER_OP(op_options, sys_project_options_t) {
  return sys_project_set_boot(args->boot);
}

ROUTER_OP_NOARGS(op_options_get) {
  const sys_project_options_t o = {.boot = sys_project_get_boot()};
  return cmd_reply(out, &o, sizeof(o));
}

ROUTER_OP(op_section, sys_project_section_t) {
  return ERR_NEW(ERR_PROJECT_NOT_IN_STREAM);
}

ROUTER_OP(op_mark, sys_project_mark_t) {
  return NULL;
}

ROUTER_OP_TAIL(op_source_append, sys_project_source_append_args_t) {
  return sys_nvs_json_append(sys_project_source(), args->offset, tail.data, tail.len);
}

ROUTER_OP_NOARGS(op_source_erase) {
  return sys_nvs_json_erase(sys_project_source());
}

ROUTER_OP_NOARGS(op_source_info) {
  const sys_nvs_json_info_t info = sys_nvs_json_info(sys_project_source());
  return cmd_reply(out, &info, sizeof(info));
}

ROUTER_OP(op_source_read, sys_project_source_read_args_t) {
  return sys_nvs_json_get(sys_project_source(), args->chunk, out->data, out->cap, &out->len);
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
    ROUTER_W(SYS_PROJECT_OP_LOAD, op_load),
    ROUTER_W(SYS_PROJECT_OP_SOURCE_APPEND, op_source_append),
    ROUTER_W(SYS_PROJECT_OP_SOURCE_ERASE, op_source_erase),
    ROUTER_R(SYS_PROJECT_OP_SOURCE_INFO, op_source_info),
    ROUTER_R(SYS_PROJECT_OP_SOURCE_READ, op_source_read),
};

ROUTER_MODULE(sys_project_router, k_ops)
