/**
 * @file sys_data_provider.c
 * @brief The provider registry (SYS_DATA_PROVIDER.MD): the driver types, the providers' slots, their lifecycle (CREATE,
 * REMOVE, the port in the data matrix), one call into a driver at a time, and the router of MODULE_ID_PROVIDER.
 */

#include "sys_data_provider.h"

#include <assert.h>
#include <string.h>

#include "data_matrix.h"
#include "sdkconfig.h"
#include "sys_data_provider_driver.h"
#include "sys_data_provider_router.h"
#include "utils.h"

ERR_MODULE_DEFINE(MODULE_ID_PROVIDER, ERR_SYS_DATA_PROVIDER_MAP)

// The router's wire format (contract_provider.json). Op codes: MODULE_ID_PROVIDER in the high byte; never renumber.
#define CONTRACT_PROVIDER_OP_CREATE ((uint16_t)((MODULE_ID_PROVIDER << 8) | 0x00u))  // W [type u8][flags u8][cfg]
#define CONTRACT_PROVIDER_OP_REMOVE ((uint16_t)((MODULE_ID_PROVIDER << 8) | 0x01u))  // W
#define CONTRACT_PROVIDER_OP_LIST   ((uint16_t)((MODULE_ID_PROVIDER << 8) | 0x02u))  // R [u8 count][entry_t...]
#define CONTRACT_PROVIDER_OP_OPS    ((uint16_t)((MODULE_ID_PROVIDER << 8) | 0x05u))  // R [op u16][sides u8]...
#define CREATE_HDR                  2u     // [type u8][flags u8] before the cfg
#define CREATE_SYSTEM               0x01u  // CREATE flag: the board's own, not removed
#define ID_NONE                     0xFFu  // Never a provider

// One row of LIST (3 B).
typedef struct __attribute__((packed)) {
  uint8_t provider;  // Provider ID
  uint8_t type;      // sys_data_provider_type_e
  uint8_t state;     // SYS_DATA_PROVIDER_STATE_*
} entry_t;

// A provider's place. The driver's struct lives in it and is never freed: a send in flight in the data matrix (its
// targets copied before REMOVE) still finds the port and its `in`, the registry's, which drops it.
typedef struct {
  union {
    sys_data_provider_t p;  // The driver's struct, its base first
    _Alignas(max_align_t) uint8_t mem[CONFIG_SYS_DATA_PROVIDER_SIZE];
  };
  const sys_data_provider_type_t* type;  // NULL: a free slot
  SemaphoreHandle_t line;                // One call into the driver at a time: its `in`, its own ops, its remove
  StaticSemaphore_t line_buf;
} prov_slot_t;

static const sys_data_provider_type_t* s_types[CONFIG_SYS_DATA_PROVIDER_TYPES];
static uint32_t s_type_count;

U_MUTEX_DEFINE(s_ctrl);                                     // CREATE, REMOVE and the own ops, one at a time
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;  // A slot's type and state, read on another core
static prov_slot_t s_slots[CONFIG_SYS_DATA_PROVIDER_MAX];

// The slot of provider `id`, NULL when there is none.
static prov_slot_t* find(uint8_t id) {
  prov_slot_t* s = NULL;
  portENTER_CRITICAL(&s_lock);
  for (uint32_t i = 0; s == NULL && i < CONFIG_SYS_DATA_PROVIDER_MAX; i++) {
    s = s_slots[i].type != NULL && (uint8_t)s_slots[i].p.port.id == id ? &s_slots[i] : NULL;
  }
  portEXIT_CRITICAL(&s_lock);
  return s;
}

void sys_data_provider_type_register(const sys_data_provider_type_t* type) {
  assert(type != NULL && type->create != NULL && type->remove != NULL && type->in != NULL);
  assert(type->op_count == 0 || type->ops != NULL);
  for (uint16_t i = 0; i < type->op_count; i++) {
    assert((type->ops[i].op >> 8) == 0xFFu && "driver ops must use SYS_DATA_PROVIDER_OWN_OP");
  }
  assert(s_type_count < CONFIG_SYS_DATA_PROVIDER_TYPES && "more provider types than SYS_DATA_PROVIDER_TYPES");
  if (s_type_count < CONFIG_SYS_DATA_PROVIDER_TYPES) {
    s_types[s_type_count++] = type;
  }
}

