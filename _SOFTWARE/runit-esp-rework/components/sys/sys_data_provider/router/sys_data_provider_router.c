/**
 * @file sys_data_provider_router.c
 * @brief The router of MODULE_ID_PROVIDER (contract_provider.json): the wire format of the provider commands, over the
 * registry's seam (sys_data_provider_internal.h).
 */

#include "sys_data_provider_router.h"

#include <string.h>

#include "sdkconfig.h"
#include "sys_data_provider_internal.h"

// Op codes: MODULE_ID_PROVIDER in the high byte; never renumber.
#define CONTRACT_PROVIDER_OP_CREATE ((uint16_t)((MODULE_ID_PROVIDER << 8) | 0x00u))  // W [type u8][flags u8][cfg]
#define CONTRACT_PROVIDER_OP_REMOVE ((uint16_t)((MODULE_ID_PROVIDER << 8) | 0x01u))  // W
#define CONTRACT_PROVIDER_OP_LIST   ((uint16_t)((MODULE_ID_PROVIDER << 8) | 0x02u))  // R [u8 count][row...]
#define CONTRACT_PROVIDER_OP_OPS    ((uint16_t)((MODULE_ID_PROVIDER << 8) | 0x05u))  // R [op u16][sides u8]...
#define CREATE_HDR                  2u     // [type u8][flags u8] before the cfg
#define CREATE_SYSTEM               0x01u  // CREATE flag: the board's own, not removed

// A LIST row on the wire is the registry's row as it is: [provider u8][type u8][state u8].
_Static_assert(sizeof(sys_data_provider_row_t) == 3, "LIST row: 3 B");

// LIST: [u8 count][row...].
static err_h list(cmd_out_t* out) {
  sys_data_provider_row_t rows[CONFIG_SYS_DATA_PROVIDER_MAX];
  const uint8_t n = (uint8_t)sys_data_provider_list(rows);
  ERR_TRY(cmd_append(out, &n, sizeof(n)));
  return cmd_append(out, rows, (uint32_t)n * sizeof(rows[0]));
}

// OPS: [op u16][sides u8] rows, the contract's of a provider and its driver's own.
static err_h ops(uint8_t id, cmd_out_t* out) {
  static const struct __attribute__((packed)) {
    uint16_t op;
    uint8_t sides;
  } k_ops[] = {{CONTRACT_PROVIDER_OP_REMOVE, 0x02u}, {CONTRACT_PROVIDER_OP_OPS, 0x01u}};
  const sys_data_provider_type_t* type = sys_data_provider_type_of(id);
  ERR_CHECK(type != NULL, ERR_PROV_NOT_FOUND, .provider = id);
  ERR_TRY(cmd_append(out, k_ops, sizeof(k_ops)));
  for (uint16_t i = 0; i < type->op_count; i++) {
    const sys_data_provider_op_t* row = &type->ops[i];
    const uint8_t sides = (row->read != NULL ? 0x01u : 0u) | (row->write != NULL ? 0x02u : 0u);
    ERR_TRY(cmd_append(out, &row->op, sizeof(row->op)));
    ERR_TRY(cmd_append(out, &sides, sizeof(sides)));
  }
  return NULL;
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
    return sys_data_provider_create(h.id, args.data[0], (args.data[1] & CREATE_SYSTEM) != 0, args.data + CREATE_HDR,
                                    args.len - CREATE_HDR);
  }
  if (h.op == CONTRACT_PROVIDER_OP_REMOVE && write) {
    return sys_data_provider_remove(h.id);
  }
  if (h.op == CONTRACT_PROVIDER_OP_LIST && read) {
    return list(out);
  }
  if (h.op == CONTRACT_PROVIDER_OP_OPS && read) {
    return ops(h.id, out);
  }
  ERR_CHECK((h.op >> 8) == 0xFFu, ERR_PROV_UNKNOWN_OP, .op = h.op);
  return sys_data_provider_own_op(h.id, h.dir, h.op, args, out);
}

ROUTER_MODULE_REGISTER(MODULE_ID_PROVIDER, sys_data_provider_router)
