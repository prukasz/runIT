#include "sys_i2c.h"

#include "sdkconfig.h"

ERR_MODULE_DEFINE(ERR_MOD_SYS_I2C, "sys_i2c", ERR_SYS_I2C_MAP)

#define ADDR_7BIT_MAX 0x7Fu

/* ----------------------------------------------------------------------------
 * Bus table (from Kconfig)
 * ------------------------------------------------------------------------- */

typedef struct {
  bool enabled;
  bool pullup;
  i2c_port_num_t port;
  uint8_t sda;
  uint8_t scl;
} bus_cfg_t;

#if CONFIG_SYS_I2C_BUS0_PULLUP
#define BUS0_PULLUP true
#else
#define BUS0_PULLUP false
#endif
#if CONFIG_SYS_I2C_BUS1_PULLUP
#define BUS1_PULLUP true
#else
#define BUS1_PULLUP false
#endif

// A disabled bus stays all-zero (enabled = false).
static const bus_cfg_t k_bus_cfg[SYS_I2C_BUS_COUNT] = {
#if CONFIG_SYS_I2C_BUS0_ENABLE
    [SYS_I2C_BUS_0] = {.enabled = true, .pullup = BUS0_PULLUP, .port = I2C_NUM_0,
                       .sda = CONFIG_SYS_I2C_BUS0_SDA, .scl = CONFIG_SYS_I2C_BUS0_SCL},
#endif
#if CONFIG_SYS_I2C_BUS1_ENABLE
    [SYS_I2C_BUS_1] = {.enabled = true, .pullup = BUS1_PULLUP, .port = I2C_NUM_1,
                       .sda = CONFIG_SYS_I2C_BUS1_SDA, .scl = CONFIG_SYS_I2C_BUS1_SCL},
#endif
};

static struct {
  i2c_master_bus_handle_t bus[SYS_I2C_BUS_COUNT];  // written only by sys_i2c_init (boot), read-only after that
  // Addresses with a device added, 1 bit per 7-bit address per bus. Under s_addr_lock.
  uint32_t addr_used[SYS_I2C_BUS_COUNT][(ADDR_7BIT_MAX + 1) / 32];
} s_i2c;
_Static_assert(sizeof(s_i2c.addr_used[0]) == sizeof(((sys_i2c_bus_info_t){0}).used), "bus info map size");
static U_SPINLOCK_DEFINE(s_addr_lock);

/* ----------------------------------------------------------------------------
 * Internal helpers - callers have validated their arguments.
 * ------------------------------------------------------------------------- */

static err_h check_bus(uint8_t bus) {
  ERR_CHECK(sys_i2c_bus_ready(bus), ERR_I2C_BAD_BUS, .bus = bus);
  return NULL;
}

static err_h check_dev(const sys_i2c_dev_t* dev) {
  ERR_CHECK(dev != NULL, ERR_I2C_BAD_ARG);
  ERR_CHECK(dev->handle != NULL, ERR_I2C_DEV_NOT_ADDED, .bus = dev->bus, .addr = dev->addr);
  return NULL;
}

// Marks the address used. False when it already was.
static bool addr_claim(uint8_t bus, uint8_t addr) {
  uint32_t bit = 1u << (addr % 32);
  bool free;
  U_CRITICAL_ENTER(s_addr_lock);
  free = (s_i2c.addr_used[bus][addr / 32] & bit) == 0;
  s_i2c.addr_used[bus][addr / 32] |= bit;
  U_CRITICAL_EXIT(s_addr_lock);
  return free;
}

static void addr_release(uint8_t bus, uint8_t addr) {
  U_CRITICAL_ENTER(s_addr_lock);
  s_i2c.addr_used[bus][addr / 32] &= ~(1u << (addr % 32));
  U_CRITICAL_EXIT(s_addr_lock);
}

