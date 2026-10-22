#include "vm_block_device.h"

#include "vm_blocks.h"

#define OUT_BYTES 12u  // the largest answer: a mirror read's key and two i32

// The row of `op` on side `dir`: READ rows are the op's read side, the others its write side.
static const vm_dev_op_t* op_find(const vm_dev_contract_t* c, uint16_t op, uint8_t dir) {
  for (uint8_t i = 0; i < c->count; i++) {
    if (c->ops[i].op == op && (c->ops[i].act == VM_DEV_ACT_READ) == (dir == CMD_DIR_READ)) {
      return &c->ops[i];
    }
  }
  return NULL;
}

// Outputs a read operation fills, and the bytes it answers.
static uint8_t result_count(const vm_dev_op_t* op) {
  switch (op->result) {
    case VM_DEV_R_NONE:
      return 0;
    case VM_DEV_R_I32X2:
    case VM_DEV_R_U8X2:
      return 2;
    case VM_DEV_R_U8X4:
      return 4;
    default:
      return 1;
  }
}

static bool result_is_u8(const vm_dev_op_t* op) {
  return op->result == VM_DEV_R_U8 || op->result == VM_DEV_R_U8X2 || op->result == VM_DEV_R_U8X4;
}

static uint8_t ftype_size(uint8_t t) {
  switch (t) {
    case VM_DEV_F_U8:
      return 1;
    case VM_DEV_F_I16:
      return 2;
    default:
      return 4;
  }
}

// A scaled field's value, in wire units: the block's float times the scale, rounded, saturated to the field's range.
static int32_t scaled(const vm_dev_op_t* op, uint8_t i, float f) {
  if (!isfinite(f)) {
    return 0;
  }
  float lo = -2147483520.0f, hi = 2147483520.0f;
  switch (op->ftype[i]) {
    case VM_DEV_F_U8:
      lo = 0, hi = UINT8_MAX;
      break;
    case VM_DEV_F_I16:
      lo = INT16_MIN, hi = INT16_MAX;
      break;
    case VM_DEV_F_U32:
      lo = 0;  // a u32 field stays within i32 here: no scaled field of the tables is that large
      break;
    default:
      break;
  }
  const float x = roundf(f * op->scale[i]);
  return (int32_t)(x < lo ? lo : x > hi ? hi : x);
}

// A field with a limit is saturated to it: -lim..lim, 0..lim for an unsigned field.
static int32_t limited(const vm_dev_op_t* op, uint8_t i, int32_t v) {
  const int32_t lim = op->lim[i];
  if (lim <= 0) {
    return v;
  }
  const int32_t lo = op->ftype[i] == VM_DEV_F_U8 || op->ftype[i] == VM_DEV_F_U32 ? 0 : -lim;
  return v < lo ? lo : v > lim ? lim : v;
}

// A constant of the state as the field's value (not yet limited): a scaled field keeps a float.
static int32_t const_value(const vm_dev_op_t* op, uint8_t i, int32_t raw) {
  if (op->scale[i] <= 0.0f) {
    return raw;
  }
  float f;
  memcpy(&f, &raw, sizeof(f));
  return scaled(op, i, f);
}

// The value of field `i` as read: its input when wired, else the constant. Out: the field as an i32 (u32 kept as its bits)
// and, for a scaled field, the value in the block's unit.
static err_h field_read(vm_block_h b, const vm_dev_op_t* op, const vm_block_device_data_t* d, uint8_t i, int32_t* out,
                        float* shown) {
  const vm_accessor_t* in = vm_block_optional_in(b, i);
  if (in == NULL) {
    if (op->scale[i] > 0.0f) {
      memcpy(shown, &d->k[i], sizeof(*shown));
    }
    *out = const_value(op, i, d->k[i]);
    return NULL;
  }
  if (op->scale[i] > 0.0f) {
    float f = 0;
    ERR_TRY(VM_OBJ_SCALAR_GET(f, in));
    *shown = f;
    *out = scaled(op, i, f);
    return NULL;
  }
  switch (op->ftype[i]) {
    case VM_DEV_F_U8: {
      uint8_t v = 0;
      ERR_TRY(VM_OBJ_SCALAR_GET(v, in));
      *out = v;
      return NULL;
    }
    case VM_DEV_F_U32: {
      uint32_t v = 0;
      ERR_TRY(VM_OBJ_SCALAR_GET(v, in));
      *out = (int32_t)v;
      return NULL;
    }
    case VM_DEV_F_I16: {
      int32_t v = 0;
      ERR_TRY(VM_OBJ_SCALAR_GET(v, in));
      *out = v > INT16_MAX ? INT16_MAX : v < INT16_MIN ? INT16_MIN : v;
      return NULL;
    }
    default:
      return VM_OBJ_SCALAR_GET(*out, in);
  }
}