// The input of every provider's port: the packet to its type's `in` as it is, one call into the driver at a time; a
// packet for a removed provider is dropped.
static void line_in(data_matrix_port_t* self, data_matrix_port_id_t from, uint8_t flags, const uint8_t* data,
                    uint32_t len) {
  prov_slot_t* s = DATA_MATRIX_OWNER(self, prov_slot_t, p.port);
  U_MUTEX_GUARD(s->line);
  const sys_data_provider_type_t* type = s->type;
  if (type != NULL) {
    type->in(self, from, flags, data, len);
  }
}

// CREATE under s_ctrl: the slot, the driver's create, the port in the data matrix.
static err_h create_locked(uint8_t id, uint8_t type_id, uint8_t flags, const void* cfg, uint32_t cfg_len) {
  ERR_CHECK(id != ID_NONE, ERR_PROV_BAD_ID, .provider = id);
  const sys_data_provider_type_t* type = NULL;
  for (uint32_t i = 0; type == NULL && i < s_type_count; i++) {
    type = s_types[i]->type == type_id ? s_types[i] : NULL;
  }
  ERR_CHECK(type != NULL, ERR_PROV_TYPE_UNKNOWN, .type = type_id);
  ERR_CHECK(cfg_len == type->cfg_size, ERR_CMD_BAD_LEN, .got = (uint16_t)cfg_len, .need = type->cfg_size);
  ERR_CHECK(find(id) == NULL, ERR_PROV_EXISTS, .provider = id);

  prov_slot_t* slot = NULL;
  for (uint32_t i = 0; slot == NULL && i < CONFIG_SYS_DATA_PROVIDER_MAX; i++) {
    slot = s_slots[i].type == NULL ? &s_slots[i] : NULL;  // a slot's type changes under s_ctrl only
  }
  ERR_CHECK(slot != NULL, ERR_PROV_FULL);
  if (slot->line == NULL) {
    slot->line = xSemaphoreCreateMutexStatic(&slot->line_buf);
  }

  sys_data_provider_t* p = &slot->p;
  // The port's `in` is never cleared (a send in flight may still read it): zeroed is only what follows the port.
  memset(slot->mem + sizeof(p->port), 0, sizeof(slot->mem) - sizeof(p->port));
  p->port.id = DATA_MATRIX_PORT(DATA_MATRIX_PORT_PROVIDER, id);
  p->port.in = line_in;
  p->state = (flags & CREATE_SYSTEM) != 0 ? SYS_DATA_PROVIDER_STATE_SYSTEM : 0u;
  ERR_TRY(type->create(p, cfg));

  portENTER_CRITICAL(&s_lock);
  slot->type = type;
  portEXIT_CRITICAL(&s_lock);
  err_h err = data_matrix_register(&p->port);  // no room in the data matrix: it can't be linked, it is not made
  if (err != NULL) {
    portENTER_CRITICAL(&s_lock);
    slot->type = NULL;
    portEXIT_CRITICAL(&s_lock);
    type->remove(p);
  }
  return err;
}

// REMOVE under s_ctrl: the port out of the data matrix first, then the driver, after the call into it in flight.
static err_h remove_locked(uint8_t id) {
  prov_slot_t* s = find(id);
  ERR_CHECK(s != NULL, ERR_PROV_NOT_FOUND, .provider = id);
  ERR_CHECK((s->p.state & SYS_DATA_PROVIDER_STATE_SYSTEM) == 0, ERR_PROV_SYSTEM, .provider = id);

  err_report(data_matrix_unregister(&s->p.port));
  U_MUTEX_GUARD(s->line);
  const sys_data_provider_type_t* type = s->type;
  portENTER_CRITICAL(&s_lock);
  s->type = NULL;
  portEXIT_CRITICAL(&s_lock);
  type->remove(&s->p);
  return NULL;
}

// LIST: [u8 count][entry_t...].
static err_h list(cmd_out_t* out) {
  entry_t rows[CONFIG_SYS_DATA_PROVIDER_MAX];
  uint8_t n = 0;
  portENTER_CRITICAL(&s_lock);
  for (uint32_t i = 0; i < CONFIG_SYS_DATA_PROVIDER_MAX; i++) {
    const prov_slot_t* s = &s_slots[i];
    if (s->type != NULL) {
      rows[n++] = (entry_t){
          .provider = (uint8_t)s->p.port.id,
          .type = s->type->type,
          .state = s->p.state,
      };
    }
  }
  portEXIT_CRITICAL(&s_lock);
  ERR_TRY(cmd_append(out, &n, sizeof(n)));
  return cmd_append(out, rows, (uint32_t)n * sizeof(rows[0]));
}