// Probe of a ready bus and a 7-bit address.
static err_h probe(uint8_t bus, uint8_t addr) {
  esp_err_t rc = i2c_master_probe(s_i2c.bus[bus], addr, CONFIG_SYS_I2C_PROBE_TIMEOUT_MS);
  switch (rc) {
    case ESP_OK:
      return NULL;
    case ESP_ERR_NOT_FOUND:
      return ERR_NEW(ERR_I2C_DEV_NOT_FOUND, .bus = bus, .addr = addr);
    case ESP_ERR_TIMEOUT:
      return ERR_NEW(ERR_I2C_BUS_STUCK, .bus = bus, .addr = addr);
    default:
      return ERR_WRAP(ERR_ESP(rc), ERR_I2C_XFER, .bus = bus, .addr = addr);
  }
}

/* Result of a transfer on `dev`. The driver returns ESP_ERR_INVALID_RESPONSE
 * when the transfer did not finish (NACK or SCL timeout) and ESP_ERR_TIMEOUT
 * when the bus lock was not free in time. */
static err_h xfer_result(const sys_i2c_dev_t* dev, esp_err_t rc) {
  switch (rc) {
    case ESP_OK:
      return NULL;
    case ESP_ERR_INVALID_RESPONSE:
      return ERR_NEW(ERR_I2C_NO_ACK, .bus = dev->bus, .addr = dev->addr);
    case ESP_ERR_TIMEOUT:
      return ERR_NEW(ERR_I2C_BUS_BUSY, .bus = dev->bus, .addr = dev->addr);
    default:
      return ERR_WRAP(ERR_ESP(rc), ERR_I2C_XFER, .bus = dev->bus, .addr = dev->addr);
  }
}

static err_h reg_write(sys_i2c_dev_t* dev, uint8_t reg, const uint8_t* data, size_t len) {
  // Register byte and data go out as one transfer from two buffers - no copy.
  i2c_master_transmit_multi_buffer_info_t bufs[] = {
      {.write_buffer = &reg, .buffer_size = 1},
      {.write_buffer = data, .buffer_size = len},
  };
  err_h e = xfer_result(dev, i2c_master_multi_buffer_transmit(dev->handle, bufs, ARRAY_LEN(bufs),
                                                              CONFIG_SYS_I2C_XFER_TIMEOUT_MS));
  return e != NULL ? ERR_WRAP(e, ERR_I2C_REG, .reg = reg, .write = 1) : NULL;
}

static err_h reg_read(sys_i2c_dev_t* dev, uint8_t reg, uint8_t* data, size_t len) {
  err_h e = xfer_result(dev, i2c_master_transmit_receive(dev->handle, &reg, 1, data, len,
                                                         CONFIG_SYS_I2C_XFER_TIMEOUT_MS));
  return e != NULL ? ERR_WRAP(e, ERR_I2C_REG, .reg = reg, .write = 0) : NULL;
}

/* ----------------------------------------------------------------------------
 * Buses
 * ------------------------------------------------------------------------- */

err_h sys_i2c_init(void) {
  for (uint8_t b = 0; b < SYS_I2C_BUS_COUNT; b++) {
    const bus_cfg_t* c = &k_bus_cfg[b];
    if (!c->enabled || s_i2c.bus[b] != NULL) {
      continue;
    }
    const i2c_master_bus_config_t cfg = {
        .i2c_port = c->port,
        .sda_io_num = c->sda,
        .scl_io_num = c->scl,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = CONFIG_SYS_I2C_GLITCH_IGNORE_CNT,
        .flags.enable_internal_pullup = c->pullup,
    };
    esp_err_t rc = i2c_new_master_bus(&cfg, &s_i2c.bus[b]);
    if (rc != ESP_OK) {
      s_i2c.bus[b] = NULL;
      return ERR_WRAP(ERR_ESP(rc), ERR_I2C_BUS_INIT, .bus = b, .sda = c->sda, .scl = c->scl);
    }
  }
  return NULL;
}

bool sys_i2c_bus_ready(uint8_t bus) {
  return bus < SYS_I2C_BUS_COUNT && s_i2c.bus[bus] != NULL;
}

