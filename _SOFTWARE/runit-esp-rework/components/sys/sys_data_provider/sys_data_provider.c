#include "sys_data_provider.h"

#include <assert.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "cmd.h"
#include "sdkconfig.h"
#include "sys_data_provider_driver.h"
#include "sys_data_provider_internal.h"
#include "utils.h"

ERR_MODULE_DEFINE(MODULE_ID_PROVIDER, ERR_SYS_DATA_PROVIDER_MAP)

/* ----------------------------------------------------------------------------
 * Registry state: registered driver types and provider slots
 * ------------------------------------------------------------------------- */

typedef struct {
  bool used;
  sys_data_provider_t p;
  const sys_data_provider_type_t* type;
} prov_slot_t;

static const sys_data_provider_type_t* s_types[CONFIG_SYS_DATA_PROVIDER_TYPES];
static uint32_t s_type_count;

U_MUTEX_DEFINE(s_prov_ctrl_lock);  // Serializes CREATE / REMOVE / driver own ops
static sys_data_provider_port_hook_f s_port_added;    // set by the app: a provider's port made
static sys_data_provider_port_hook_f s_port_removed;  // set by the app: a provider's port about to go
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;  // Short critical section for slot reads & data plane
static prov_slot_t s_slots[CONFIG_SYS_DATA_PROVIDER_MAX];

static prov_slot_t* find_slot_locked(uint8_t id) {
  if (id == SYS_DATA_PROVIDER_ID_NONE) {
    return NULL;
  }
  for (uint32_t i = 0; i < CONFIG_SYS_DATA_PROVIDER_MAX; i++) {
    if (s_slots[i].used && s_slots[i].p.id == id) {
      return &s_slots[i];
    }
  }
  return NULL;
}

static int find_free_slot_locked(void) {
  for (int i = 0; i < CONFIG_SYS_DATA_PROVIDER_MAX; i++) {
    if (!s_slots[i].used) {
      return i;
    }
  }
  return -1;
}

static const sys_data_provider_type_t* type_find(uint8_t type_id) {
  for (uint32_t i = 0; i < s_type_count; i++) {
    if (s_types[i]->type == type_id) {
      return s_types[i];
    }
  }
  return NULL;
}

static const sys_data_provider_op_t* find_op_row(const sys_data_provider_op_t* ops, uint32_t count, uint16_t op) {
  for (uint32_t i = 0; i < count; i++) {
    if (ops[i].op == op) {
      return &ops[i];
    }
  }
  return NULL;
}

/* ----------------------------------------------------------------------------
 * Type registration (SYS_DATA_PROVIDER_TYPE_DEFINE)
 * ------------------------------------------------------------------------- */

void sys_data_provider_type_register(const sys_data_provider_type_t* type) {
  assert(type != NULL && type->type >= SYS_DATA_PROVIDER_TYPE_UART);
  assert(type->create != NULL && type->remove != NULL);
  assert(type->op_count == 0 || type->ops != NULL);
  for (uint16_t i = 0; i < type->op_count; i++) {
    const sys_data_provider_op_t* row = &type->ops[i];
    assert((row->op >> 8) == 0xFFu && "driver ops must use SYS_DATA_PROVIDER_OWN_OP");
    assert(row->read != NULL || row->write != NULL);
  }
  assert(type_find(type->type) == NULL && "provider type registered twice");
  assert(s_type_count < CONFIG_SYS_DATA_PROVIDER_TYPES && "more provider types than SYS_DATA_PROVIDER_TYPES");
  if (s_type_count < CONFIG_SYS_DATA_PROVIDER_TYPES) {
    s_types[s_type_count++] = type;
  }
}

/* ----------------------------------------------------------------------------
 * Control-plane handlers and centralized dispatch (CONTRACT_PROVIDER + 0xFFxx own ops)
 * ------------------------------------------------------------------------- */

static err_h require_empty(cmd_in_t in) {
  ERR_CHECK(in.len == 0, ERR_CMD_BAD_LEN, .got = (uint16_t)in.len, .need = 0);
  return NULL;
}