// The value of field `i`, saturated to the field's limit. A value it held is reported once while it stays held.
static err_h field_get(vm_block_h b, const vm_dev_op_t* op, vm_block_device_data_t* d, uint8_t i, int32_t* out,
                       bool* held) {
  float shown = 0;
  ERR_TRY(field_read(b, op, d, i, out, &shown));
  const int32_t v = limited(op, i, *out);
  if (v == *out) {
    return NULL;
  }
  *held = true;
  if (!(d->flags & VM_DEV_F_HELD)) {
    d->flags |= VM_DEV_F_HELD;
    const float unit = op->scale[i] > 0.0f ? op->scale[i] : 1.0f;
    const int32_t lo = limited(op, i, INT32_MIN);
    VM_BLK_EMIT_ERR(ERR_VM_DEV_HELD, .block_idx = b->cfg.block_idx, .field = i,
                    .value = op->scale[i] > 0.0f ? shown : (float)*out, .min = (float)lo / unit,
                    .max = (float)op->lim[i] / unit);
  }
  *out = v;
  return NULL;
}

// Packs the fields into the op's packed argument struct (little-endian, the wire form). Returns its length.
static uint32_t args_pack(const vm_dev_op_t* op, const int32_t* v, uint8_t* out) {
  uint32_t n = 0;
  for (uint8_t i = 0; i < op->nfield; i++) {
    const uint8_t size = ftype_size(op->ftype[i]);
    memcpy(out + n, &v[i], size);  // little-endian: the low bytes
    n += size;
  }
  return n;
}

static err_h call(const vm_block_device_data_t* d, const vm_dev_op_t* op, const int32_t* v, uint8_t* result) {
  uint8_t args[VM_DEV_ARGS_BYTES];
  const uint32_t len = args_pack(op, v, args);
  uint8_t out[OUT_BYTES];
  uint32_t out_len = 0;
  const bool read = op->act == VM_DEV_ACT_READ;
  ERR_TRY(sys_device_call(d->device_id, read ? CMD_DIR_READ : CMD_DIR_WRITE, op->op, args, len, out, sizeof(out),
                          &out_len));
  if (op->result != VM_DEV_R_NONE) {
    const uint32_t want = result_count(op) * (result_is_u8(op) ? 1u : 4u);
    const uint32_t skip = out_len == want + len ? len : 0;  // the read of a mirror op answers its key first
    ERR_CHECK(out_len - skip == want, ERR_VM_RANGE, .val = out_len, .min = want, .max = want);
    memcpy(result, out + skip, want);
  }
  return NULL;
}

// The idle operation: the address fields as last sent, the rest from the constants.
static void idle_send(vm_block_h b, vm_block_device_data_t* d, const vm_dev_contract_t* c) {
  const vm_dev_op_t* op = op_find(c, d->idle_op, CMD_DIR_WRITE);
  if (op == NULL) {
    return;
  }
  int32_t v[VM_DEV_FIELDS_MAX] = {0};
  for (uint8_t i = 0; i < op->nfield; i++) {
    v[i] = i < c->naddr ? d->last[i] : limited(op, i, const_value(op, i, d->idle_k[i - c->naddr]));
  }
  BLOCK_CALL(call(d, op, v, NULL), b);
  if (!vm_block_failed(b)) {
    d->flags |= VM_DEV_F_IDLE;
    d->flags &= (uint8_t)~VM_DEV_F_SENT;  // the next enabled pass sends again
  }
}

