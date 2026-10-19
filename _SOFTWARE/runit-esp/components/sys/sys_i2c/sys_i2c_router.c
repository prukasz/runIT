#include "sys_i2c_router.h"

#include "sdkconfig.h"
#include "sys_i2c.h"

/*   key  op          request                          response
 *   0x01 INFO        -                                sys_i2c_info_t[SYS_I2C_BUS_COUNT]
 *   0x02 SCAN        { u8 bus }                       { u32 found[4] } bit n = address n acked
 *   0x03 RESET       { u8 bus }                       -
 *   0x04 RAW_READ    { u8 bus, addr, reg, len }       len bytes          (CONFIG_SYS_I2C_REMOTE_RAW)
 *   0x05 RAW_WRITE   { u8 bus, addr, reg } data[1..]  -                  (CONFIG_SYS_I2C_REMOTE_RAW) */

#define OP_INFO 0x01u
#define OP_SCAN 0x02u
#define OP_RESET 0x03u
#define OP_RAW_READ 0x04u
#define OP_RAW_WRITE 0x05u

#define SCAN_FIRST 0x08u      // 0x00-0x07 and 0x78-0x7F are reserved addresses
#define SCAN_LAST 0x77u
#define RAW_SPEED_HZ 100000u  // temporary device of the raw ops

#define INFO_ENABLED (1u << 0)
#define INFO_READY (1u << 1)
#define INFO_PULLUP (1u << 2)

static err_h check_remote_bus(uint8_t bus) {
  ERR_CHECK(bus == CONFIG_SYS_I2C_REMOTE_BUS, ERR_I2C_REMOTE_DENIED, .bus = bus);
  return NULL;
}

ROUTER_OP_NOARGS(op_info) {
  sys_i2c_info_t r[SYS_I2C_BUS_COUNT];
  for (uint8_t b = 0; b < SYS_I2C_BUS_COUNT; b++) {
    sys_i2c_bus_info_t i;
    ERR_TRY(sys_i2c_bus_info(b, &i));
    r[b] = (sys_i2c_info_t){
        .flags = (uint8_t)((i.enabled ? INFO_ENABLED : 0) | (i.ready ? INFO_READY : 0) | (i.pullup ? INFO_PULLUP : 0)),
        .sda = i.sda,
        .scl = i.scl,
    };
    memcpy(r[b].used, i.used, sizeof(r[b].used));
  }
  return cmd_reply(out, out_cap, out_len, r, sizeof(r));
}

ROUTER_OP(op_scan, sys_i2c_bus_args_t) {
  ERR_TRY(check_remote_bus(args->bus));
  sys_i2c_scan_t res = {{0}};
  for (uint8_t addr = SCAN_FIRST; addr <= SCAN_LAST; addr++) {
    err_h err = sys_i2c_probe(args->bus, addr);
    if (err == NULL) {
      res.found[addr / 32] |= 1u << (addr % 32);
    } else if (err->tag == ERR_I2C_DEV_NOT_FOUND) {
      err_free(err);
    } else {
      return err;  // stuck bus or driver failure: the rest of the scan would fail too
    }
  }
  return cmd_reply(out, out_cap, out_len, &res, sizeof(res));
}

ROUTER_OP(op_reset, sys_i2c_bus_args_t) {
  ERR_TRY(check_remote_bus(args->bus));
  return sys_i2c_bus_reset(args->bus);
}

#if CONFIG_SYS_I2C_REMOTE_RAW
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

ROUTER_OP(op_raw_read, sys_i2c_raw_read_args_t) {
  ERR_TRY(check_remote_bus(args->bus));
  ERR_CHECK(args->len > 0 && args->len <= out_cap, ERR_CMD_OUT_FULL, .need = args->len, .cap = (uint16_t)out_cap);

  sys_i2c_dev_t dev;
  sys_i2c_dev_init(&dev, args->bus, args->addr, RAW_SPEED_HZ);
  ERR_TRY(sys_i2c_dev_add(&dev));
  err_h err = raw_finish(&dev, sys_i2c_reg_read(&dev, args->reg, out, args->len));
  if (err == NULL) {
    *out_len = args->len;
  }
  return err;
}

ROUTER_OP_TAIL(op_raw_write, sys_i2c_raw_write_args_t) {
  ERR_TRY(check_remote_bus(args->bus));

  sys_i2c_dev_t dev;
  sys_i2c_dev_init(&dev, args->bus, args->addr, RAW_SPEED_HZ);
  ERR_TRY(sys_i2c_dev_add(&dev));
  return raw_finish(&dev, sys_i2c_reg_write(&dev, args->reg, tail, tail_len));
}
#endif

static const router_entry_t k_ops[] = {
    {OP_INFO, op_info},
    {OP_SCAN, op_scan},
    {OP_RESET, op_reset},
#if CONFIG_SYS_I2C_REMOTE_RAW
    {OP_RAW_READ, op_raw_read},
    {OP_RAW_WRITE, op_raw_write},
#endif
};

ROUTER_MODULE(sys_i2c_router, k_ops)
