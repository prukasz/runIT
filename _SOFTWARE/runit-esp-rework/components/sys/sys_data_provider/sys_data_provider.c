#include "sys_data_provider.h"

#include <assert.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "cmd.h"
#include "data_stream.h"
#include "sdkconfig.h"
#include "sys_data_provider_driver.h"
#include "sys_data_provider_internal.h"
#include "utils.h"

ERR_MODULE_DEFINE(MODULE_ID_PROVIDER, ERR_SYS_DATA_PROVIDER_MAP)

/* ----------------------------------------------------------------------------
 * Registry state: registered driver types and provider slots
 * ------------------------------------------------------------------------- */

typedef struct {
  sys_data_provider_t* p;                // The driver's struct; NULL: a free slot
  const sys_data_provider_type_t* type;
  SemaphoreHandle_t line;                // One call into the driver at a time: its pass, its own ops, its remove
  StaticSemaphore_t line_buf;
} prov_slot_t;

static const sys_data_provider_type_t* s_types[CONFIG_SYS_DATA_PROVIDER_TYPES];
static uint32_t s_type_count;

U_MUTEX_DEFINE(s_prov_ctrl_lock);  // Serializes CREATE / REMOVE / driver own ops
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;  // Short critical section for slot reads and the state
static prov_slot_t s_slots[CONFIG_SYS_DATA_PROVIDER_MAX];

static prov_slot_t* find_slot_locked(uint8_t id) {
  if (id == SYS_DATA_PROVIDER_ID_NONE) {
    return NULL;
  }
  for (uint32_t i = 0; i < CONFIG_SYS_DATA_PROVIDER_MAX; i++) {
    if (s_slots[i].p != NULL && (uint8_t)s_slots[i].p->port.id == id) {
      return &s_slots[i];
    }
  }
  return NULL;
}