void vm_device_run(vm_block_h b, const vm_dev_contract_t* c) {
  vm_block_device_data_t* d = (vm_block_device_data_t*)vm_block_get_custom_data(b);
  const vm_dev_op_t* op = op_find(c, d->op, d->dir);  // checked at load
  const bool en = vm_block_is_enabled(b);
  const bool rising = en && !(d->flags & VM_DEV_F_PREV_EN);
  d->flags = (uint8_t)(en ? (d->flags | VM_DEV_F_PREV_EN) : (d->flags & ~VM_DEV_F_PREV_EN));

  if (op == NULL) {
    vm_block_set_eno(b, false);
    return;
  }
  if (!en) {
    if (d->idle_op != 0 && (d->flags & VM_DEV_F_SENT) && !(d->flags & VM_DEV_F_IDLE)) {
      idle_send(b, d, c);
    }
    vm_block_set_eno(b, false);
    return;
  }
  d->flags &= (uint8_t)~VM_DEV_F_IDLE;

  int32_t v[VM_DEV_FIELDS_MAX] = {0};
  bool held = false;
  for (uint8_t i = 0; i < op->nfield; i++) {
    if (!vm_block_check(b, field_get(b, op, d, i, &v[i], &held))) {
      vm_block_set_eno(b, false);
      return;
    }
  }
  if (!held) {
    d->flags &= (uint8_t)~VM_DEV_F_HELD;  // back in range: the next held value is reported again
  }

  bool fired = true;
  if (op->act == VM_DEV_ACT_EDGE) {
    fired = rising;
  } else if (op->act == VM_DEV_ACT_CHANGE) {
    fired = !(d->flags & VM_DEV_F_SENT) || memcmp(v, d->last, sizeof(int32_t) * op->nfield) != 0;
  }
  if (!fired) {
    vm_block_set_eno(b, op->act != VM_DEV_ACT_EDGE);
    return;
  }

  uint8_t result[OUT_BYTES] = {0};  // the answer as the device sent it (little-endian)
  BLOCK_CALL(call(d, op, v, result), b);
  if (vm_block_failed(b)) {
    vm_block_set_eno(b, false);
    return;
  }
  if (op->act == VM_DEV_ACT_READ) {
    for (uint8_t i = 0; i < b->cfg.q_cnt; i++) {
      vm_obj_h q = vm_block_get_outputs(b)[i];
      if (result_is_u8(op)) {
        const uint8_t r = result[i];
        BLOCK_CALL(VM_OBJ_SET_SCALAR(r, q), b);
      } else if (op->rscale[i] > 0.0f) {
        int32_t r;
        memcpy(&r, result + i * sizeof(r), sizeof(r));
        const float f = (float)r / op->rscale[i];
        BLOCK_CALL(VM_OBJ_SET_SCALAR(f, q), b);
      } else {
        int32_t r;
        memcpy(&r, result + i * sizeof(r), sizeof(r));
        BLOCK_CALL(VM_OBJ_SET_SCALAR(r, q), b);
      }
    }
  } else {
    memcpy(d->last, v, sizeof(d->last));
    d->flags |= VM_DEV_F_SENT;
  }
  vm_block_set_eno(b, !vm_block_failed(b));
}

bool vm_device_check(vm_block_h b, const vm_dev_contract_t* c) {
  vm_block_device_data_t d;
  memcpy(&d, vm_block_get_custom_data(b), sizeof(d));
  if (d.device_id == SYS_DEVICE_ID_NONE || d.dir > CMD_DIR_WRITE) {
    return false;
  }
  const vm_dev_op_t* op = op_find(c, d.op, d.dir);
  if (op == NULL || b->cfg.in_cnt > op->nfield) {
    return false;
  }
  if (b->cfg.q_cnt > result_count(op)) {
    return false;
  }
  if (d.idle_op == 0) {
    return true;
  }
  // an idle operation: a send, on the same address fields (the ones last sent)
  const vm_dev_op_t* idle = op_find(c, d.idle_op, CMD_DIR_WRITE);
  if (idle == NULL || op->act == VM_DEV_ACT_READ || idle->act == VM_DEV_ACT_READ) {
    return false;
  }
  if (op->nfield < c->naddr || idle->nfield < c->naddr) {
    return false;
  }
  for (uint8_t i = 0; i < c->naddr; i++) {
    if (idle->ftype[i] != op->ftype[i]) {
      return false;
    }
  }
  return true;
}