static err_h list_providers(cmd_out_t* out) {
  sys_data_provider_entry_t rows[CONFIG_SYS_DATA_PROVIDER_MAX];
  uint8_t n = 0;
  portENTER_CRITICAL(&s_lock);
  for (uint32_t i = 0; i < CONFIG_SYS_DATA_PROVIDER_MAX; i++) {
    const prov_slot_t* s = &s_slots[i];
    if (s->used) {
      rows[n++] = (sys_data_provider_entry_t){
          .provider = s->p.id,
          .type = s->p.type,
          .state = s->p.state,
      };
    }
  }
  portEXIT_CRITICAL(&s_lock);
  ERR_TRY(cmd_append(out, &n, sizeof(n)));
  return n > 0 ? cmd_append(out, rows, (uint32_t)n * sizeof(rows[0])) : NULL;
}

static err_h info_reply(uint8_t id, cmd_out_t* out) {
  sys_data_provider_info_t info = {0};
  bool found = false;
  portENTER_CRITICAL(&s_lock);
  const prov_slot_t* s = find_slot_locked(id);
  if (s != NULL) {
    found = true;
    info.type = s->p.type;
    info.state = s->p.state;
  }
  portEXIT_CRITICAL(&s_lock);
  ERR_CHECK(found, ERR_PROV_NOT_FOUND, .provider = id);
  return cmd_reply(out, &info, sizeof(info));
}

static err_h ops_reply(uint8_t id, cmd_out_t* out) {
  static const struct {
    uint16_t op;
    uint8_t sides;
  } k_pub_ops[] = {
      {CONTRACT_PROVIDER_OP_REMOVE,          0x02u},
      {CONTRACT_PROVIDER_OP_SEND,            0x02u},
      {CONTRACT_PROVIDER_OP_INFO,            0x01u},
      {CONTRACT_PROVIDER_OP_OPS,             0x01u},
  };

  const sys_data_provider_type_t* type = NULL;
  bool found = false;
  portENTER_CRITICAL(&s_lock);
  const prov_slot_t* s = find_slot_locked(id);
  if (s != NULL) {
    found = true;
    type = s->type;
  }
  portEXIT_CRITICAL(&s_lock);
  ERR_CHECK(found, ERR_PROV_NOT_FOUND, .provider = id);

  for (uint32_t i = 0; i < ARRAY_LEN(k_pub_ops); i++) {
    ERR_TRY(cmd_append(out, &k_pub_ops[i].op, sizeof(k_pub_ops[i].op)));
    ERR_TRY(cmd_append(out, &k_pub_ops[i].sides, sizeof(k_pub_ops[i].sides)));
  }
  for (uint16_t i = 0; type != NULL && i < type->op_count; i++) {
    const sys_data_provider_op_t* row = &type->ops[i];
    if ((row->flags & SYS_DATA_PROVIDER_OP_LOCAL) != 0u) {
      continue;
    }
    const uint8_t sides = (row->read != NULL ? 0x01u : 0u) | (row->write != NULL ? 0x02u : 0u);
    ERR_TRY(cmd_append(out, &row->op, sizeof(row->op)));
    ERR_TRY(cmd_append(out, &sides, sizeof(sides)));
  }
  return NULL;
}

/* ----------------------------------------------------------------------------
 * The provider as a stream port (data_stream_port.h)
 * ------------------------------------------------------------------------- */

// A packet for the provider from the switch: sent on its line, never waited for (pass never blocks).
static void port_pass(data_stream_port_t* self, data_stream_port_id_t from, const uint8_t* data, uint32_t len) {
  (void)from;
  const sys_data_provider_t* p = DATA_STREAM_OWNER(self, sys_data_provider_t, port);
  err_free(sys_data_provider_send(p->id, (cmd_in_t){.data = data, .len = len}, CMD_SEND_NOWAIT));  // not taken: lost
}

void sys_data_provider_set_port_hooks(sys_data_provider_port_hook_f added, sys_data_provider_port_hook_f removed) {
  U_MUTEX_GUARD(s_prov_ctrl_lock);
  s_port_added = added;
  s_port_removed = removed;
  for (uint32_t i = 0; added != NULL && i < CONFIG_SYS_DATA_PROVIDER_MAX; i++) {
    if (s_slots[i].used) {
      err_report(added(&s_slots[i].p.port));
    }
  }
}