// OPS: [op u16][sides u8] rows, the contract's of a provider and its driver's own.
static err_h ops(uint8_t id, cmd_out_t* out) {
  static const struct __attribute__((packed)) {
    uint16_t op;
    uint8_t sides;
  } k_ops[] = {{CONTRACT_PROVIDER_OP_REMOVE, 0x02u}, {CONTRACT_PROVIDER_OP_OPS, 0x01u}};
  const prov_slot_t* s = find(id);
  ERR_CHECK(s != NULL, ERR_PROV_NOT_FOUND, .provider = id);
  ERR_TRY(cmd_append(out, k_ops, sizeof(k_ops)));
  for (uint16_t i = 0; i < s->type->op_count; i++) {
    const sys_data_provider_op_t* row = &s->type->ops[i];
    const uint8_t sides = (row->read != NULL ? 0x01u : 0u) | (row->write != NULL ? 0x02u : 0u);
    ERR_TRY(cmd_append(out, &row->op, sizeof(row->op)));
    ERR_TRY(cmd_append(out, &sides, sizeof(sides)));
  }
  return NULL;
}

// A driver's own op, under s_ctrl (the slot stays) and the provider's line mutex.
static err_h own_op_locked(uint8_t id, uint8_t dir, uint16_t op, cmd_in_t in, cmd_out_t* out) {
  prov_slot_t* s = find(id);
  ERR_CHECK(s != NULL, ERR_PROV_NOT_FOUND, .provider = id);
  cmd_f fn = NULL;
  for (uint16_t i = 0; fn == NULL && i < s->type->op_count; i++) {
    const sys_data_provider_op_t* row = &s->type->ops[i];
    fn = row->op != op ? NULL : dir == CMD_DIR_READ ? row->read : dir == CMD_DIR_WRITE ? row->write : NULL;
  }
  ERR_CHECK(fn != NULL, ERR_PROV_UNKNOWN_OP, .op = op);
  U_MUTEX_GUARD(s->line);
  return fn(&s->p, in, out);
}

err_h sys_data_provider_router(CMD_F_PARAMS) {
  (void)ctx;
  sys_data_provider_router_hdr_t h;
  const uint32_t got = in.len > 0 ? in.len - 1u : 0u;  // after the key
  ERR_CHECK(got >= sizeof(h), ERR_CMD_BAD_LEN, .got = (uint16_t)got, .need = sizeof(h));
  memcpy(&h, in.data + 1u, sizeof(h));  // little-endian target
  const cmd_in_t args = {.data = in.data + 1u + sizeof(h), .len = got - sizeof(h)};
  const bool read = h.dir == CMD_DIR_READ;
  const bool write = h.dir == CMD_DIR_WRITE;

  if (h.op == CONTRACT_PROVIDER_OP_CREATE && write) {
    ERR_CHECK(args.len >= CREATE_HDR, ERR_CMD_BAD_LEN, .got = (uint16_t)args.len, .need = CREATE_HDR);
    U_MUTEX_GUARD(s_ctrl);
    return create_locked(h.id, args.data[0], args.data[1], args.data + CREATE_HDR, args.len - CREATE_HDR);
  }
  if (h.op == CONTRACT_PROVIDER_OP_REMOVE && write) {
    U_MUTEX_GUARD(s_ctrl);
    return remove_locked(h.id);
  }
  if (h.op == CONTRACT_PROVIDER_OP_LIST && read) {
    return list(out);
  }
  if (h.op == CONTRACT_PROVIDER_OP_OPS && read) {
    return ops(h.id, out);
  }
  ERR_CHECK((h.op >> 8) == 0xFFu, ERR_PROV_UNKNOWN_OP, .op = h.op);
  U_MUTEX_GUARD(s_ctrl);
  return own_op_locked(h.id, h.dir, h.op, args, out);
}

ROUTER_MODULE_REGISTER(MODULE_ID_PROVIDER, sys_data_provider_router)

err_h sys_data_provider_create_system(uint8_t id, uint8_t type_id, const void* cfg, uint32_t cfg_len) {
  U_MUTEX_GUARD(s_ctrl);
  return create_locked(id, type_id, CREATE_SYSTEM, cfg, cfg_len);
}

void sys_data_provider_set_up(sys_data_provider_t* p, bool up) {
  portENTER_CRITICAL(&s_lock);
  p->state = up ? (uint8_t)(p->state | SYS_DATA_PROVIDER_STATE_UP) : (uint8_t)(p->state & ~SYS_DATA_PROVIDER_STATE_UP);
  portEXIT_CRITICAL(&s_lock);
}
