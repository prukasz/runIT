#include "sys_i2c.h"

#include "sys_device_driver.h"

ERR_MODULE_DEFINE(ERR_MOD_SYS_I2C, "sys_i2c", ERR_SYS_I2C_MAP)

SYS_DEVICE_CONTRACT_DEFINE(I2C, 0x02)

/* The client: every call is an op of the chip's bus. Nothing here knows how a bus is made. */

static err_h check_dev(const sys_i2c_dev_t* dev) {
  ERR_CHECK(dev != NULL, ERR_I2C_BAD_ARG);
  ERR_CHECK(dev->attached, ERR_I2C_DEV_NOT_ADDED, .bus = dev->bus, .addr = dev->addr);
  return NULL;
}

static err_h xfer(const sys_i2c_dev_t* dev, const uint8_t* w1, size_t n1, const uint8_t* w2, size_t n2, uint8_t* r,
                  size_t rn) {
  const i2c_xfer_t x = {.addr = dev->addr, .w1 = w1, .n1 = n1, .w2 = w2, .n2 = n2, .r = r, .rn = rn};
  return SYS_DEVICE_WRITE(dev->bus, CONTRACT_I2C, OP_XFER, &x);
}

static err_h reg_write(const sys_i2c_dev_t* dev, uint8_t reg, const uint8_t* data, size_t len) {
  err_h e = xfer(dev, &reg, 1, data, len, NULL, 0);
  return e != NULL ? ERR_WRAP(e, ERR_I2C_REG, .reg = reg, .write = 1) : NULL;
}

static err_h reg_read(const sys_i2c_dev_t* dev, uint8_t reg, uint8_t* data, size_t len) {
  err_h e = xfer(dev, &reg, 1, NULL, 0, data, len);
  return e != NULL ? ERR_WRAP(e, ERR_I2C_REG, .reg = reg, .write = 0) : NULL;
}

/* ----------------------------------------------------------------------------
 * A chip on a bus
 * ------------------------------------------------------------------------- */

// The owner's resource: DETACH of an attached chip; not attached afterwards even when the bus failed.
static err_h dev_release(void* arg) {
  sys_i2c_dev_t* dev = arg;
  if (!dev->attached) {
    return NULL;
  }
  dev->attached = 0;
  return SYS_DEVICE_SET(dev->bus, CONTRACT_I2C, OP_DETACH, .addr = dev->addr);
}

err_h sys_i2c_dev_attach(uint8_t owner, sys_i2c_dev_t* dev, uint8_t bus, uint8_t addr, uint32_t speed_Hz) {
  ERR_CHECK(dev != NULL, ERR_I2C_BAD_ARG);
  dev->bus = bus;
  dev->addr = addr;
  ERR_TRY(sys_device_require_op(owner, bus, CONTRACT_I2C_OP_XFER, CMD_DIR_WRITE));  // declares the use as well
  ERR_TRY(sys_device_res_add(owner, &dev->res, dev_release, dev));         // before the address is taken
  ERR_TRY(SYS_DEVICE_SET(bus, CONTRACT_I2C, OP_ATTACH, .addr = addr, .speed_Hz = speed_Hz));
  dev->attached = 1;
  return NULL;
}

/* ----------------------------------------------------------------------------
 * Transfers
 * ------------------------------------------------------------------------- */

err_h sys_i2c_write(const sys_i2c_dev_t* dev, const uint8_t* data, size_t len) {
  ERR_TRY(check_dev(dev));
  ERR_CHECK(data != NULL && len > 0, ERR_I2C_BAD_ARG);
  return xfer(dev, data, len, NULL, 0, NULL, 0);
}

err_h sys_i2c_read(const sys_i2c_dev_t* dev, uint8_t* data, size_t len) {
  ERR_TRY(check_dev(dev));
  ERR_CHECK(data != NULL && len > 0, ERR_I2C_BAD_ARG);
  return xfer(dev, NULL, 0, NULL, 0, data, len);
}

err_h sys_i2c_write_read(const sys_i2c_dev_t* dev, const uint8_t* wdata, size_t wlen, uint8_t* rdata, size_t rlen) {
  ERR_TRY(check_dev(dev));
  ERR_CHECK(wdata != NULL && wlen > 0 && rdata != NULL && rlen > 0, ERR_I2C_BAD_ARG);
  return xfer(dev, wdata, wlen, NULL, 0, rdata, rlen);
}

/* ----------------------------------------------------------------------------
 * Register helpers
 * ------------------------------------------------------------------------- */

err_h sys_i2c_reg_write(const sys_i2c_dev_t* dev, uint8_t reg, const uint8_t* data, size_t len) {
  ERR_TRY(check_dev(dev));
  ERR_CHECK(data != NULL && len > 0, ERR_I2C_BAD_ARG);
  return reg_write(dev, reg, data, len);
}

err_h sys_i2c_reg_read(const sys_i2c_dev_t* dev, uint8_t reg, uint8_t* data, size_t len) {
  ERR_TRY(check_dev(dev));
  ERR_CHECK(data != NULL && len > 0, ERR_I2C_BAD_ARG);
  return reg_read(dev, reg, data, len);
}

err_h sys_i2c_reg_read_u16(const sys_i2c_dev_t* dev, uint8_t reg, uint16_t* val, sys_i2c_order_e order) {
  ERR_TRY(check_dev(dev));
  ERR_CHECK(val != NULL && (order == SYS_I2C_BE || order == SYS_I2C_LE), ERR_I2C_BAD_ARG);
  uint8_t b[2];
  ERR_TRY(reg_read(dev, reg, b, sizeof(b)));
  *val = order == SYS_I2C_BE ? (uint16_t)((b[0] << 8) | b[1]) : (uint16_t)((b[1] << 8) | b[0]);
  return NULL;
}

err_h sys_i2c_reg_write_u16(const sys_i2c_dev_t* dev, uint8_t reg, uint16_t val, sys_i2c_order_e order) {
  ERR_TRY(check_dev(dev));
  ERR_CHECK(order == SYS_I2C_BE || order == SYS_I2C_LE, ERR_I2C_BAD_ARG);
  const uint8_t hi = (uint8_t)(val >> 8);
  const uint8_t lo = (uint8_t)val;
  const uint8_t b[2] = {order == SYS_I2C_BE ? hi : lo, order == SYS_I2C_BE ? lo : hi};
  return reg_write(dev, reg, b, sizeof(b));
}

err_h sys_i2c_reg_update(const sys_i2c_dev_t* dev, uint8_t reg, uint8_t mask, uint8_t val) {
  ERR_TRY(check_dev(dev));
  uint8_t r = 0;
  ERR_TRY(reg_read(dev, reg, &r, 1));
  r = (uint8_t)((r & ~mask) | (val & mask));
  return reg_write(dev, reg, &r, 1);
}