static err_h create_locked(uint8_t id, uint8_t type_id, uint8_t flags, const void* cfg, uint32_t cfg_len) {
  ERR_CHECK((flags & ~SYS_DATA_PROVIDER_CREATE_SYSTEM) == 0, ERR_CMD_BAD_ARG);

  const sys_data_provider_type_t* type = type_find(type_id);
  ERR_CHECK(type != NULL, ERR_PROV_TYPE_UNKNOWN, .type = type_id);
  ERR_CHECK(cfg_len == type->cfg_size, ERR_CMD_BAD_LEN, .got = (uint16_t)cfg_len, .need = type->cfg_size);

  portENTER_CRITICAL(&s_lock);
  const bool exists = find_slot_locked(id) != NULL;
  const int idx = exists ? -1 : find_free_slot_locked();
  portEXIT_CRITICAL(&s_lock);

  ERR_CHECK(!exists, ERR_PROV_EXISTS, .provider = id);
  ERR_CHECK(idx >= 0, ERR_PROV_FULL);

  prov_slot_t* slot = &s_slots[idx];
  void* instance = type->ctx_size > 0 ? calloc(1, type->ctx_size) : NULL;
  ERR_CHECK(type->ctx_size == 0 || instance != NULL, ERR_PROV_NO_MEM, .provider = id, .size = type->ctx_size);

  memset(slot, 0, sizeof(*slot));
  slot->p = (sys_data_provider_t){
      .driver = instance,
      .id = id,
      .type = type->type,
      .state = (flags & SYS_DATA_PROVIDER_CREATE_SYSTEM) != 0 ? SYS_DATA_PROVIDER_STATE_SYSTEM : 0u,
      .port = {.id = DATA_STREAM_PORT(DATA_STREAM_PORT_PROVIDER, id), .pass = port_pass},
  };

  err_h err = type->create(&slot->p, instance, cfg);
  if (err != NULL) {
    memset(slot, 0, sizeof(*slot));
    if (type->ctx_size > 0) {
      free(instance);
    }
    return err;
  }

  // Every driver must populate p->send inside create(); treat a missing TX hook as a contract violation.
  if (slot->p.send == NULL) {
    err_h rerr = type->remove(&slot->p, slot->p.driver);
    err_free(rerr);
    if (type->ctx_size > 0) {
      free(instance);
    }
    memset(slot, 0, sizeof(*slot));
    return ERR_NEW(ERR_CMD_BAD_ARG);
  }

  portENTER_CRITICAL(&s_lock);
  slot->type = type;
  slot->used = true;
  portEXIT_CRITICAL(&s_lock);
  if (s_port_added != NULL) {
    err_report(s_port_added(&slot->p.port));
  }
  return NULL;
}

static err_h remove_locked(uint8_t id) {
  const sys_data_provider_type_t* type = NULL;
  void* instance = NULL;
  prov_slot_t* slot = NULL;

  portENTER_CRITICAL(&s_lock);
  prov_slot_t* s = find_slot_locked(id);
  const bool found = s != NULL;
  const bool system = found && (s->p.state & SYS_DATA_PROVIDER_STATE_SYSTEM) != 0;
  const bool in_use = found && s->p.busy > 0;
  if (found && !system && !in_use) {
    type = s->type;
    instance = s->p.driver;
    s->used = false;
    slot = s;
  }
  portEXIT_CRITICAL(&s_lock);

  ERR_CHECK(found, ERR_PROV_NOT_FOUND, .provider = id);
  ERR_CHECK(!system, ERR_PROV_SYSTEM, .provider = id);
  ERR_CHECK(!in_use, ERR_PROV_IN_USE, .provider = id);

  if (s_port_removed != NULL) {
    err_report(s_port_removed(&slot->p.port));  // its links go before its driver does
  }
  err_h err = type->remove(&slot->p, instance);
  if (type->ctx_size > 0) {
    free(instance);
  }

  portENTER_CRITICAL(&s_lock);
  memset(slot, 0, sizeof(*slot));
  portEXIT_CRITICAL(&s_lock);
  return err;
}