static int find_free_slot_locked(void) {
  for (int i = 0; i < CONFIG_SYS_DATA_PROVIDER_MAX; i++) {
    if (s_slots[i].p == NULL) {
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
  assert(type->create != NULL && type->remove != NULL && type->pass != NULL);
  assert(type->size >= sizeof(sys_data_provider_t));
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
    if (s->p != NULL) {
      rows[n++] = (sys_data_provider_entry_t){
          .provider = (uint8_t)s->p->port.id,
          .type = s->type->type,
          .state = s->p->state,
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
    info.type = s->type->type;
    info.state = s->p->state;
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
    const uint8_t sides = (row->read != NULL ? 0x01u : 0u) | (row->write != NULL ? 0x02u : 0u);
    ERR_TRY(cmd_append(out, &row->op, sizeof(row->op)));
    ERR_TRY(cmd_append(out, &sides, sizeof(sides)));
  }
  return NULL;
}

// The input of every provider's port: the packet to its type's pass as it is, one call into the driver at a time.
static void line_pass(data_stream_port_t* self, data_stream_port_id_t from, uint8_t flags, const uint8_t* data,
                      uint32_t len) {
  const sys_data_provider_t* p = DATA_STREAM_OWNER(self, sys_data_provider_t, port);
  prov_slot_t* s = NULL;
  portENTER_CRITICAL(&s_lock);
  for (uint32_t i = 0; s == NULL && i < CONFIG_SYS_DATA_PROVIDER_MAX; i++) {
    s = s_slots[i].p == p ? &s_slots[i] : NULL;
  }
  portEXIT_CRITICAL(&s_lock);
  if (s != NULL) {
    U_MUTEX_GUARD(s->line);
    if (s->p == p) {  // not removed while it waited
      s->type->pass(self, from, flags, data, len);
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

  sys_data_provider_t* p = calloc(1, type->size);
  ERR_CHECK(p != NULL, ERR_PROV_NO_MEM, .provider = id, .size = type->size);
  p->port = (data_stream_port_t){.id = DATA_STREAM_PORT(DATA_STREAM_PORT_PROVIDER, id), .pass = line_pass};
  p->state = (flags & SYS_DATA_PROVIDER_CREATE_SYSTEM) != 0 ? SYS_DATA_PROVIDER_STATE_SYSTEM : 0u;

  err_h err = type->create(p, cfg);
  if (err != NULL) {
    free(p);
    return err;
  }

  prov_slot_t* slot = &s_slots[idx];
  if (slot->line == NULL) {
    slot->line = xSemaphoreCreateMutexStatic(&slot->line_buf);
  }
  portENTER_CRITICAL(&s_lock);
  slot->type = type;
  slot->p = p;
  portEXIT_CRITICAL(&s_lock);

  err = data_stream_register(&p->port);  // no room in the data matrix: it can't be linked, it is not made
  if (err != NULL) {
    portENTER_CRITICAL(&s_lock);
    slot->p = NULL;
    portEXIT_CRITICAL(&s_lock);
    err_free(type->remove(p));
    free(p);
  }
  return err;
}

static err_h remove_locked(uint8_t id) {
  portENTER_CRITICAL(&s_lock);
  prov_slot_t* s = find_slot_locked(id);
  sys_data_provider_t* p = s != NULL ? s->p : NULL;
  const bool system = p != NULL && (p->state & SYS_DATA_PROVIDER_STATE_SYSTEM) != 0;
  portEXIT_CRITICAL(&s_lock);

  ERR_CHECK(p != NULL, ERR_PROV_NOT_FOUND, .provider = id);
  ERR_CHECK(!system, ERR_PROV_SYSTEM, .provider = id);

  err_report(data_stream_unregister(&p->port));  // out of the data matrix first: its links go before its driver
  err_h err = NULL;
  {
    U_MUTEX_GUARD(s->line);  // after the call into the driver in flight
    portENTER_CRITICAL(&s_lock);
    s->p = NULL;
    portEXIT_CRITICAL(&s_lock);
    err = s->type->remove(p);
  }
  free(p);
  return err;
}

static err_h call_own_op(uint8_t id, uint8_t dir, uint16_t op, cmd_in_t in, cmd_out_t* out) {
  U_MUTEX_GUARD(s_prov_ctrl_lock);  // no CREATE / REMOVE meanwhile: the slot stays as it is

  portENTER_CRITICAL(&s_lock);
  prov_slot_t* s = find_slot_locked(id);
  portEXIT_CRITICAL(&s_lock);
  ERR_CHECK(s != NULL, ERR_PROV_NOT_FOUND, .provider = id);

  const sys_data_provider_op_t* row = find_op_row(s->type->ops, s->type->op_count, op);
  ERR_CHECK(row != NULL, ERR_PROV_UNKNOWN_OP, .op = op);

  const cmd_f fn = dir == CMD_DIR_READ ? row->read : row->write;
  ERR_CHECK(fn != NULL, ERR_PROV_UNKNOWN_OP, .op = op);
  U_MUTEX_GUARD(s->line);
  return fn(s->p, in, out);
}

err_h sys_data_provider_call_routed(uint8_t id, uint8_t dir, uint16_t op, cmd_in_t in, cmd_out_t* out) {
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
    return call_own_op(id, dir, op, in, answer);
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

err_h sys_data_provider_create_system(uint8_t id, uint8_t type_id, const void* cfg, uint32_t cfg_len) {
  uint8_t frame[SYS_DATA_PROVIDER_CREATE_HDR + SYS_DATA_PROVIDER_CREATE_CFG_MAX];
  ERR_CHECK(cfg_len <= SYS_DATA_PROVIDER_CREATE_CFG_MAX, ERR_CMD_BAD_LEN, .got = (uint16_t)cfg_len,
            .need = (uint16_t)SYS_DATA_PROVIDER_CREATE_CFG_MAX);
  frame[0] = type_id;
  frame[1] = SYS_DATA_PROVIDER_CREATE_SYSTEM;
  if (cfg_len > 0u) {
    memcpy(frame + SYS_DATA_PROVIDER_CREATE_HDR, cfg, cfg_len);
  }
  const cmd_in_t in = {.data = frame, .len = SYS_DATA_PROVIDER_CREATE_HDR + cfg_len};
  return sys_data_provider_call_routed(id, CMD_DIR_WRITE, CONTRACT_PROVIDER_OP_CREATE, in, NULL);
}

/* ----------------------------------------------------------------------------
 * The driver's side of the data plane
 * ------------------------------------------------------------------------- */

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
    data_stream_send(&p->port, 0, data, len);  // to the ports linked from it; nothing while it is not registered
  }
}