err_h sys_i2c_bus_info(uint8_t bus, sys_i2c_bus_info_t* info) {
  ERR_CHECK(info != NULL, ERR_I2C_BAD_ARG);
  ERR_CHECK(bus < SYS_I2C_BUS_COUNT, ERR_I2C_BAD_BUS, .bus = bus);
  const bus_cfg_t* c = &k_bus_cfg[bus];
  *info = (sys_i2c_bus_info_t){
      .enabled = c->enabled,
      .ready = s_i2c.bus[bus] != NULL,
      .pullup = c->pullup,
      .sda = c->sda,
      .scl = c->scl,
  };
  U_CRITICAL_ENTER(s_addr_lock);
  memcpy(info->used, s_i2c.addr_used[bus], sizeof(info->used));
  U_CRITICAL_EXIT(s_addr_lock);
  return NULL;
}

err_h sys_i2c_probe(uint8_t bus, uint8_t addr) {
  ERR_TRY(check_bus(bus));
  ERR_CHECK(addr <= ADDR_7BIT_MAX, ERR_I2C_BAD_DEV_CFG, .bus = bus, .addr = addr);
  return probe(bus, addr);
}

err_h sys_i2c_bus_reset(uint8_t bus) {
  ERR_TRY(check_bus(bus));
  esp_err_t rc = i2c_master_bus_reset(s_i2c.bus[bus]);
  return rc == ESP_OK ? NULL : ERR_WRAP(ERR_ESP(rc), ERR_I2C_BUS_RESET, .bus = bus);
}

/* ----------------------------------------------------------------------------
 * Devices
 * ------------------------------------------------------------------------- */

err_h sys_i2c_dev_add(sys_i2c_dev_t* dev) {
  ERR_CHECK(dev != NULL, ERR_I2C_BAD_ARG);
  if (dev->handle != NULL) {
    return NULL;
  }
  ERR_TRY(check_bus(dev->bus));
  ERR_CHECK(dev->addr <= ADDR_7BIT_MAX && dev->speed_Hz > 0, ERR_I2C_BAD_DEV_CFG, .bus = dev->bus,
            .addr = dev->addr);
  // Claimed before the probe, so two concurrent adds of one address can't both pass.
  ERR_CHECK(addr_claim(dev->bus, dev->addr), ERR_I2C_ADDR_TAKEN, .bus = dev->bus, .addr = dev->addr);

  err_h err = probe(dev->bus, dev->addr);
  if (err == NULL) {
    const i2c_device_config_t cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = dev->addr,
        .scl_speed_hz = dev->speed_Hz,
    };
    esp_err_t rc = i2c_master_bus_add_device(s_i2c.bus[dev->bus], &cfg, &dev->handle);
    if (rc != ESP_OK) {
      dev->handle = NULL;
      err = ERR_WRAP(ERR_ESP(rc), ERR_I2C_DEV_ADD, .bus = dev->bus, .addr = dev->addr);
    }
  }
  if (err != NULL) {
    addr_release(dev->bus, dev->addr);
  }
  return err;
}

err_h sys_i2c_dev_attach(sys_i2c_dev_t* dev, uint8_t bus, uint8_t addr, uint32_t speed_Hz) {
  sys_i2c_dev_init(dev, bus, addr, speed_Hz);
  return sys_i2c_dev_add(dev);
}

err_h sys_i2c_dev_remove(sys_i2c_dev_t* dev) {
  ERR_CHECK(dev != NULL, ERR_I2C_BAD_ARG);
  if (dev->handle == NULL) {
    return NULL;
  }
  esp_err_t rc = i2c_master_bus_rm_device(dev->handle);
  dev->handle = NULL;
  addr_release(dev->bus, dev->addr);
  return rc == ESP_OK ? NULL : ERR_WRAP(ERR_ESP(rc), ERR_I2C_DEV_REMOVE, .bus = dev->bus, .addr = dev->addr);
}