static err_h call_own_op(uint8_t id, uint8_t dir, uint16_t op, cmd_in_t in, cmd_out_t* out, bool routed) {
  U_MUTEX_GUARD(s_prov_ctrl_lock);

  const sys_data_provider_type_t* type = NULL;
  void* instance = NULL;
  bool found = false;
  portENTER_CRITICAL(&s_lock);
  const prov_slot_t* s = find_slot_locked(id);
  if (s != NULL) {
    found = true;
    type = s->type;
    instance = s->p.driver;
  }
  portEXIT_CRITICAL(&s_lock);
  ERR_CHECK(found, ERR_PROV_NOT_FOUND, .provider = id);

  const sys_data_provider_op_t* row = find_op_row(type->ops, type->op_count, op);
  ERR_CHECK(row != NULL, ERR_PROV_UNKNOWN_OP, .op = op);
  ERR_CHECK(!(routed && (row->flags & SYS_DATA_PROVIDER_OP_LOCAL) != 0u), ERR_PROV_LOCAL_OP, .op = op);

  const cmd_f fn = dir == CMD_DIR_READ ? row->read : row->write;
  ERR_CHECK(fn != NULL, ERR_PROV_UNKNOWN_OP, .op = op);
  return fn(instance, in, out);
}

static err_h call_dispatch(uint8_t id, uint8_t dir, uint16_t op, cmd_in_t in, cmd_out_t* out, bool routed) {
  cmd_out_t none = {0};
  cmd_out_t* answer = out != NULL ? out : &none;
  ERR_CHECK(dir == CMD_DIR_READ || dir == CMD_DIR_WRITE, ERR_CMD_DIRECTION, .dir = dir);

  if (op == CONTRACT_PROVIDER_OP_LIST) {
    ERR_CHECK(id == SYS_DATA_PROVIDER_ID_NONE, ERR_PROV_BAD_ID, .provider = id);
    ERR_CHECK(dir == CMD_DIR_READ, ERR_PROV_UNKNOWN_OP, .op = op);
    ERR_TRY(require_empty(in));
    return list_providers(answer);
  }

  ERR_CHECK(id != SYS_DATA_PROVIDER_ID_NONE, ERR_PROV_BAD_ID, .provider = id);

  if ((op >> 8) != MODULE_ID_PROVIDER) {
    return call_own_op(id, dir, op, in, answer, routed);
  }

  const bool is_read = dir == CMD_DIR_READ;
  const bool expect_read = (op == CONTRACT_PROVIDER_OP_INFO || op == CONTRACT_PROVIDER_OP_OPS);
  ERR_CHECK(is_read == expect_read, ERR_PROV_UNKNOWN_OP, .op = op);

  if (op == CONTRACT_PROVIDER_OP_CREATE) {
    ERR_CHECK(in.len >= SYS_DATA_PROVIDER_CREATE_HDR, ERR_CMD_BAD_LEN, .got = (uint16_t)in.len,
              .need = SYS_DATA_PROVIDER_CREATE_HDR);
    U_MUTEX_GUARD(s_prov_ctrl_lock);
    return create_locked(id, in.data[0], in.data[1], in.data + SYS_DATA_PROVIDER_CREATE_HDR,
                         in.len - SYS_DATA_PROVIDER_CREATE_HDR);
  }
  if (op == CONTRACT_PROVIDER_OP_SEND) {
    return sys_data_provider_send(id, in, 0);
  }

  ERR_TRY(require_empty(in));
  switch (op) {
    case CONTRACT_PROVIDER_OP_REMOVE: {
      U_MUTEX_GUARD(s_prov_ctrl_lock);
      return remove_locked(id);
    }
    case CONTRACT_PROVIDER_OP_INFO:
      return info_reply(id, answer);
    case CONTRACT_PROVIDER_OP_OPS:
      return ops_reply(id, answer);
    default:
      return ERR_NEW(ERR_PROV_UNKNOWN_OP, .op = op);
  }
}

err_h sys_data_provider_call(uint8_t id, uint8_t dir, uint16_t op, cmd_in_t in, cmd_out_t* out) {
  return call_dispatch(id, dir, op, in, out, false);
}

err_h sys_data_provider_call_routed(uint8_t id, uint8_t dir, uint16_t op, cmd_in_t in, cmd_out_t* out) {
  return call_dispatch(id, dir, op, in, out, true);
}

err_h sys_data_provider_write(uint8_t id, uint16_t op, const void* args, uint32_t args_len) {
  return sys_data_provider_call(id, CMD_DIR_WRITE, op, (cmd_in_t){.data = args, .len = args_len}, NULL);
}

