/**
 * @file sys_data_provider.c
 * @brief The provider registry (SYS_DATA_PROVIDER.MD): the driver types, the providers' slots, their lifecycle (CREATE,
 * REMOVE, the port in the data matrix) and one call into a driver at a time. The wire format is the router's
 * (router/sys_data_provider_router.c, over sys_data_provider_internal.h).
 */

#include "sys_data_provider.h"

#include <assert.h>
#include <string.h>

#include "data_matrix.h"
#include "sdkconfig.h"
#include "sys_data_provider_driver.h"
#include "sys_data_provider_internal.h"
#include "utils.h"

ERR_MODULE_DEFINE(MODULE_ID_PROVIDER, ERR_SYS_DATA_PROVIDER_MAP)

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

__attribute__((constructor)) static void slots_init(void) {
  for (uint32_t i = 0; i < CONFIG_SYS_DATA_PROVIDER_MAX; i++) {
    s_slots[i].line = xSemaphoreCreateMutexStatic(&s_slots[i].line_buf);
  }
}

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

err_h sys_data_provider_create(uint8_t id, uint8_t type_id, bool system, const void* cfg, uint32_t cfg_len) {
  U_MUTEX_GUARD(s_ctrl);
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

  sys_data_provider_t* p = &slot->p;
  // The port's `in` is never cleared (a send in flight may still read it): zeroed is only what follows the port.
  memset(slot->mem + sizeof(p->port), 0, sizeof(slot->mem) - sizeof(p->port));
  p->port.id = DATA_MATRIX_PORT(DATA_MATRIX_PORT_PROVIDER, id);
  p->port.in = line_in;
  p->state = system ? SYS_DATA_PROVIDER_STATE_SYSTEM : 0u;
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

err_h sys_data_provider_remove(uint8_t id) {
  U_MUTEX_GUARD(s_ctrl);
  prov_slot_t* s = find(id);
  ERR_CHECK(s != NULL, ERR_PROV_NOT_FOUND, .provider = id);
  ERR_CHECK((s->p.state & SYS_DATA_PROVIDER_STATE_SYSTEM) == 0, ERR_PROV_SYSTEM, .provider = id);

  err_report(data_matrix_unregister(&s->p.port));  // out of the data matrix first: its links go before its driver
  const sys_data_provider_type_t* type = s->type;
  {
    U_MUTEX_GUARD(s->line);  // after the call into the driver in flight; a packet after it finds no type
    portENTER_CRITICAL(&s_lock);
    s->type = NULL;
    portEXIT_CRITICAL(&s_lock);
  }
  // Outside the line's mutex: the driver's remove may wait for its RX callbacks, and one of them may be sending to
  // this provider's port (its targets copied before the unregister) and wait for that mutex.
  type->remove(&s->p);
  return NULL;
}

uint32_t sys_data_provider_list(sys_data_provider_row_t* rows) {
  uint32_t n = 0;
  portENTER_CRITICAL(&s_lock);
  for (uint32_t i = 0; i < CONFIG_SYS_DATA_PROVIDER_MAX; i++) {
    const prov_slot_t* s = &s_slots[i];
    if (s->type != NULL) {
      rows[n++] = (sys_data_provider_row_t){.id = (uint8_t)s->p.port.id, .type = s->type->type, .state = s->p.state};
    }
  }
  portEXIT_CRITICAL(&s_lock);
  return n;
}

err_h sys_data_provider_own_op(uint8_t id, uint8_t dir, uint16_t op, cmd_in_t in, cmd_out_t* out) {
  U_MUTEX_GUARD(s_ctrl);  // no CREATE / REMOVE meanwhile: the slot stays
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

err_h sys_data_provider_create_system(uint8_t id, uint8_t type_id, const void* cfg, uint32_t cfg_len) {
  return sys_data_provider_create(id, type_id, true, cfg, cfg_len);
}

void sys_data_provider_set_up(sys_data_provider_t* p, bool up) {
  portENTER_CRITICAL(&s_lock);
  p->state = up ? (uint8_t)(p->state | SYS_DATA_PROVIDER_STATE_UP) : (uint8_t)(p->state & ~SYS_DATA_PROVIDER_STATE_UP);
  portEXIT_CRITICAL(&s_lock);
}