/* ----------------------------------------------------------------------------
 * Transfers
 * ------------------------------------------------------------------------- */

err_h sys_i2c_write(sys_i2c_dev_t* dev, const uint8_t* data, size_t len) {
  ERR_TRY(check_dev(dev));
  ERR_CHECK(data != NULL && len > 0, ERR_I2C_BAD_ARG);
  return xfer_result(dev, i2c_master_transmit(dev->handle, data, len, CONFIG_SYS_I2C_XFER_TIMEOUT_MS));
}

err_h sys_i2c_read(sys_i2c_dev_t* dev, uint8_t* data, size_t len) {
  ERR_TRY(check_dev(dev));
  ERR_CHECK(data != NULL && len > 0, ERR_I2C_BAD_ARG);
  return xfer_result(dev, i2c_master_receive(dev->handle, data, len, CONFIG_SYS_I2C_XFER_TIMEOUT_MS));
}

err_h sys_i2c_write_read(sys_i2c_dev_t* dev, const uint8_t* wdata, size_t wlen, uint8_t* rdata, size_t rlen) {
  ERR_TRY(check_dev(dev));
  ERR_CHECK(wdata != NULL && wlen > 0 && rdata != NULL && rlen > 0, ERR_I2C_BAD_ARG);
  return xfer_result(dev, i2c_master_transmit_receive(dev->handle, wdata, wlen, rdata, rlen,
                                                      CONFIG_SYS_I2C_XFER_TIMEOUT_MS));
}

/* ----------------------------------------------------------------------------
 * Register helpers
 * ------------------------------------------------------------------------- */

err_h sys_i2c_reg_write(sys_i2c_dev_t* dev, uint8_t reg, const uint8_t* data, size_t len) {
  ERR_TRY(check_dev(dev));
  ERR_CHECK(data != NULL && len > 0, ERR_I2C_BAD_ARG);
  return reg_write(dev, reg, data, len);
}

err_h sys_i2c_reg_read(sys_i2c_dev_t* dev, uint8_t reg, uint8_t* data, size_t len) {
  ERR_TRY(check_dev(dev));
  ERR_CHECK(data != NULL && len > 0, ERR_I2C_BAD_ARG);
  return reg_read(dev, reg, data, len);
}

err_h sys_i2c_reg_read_u16(sys_i2c_dev_t* dev, uint8_t reg, uint16_t* val, sys_i2c_order_e order) {
  ERR_TRY(check_dev(dev));
  ERR_CHECK(val != NULL && (order == SYS_I2C_BE || order == SYS_I2C_LE), ERR_I2C_BAD_ARG);
  uint8_t b[2];
  ERR_TRY(reg_read(dev, reg, b, sizeof(b)));
  *val = order == SYS_I2C_BE ? (uint16_t)((b[0] << 8) | b[1]) : (uint16_t)((b[1] << 8) | b[0]);
  return NULL;
}

err_h sys_i2c_reg_write_u16(sys_i2c_dev_t* dev, uint8_t reg, uint16_t val, sys_i2c_order_e order) {
  ERR_TRY(check_dev(dev));
  ERR_CHECK(order == SYS_I2C_BE || order == SYS_I2C_LE, ERR_I2C_BAD_ARG);
  const uint8_t hi = (uint8_t)(val >> 8);
  const uint8_t lo = (uint8_t)val;
  const uint8_t b[2] = {order == SYS_I2C_BE ? hi : lo, order == SYS_I2C_BE ? lo : hi};
  return reg_write(dev, reg, b, sizeof(b));
}

err_h sys_i2c_reg_update(sys_i2c_dev_t* dev, uint8_t reg, uint8_t mask, uint8_t val) {
  ERR_TRY(check_dev(dev));
  uint8_t r = 0;
  ERR_TRY(reg_read(dev, reg, &r, 1));
  r = (uint8_t)((r & ~mask) | (val & mask));
  return reg_write(dev, reg, &r, 1);
}