err_h sys_data_provider_create_system(uint8_t id, uint8_t type_id, const void* cfg, uint32_t cfg_len) {
  uint8_t frame[SYS_DATA_PROVIDER_CREATE_HDR + SYS_DATA_PROVIDER_CREATE_CFG_MAX];
  ERR_CHECK(cfg_len <= SYS_DATA_PROVIDER_CREATE_CFG_MAX, ERR_CMD_BAD_LEN, .got = (uint16_t)cfg_len,
            .need = (uint16_t)SYS_DATA_PROVIDER_CREATE_CFG_MAX);
  frame[0] = type_id;
  frame[1] = SYS_DATA_PROVIDER_CREATE_SYSTEM;
  if (cfg_len > 0u) {
    memcpy(frame + SYS_DATA_PROVIDER_CREATE_HDR, cfg, cfg_len);
  }
  return sys_data_provider_write(id, CONTRACT_PROVIDER_OP_CREATE, frame, SYS_DATA_PROVIDER_CREATE_HDR + cfg_len);
}

err_h sys_data_provider_read(uint8_t id, uint16_t op, const void* args, uint32_t args_len, void* out,
                             uint32_t out_size) {
  cmd_out_t answer = {.data = out, .cap = out_size, .len = 0};
  ERR_TRY(sys_data_provider_call(id, CMD_DIR_READ, op, (cmd_in_t){.data = args, .len = args_len}, &answer));
  ERR_CHECK(answer.len == out_size, ERR_CMD_BAD_LEN, .got = (uint16_t)answer.len, .need = (uint16_t)out_size);
  return NULL;
}

/* ----------------------------------------------------------------------------
 * Data plane
 * ------------------------------------------------------------------------- */

err_h sys_data_provider_send(uint8_t id, cmd_in_t frame, uint8_t flags) {
  ERR_CHECK(frame.len > 0 && frame.data != NULL, ERR_CMD_BAD_ARG);
  ERR_CHECK(frame.len <= CMD_FRAME_MAX, ERR_PROV_TOO_LONG, .provider = id,
            .len = frame.len > UINT16_MAX ? UINT16_MAX : (uint16_t)frame.len, .max = CMD_FRAME_MAX);

  portENTER_CRITICAL(&s_lock);
  prov_slot_t* s = find_slot_locked(id);
  sys_data_provider_t* p = s != NULL ? &s->p : NULL;
  const bool up = p != NULL && (p->state & SYS_DATA_PROVIDER_STATE_UP) != 0;
  if (p != NULL && up) {
    p->busy++;
  }
  portEXIT_CRITICAL(&s_lock);

  ERR_CHECK(p != NULL, ERR_PROV_NOT_FOUND, .provider = id);
  ERR_CHECK(up, ERR_PROV_DOWN, .provider = id);

  err_h err = p->send(p, frame, flags);

  portENTER_CRITICAL(&s_lock);
  p->busy--;
  portEXIT_CRITICAL(&s_lock);

  if (err == NULL || err_root_tag(err) == ERR_PROV_BUSY) {
    return err;
  }
  return ERR_WRAP(err, ERR_PROV_SEND, .provider = id);
}

uint8_t sys_data_provider_state(uint8_t id) {
  portENTER_CRITICAL(&s_lock);
  const prov_slot_t* s = find_slot_locked(id);
  const uint8_t state = s != NULL ? s->p.state : 0u;
  portEXIT_CRITICAL(&s_lock);
  return state;
}

void sys_data_provider_set_up(sys_data_provider_t* p, bool up) {
  if (p == NULL) {
    return;
  }
  portENTER_CRITICAL(&s_lock);
  p->state = up ? (uint8_t)(p->state | SYS_DATA_PROVIDER_STATE_UP) : (uint8_t)(p->state & ~SYS_DATA_PROVIDER_STATE_UP);
  portEXIT_CRITICAL(&s_lock);
}

void sys_data_provider_deliver(sys_data_provider_t* p, const uint8_t* data, uint32_t len) {
  if (p != NULL && data != NULL) {
    data_stream_send(&p->port, data, len);  // to the ports linked from it; nothing while it is not registered
  }
}
