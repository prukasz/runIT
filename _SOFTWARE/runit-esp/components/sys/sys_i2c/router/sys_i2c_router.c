#include "sys_i2c_router.h"

#include "sys_i2c.h"

#define SCAN_ADDR_FIRST 0x08u  // 0x00-0x07 and 0x78-0x7F are reserved addresses
#define SCAN_ADDR_LAST  0x77u
#define RAW_SPEED_HZ    100000u  // temporary device of the raw ops

ROUTER_OP(op_scan, sys_i2c_bus_args_t) {
  sys_i2c_scan_t res = {{0}};
  for (uint8_t addr = SCAN_ADDR_FIRST; addr <= SCAN_ADDR_LAST; addr++) {
    err_h err = sys_i2c_probe(args->bus, addr);
    if (err == NULL) {
      res.found[addr / 32] |= 1u << (addr % 32);
    } else if (err->tag == ERR_I2C_DEV_NOT_FOUND) {
      err_free(err);
    } else {
      return err;  // stuck bus or driver failure: the rest of the scan would fail too
    }
  }
  return cmd_reply(out, &res, sizeof(res));
}

ROUTER_OP(op_bus_create, sys_i2c_bus_cfg_t) {
  return sys_i2c_bus_create(args);
}

ROUTER_OP(op_bus_get, sys_i2c_bus_args_t) {
  sys_i2c_bus_cfg_t cfg;
  ERR_TRY(sys_i2c_bus_get(args->bus, &cfg));
  return cmd_reply(out, &cfg, sizeof(cfg));
}

ROUTER_OP(op_bus_remove, sys_i2c_bus_args_t) {
  return sys_i2c_bus_remove(args->bus);
}

ROUTER_OP(op_reset, sys_i2c_bus_args_t) {
  return sys_i2c_bus_reset(args->bus);
}

/* Raw access goes through a temporary device: dev_add claims the address (so it
 * fails on a chip a driver owns) and probes it; removed again afterwards. The
 * transfer's error wins over the remove's. */
static err_h raw_finish(sys_i2c_dev_t* dev, err_h err) {
  err_h rm = sys_i2c_dev_remove(dev);
  if (err == NULL) {
    return rm;
  }
  err_free(rm);
  return err;
}

// The answer is the write's layout (the mirror): the address, then the bytes read.
ROUTER_OP(op_raw_read, sys_i2c_raw_read_args_t) {
  const sys_i2c_raw_write_args_t head = {.bus = args->bus, .addr = args->addr, .reg = args->reg};
  const uint32_t need = sizeof(head) + args->len;
  ERR_CHECK(args->len > 0 && need <= out->cap, ERR_CMD_OUT_FULL, .need = (uint16_t)need, .cap = (uint16_t)out->cap);

  sys_i2c_dev_t dev;
  sys_i2c_dev_init(&dev, args->bus, args->addr, RAW_SPEED_HZ);
  ERR_TRY(sys_i2c_dev_add(&dev));
  err_h err = raw_finish(&dev, sys_i2c_reg_read(&dev, args->reg, out->data + sizeof(head), args->len));
  if (err == NULL) {
    memcpy(out->data, &head, sizeof(head));
    out->len = need;
  }
  return err;
}

ROUTER_OP_TAIL(op_raw_write, sys_i2c_raw_write_args_t) {
  sys_i2c_dev_t dev;
  sys_i2c_dev_init(&dev, args->bus, args->addr, RAW_SPEED_HZ);
  ERR_TRY(sys_i2c_dev_add(&dev));
  return raw_finish(&dev, sys_i2c_reg_write(&dev, args->reg, tail.data, tail.len));
}

static const router_op_t k_ops[] = {
    ROUTER_R(SYS_I2C_OP_SCAN, op_scan),
    ROUTER_W(SYS_I2C_OP_RESET, op_reset),
    ROUTER_RW(SYS_I2C_OP_BUS_CREATE, op_bus_get, op_bus_create),
    ROUTER_W(SYS_I2C_OP_BUS_REMOVE, op_bus_remove),
    ROUTER_RW(SYS_I2C_OP_RAW, op_raw_read, op_raw_write),
};

ROUTER_MODULE(sys_i2c_router, k_ops)
