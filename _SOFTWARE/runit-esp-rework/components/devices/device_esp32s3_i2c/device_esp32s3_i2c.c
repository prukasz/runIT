/*
 * ESP32-S3 I2C controller as a bus device, type 0x004C: the I2C contract on the ESP-IDF i2c_master driver. One device
 * per controller (port 0 or 1), on two pins of the ESP32-S3 itself: the controller reaches its pads through the GPIO
 * matrix, and a pin's index is its GPIO (pin 46 is GPIO 46). That the two pins are the ESP32-S3's, not an expander's,
 * is the board's configuration: the driver knows no other device's type. The whole device is this file: nothing
 * includes it, the chips reach it by its ID and the I2C contract's ops (sys_i2c.h).
 */
#include <string.h>

#include "driver/i2c_master.h"
#include "sdkconfig.h"
#include "soc/soc_caps.h"
#include "sys_device_driver.h"
#include "sys_i2c.h"
#include "sys_io.h"
#include "sys_io_claim.h"

SYS_DEVICE_TYPE_DECLARE(ESP32S3_I2C, 0x004C)

/** @brief CREATE's struct (wire format). */
typedef struct __attribute__((packed)) {
  uint8_t port;      /**< I2C controller, 0 or 1 */
  sys_io_pin_t sda;  /**< A pin of the ESP32-S3 itself: its index is the GPIO */
  sys_io_pin_t scl;  /**< A pin of the ESP32-S3 itself: its index is the GPIO */
  uint8_t pullup;    /**< 1: the ESP32-S3's internal pull-ups on */
} esp32s3_i2c_cfg_t;

typedef struct {
  uint8_t id;
  sys_io_claim_t sda;
  sys_io_claim_t scl;
  sys_device_res_t bus_res;                          // the controller as a resource of the device
  i2c_master_bus_handle_t bus;                       // NULL until create() made the bus
  i2c_master_dev_handle_t dev[SYS_I2C_ADDR_MAX + 1];  // by 7-bit address: the attached chips, NULL = free
  uint8_t owner[SYS_I2C_ADDR_MAX + 1];                // of an attached address: the device that attached it
} esp_i2c_t;

#define SYS_DEVICE_CTX esp_i2c_t

/* The driver returns ESP_ERR_INVALID_RESPONSE when a transfer did not finish (NACK or SCL timeout) and
 * ESP_ERR_TIMEOUT when the bus lock was not free in time. */
static err_h xfer_error(const esp_i2c_t* c, uint8_t addr, esp_err_t rc) {
  switch (rc) {
    case ESP_OK:
      return NULL;
    case ESP_ERR_INVALID_RESPONSE:
      return ERR_NEW(ERR_I2C_NO_ACK, .bus = c->id, .addr = addr);
    case ESP_ERR_TIMEOUT:
      return ERR_NEW(ERR_I2C_BUS_BUSY, .bus = c->id, .addr = addr);
    default:
      return ERR_WRAP(ERR_ESP(rc), ERR_I2C_XFER, .bus = c->id, .addr = addr);
  }
}

// ERR_I2C_DEV_NOT_FOUND when nothing acks, ERR_I2C_BUS_STUCK when the bus timed out.
static err_h probe(const esp_i2c_t* c, uint8_t addr) {
  esp_err_t rc = i2c_master_probe(c->bus, addr, CONFIG_ESP32S3_I2C_PROBE_TIMEOUT_MS);
  switch (rc) {
    case ESP_OK:
      return NULL;
    case ESP_ERR_NOT_FOUND:
      return ERR_NEW(ERR_I2C_DEV_NOT_FOUND, .bus = c->id, .addr = addr);
    case ESP_ERR_TIMEOUT:
      return ERR_NEW(ERR_I2C_BUS_STUCK, .bus = c->id, .addr = addr);
    default:
      return ERR_WRAP(ERR_ESP(rc), ERR_I2C_XFER, .bus = c->id, .addr = addr);
  }
}

static err_h detach(esp_i2c_t* c, uint8_t addr) {
  if (c->dev[addr] == NULL) {
    return NULL;
  }
  esp_err_t rc = i2c_master_bus_rm_device(c->dev[addr]);
  c->dev[addr] = NULL;  // free even when the driver failed
  return rc == ESP_OK ? NULL : ERR_WRAP(ERR_ESP(rc), ERR_I2C_DEV_REMOVE, .bus = c->id, .addr = addr);
}

/* Claimed for the caller of the op and probed, then added to the driver with the chip's own clock. The owner is the
 * device whose op called (a chip's create(); SYS_DEVICE_ID_NONE for RAW from a command). */
static err_h attach(esp_i2c_t* c, uint8_t addr, uint32_t speed_Hz) {
  ERR_CHECK(addr <= SYS_I2C_ADDR_MAX && speed_Hz > 0, ERR_I2C_BAD_DEV_CFG, .bus = c->id, .addr = addr);
  ERR_CHECK(c->dev[addr] == NULL, ERR_I2C_ADDR_TAKEN, .bus = c->id, .addr = addr, .owner = c->owner[addr]);
  ERR_TRY(probe(c, addr));
  const i2c_device_config_t cfg = {
      .dev_addr_length = I2C_ADDR_BIT_LEN_7,
      .device_address = addr,
      .scl_speed_hz = speed_Hz,
  };
  esp_err_t rc = i2c_master_bus_add_device(c->bus, &cfg, &c->dev[addr]);
  if (rc != ESP_OK) {
    c->dev[addr] = NULL;
    return ERR_WRAP(ERR_ESP(rc), ERR_I2C_DEV_ADD, .bus = c->id, .addr = addr);
  }
  c->owner[addr] = sys_device_caller();
  return NULL;
}

// DETACH and XFER are the owner's: the device that attached the address. An address not attached has no owner.
static err_h check_owner(const esp_i2c_t* c, uint8_t addr) {
  ERR_CHECK(addr <= SYS_I2C_ADDR_MAX, ERR_I2C_BAD_DEV_CFG, .bus = c->id, .addr = addr);
  ERR_CHECK(c->dev[addr] == NULL || c->owner[addr] == sys_device_caller(), ERR_I2C_NOT_OWNER, .bus = c->id,
            .addr = addr, .owner = c->owner[addr]);
  return NULL;
}

// One transfer to an attached address, by the buffers that are set (i2c_xfer_t).
static err_h xfer(const esp_i2c_t* c, const i2c_xfer_t* x) {
  ERR_CHECK(x->addr <= SYS_I2C_ADDR_MAX && c->dev[x->addr] != NULL, ERR_I2C_DEV_NOT_ADDED, .bus = c->id,
            .addr = x->addr);
  i2c_master_dev_handle_t h = c->dev[x->addr];
  const int ms = CONFIG_ESP32S3_I2C_XFER_TIMEOUT_MS;
  esp_err_t rc;
  if (x->n1 > 0 && x->n2 > 0 && x->rn == 0) {
    // Two buffers go out as one transfer - no copy.
    i2c_master_transmit_multi_buffer_info_t bufs[] = {
        {.write_buffer = x->w1, .buffer_size = x->n1},
        {.write_buffer = x->w2, .buffer_size = x->n2},
    };
    rc = i2c_master_multi_buffer_transmit(h, bufs, 2, ms);
  } else if (x->n1 > 0 && x->n2 == 0 && x->rn > 0) {
    rc = i2c_master_transmit_receive(h, x->w1, x->n1, x->r, x->rn, ms);
  } else if (x->n1 > 0 && x->n2 == 0 && x->rn == 0) {
    rc = i2c_master_transmit(h, x->w1, x->n1, ms);
  } else if (x->n1 == 0 && x->n2 == 0 && x->rn > 0) {
    rc = i2c_master_receive(h, x->r, x->rn, ms);
  } else {
    return ERR_NEW(ERR_I2C_BAD_ARG);
  }
  return xfer_error(c, x->addr, rc);
}

/* RAW: the address attached for one transfer, then let go; the transfer's error wins over the detach's. Attach fails
 * on an address a chip has (ERR_I2C_ADDR_TAKEN): RAW never reaches a chip a driver owns. */
static err_h raw(esp_i2c_t* c, const i2c_xfer_t* x) {
  ERR_TRY(attach(c, x->addr, SYS_I2C_RAW_SPEED_HZ));
  err_h err = xfer(c, x);
  err_h rm = detach(c, x->addr);
  if (err == NULL) {
    return rm;
  }
  err_free(rm);
  return err;
}

/* ---- I2C contract ---- */

OP_WRITE(I2C, ATTACH) {
  return attach(c, args->addr, args->speed_Hz);
}

OP_WRITE(I2C, DETACH) {
  ERR_TRY(check_owner(c, args->addr));
  return detach(c, args->addr);
}

OP_WRITE(I2C, XFER) {
  ERR_TRY(check_owner(c, args->addr));
  return xfer(c, args);
}

// The answer is the write's layout (the mirror): the head, then the bytes read.
OP_READ(I2C, RAW) {
  const i2c_raw_t head = {.addr = args->addr, .reg = args->reg};
  const uint32_t need = sizeof(head) + args->len;
  ERR_CHECK(args->len > 0, ERR_I2C_BAD_ARG);
  ERR_CHECK(need <= out->cap, ERR_CMD_OUT_FULL, .need = (uint16_t)need, .cap = (uint16_t)out->cap);
  const i2c_xfer_t x = {.addr = args->addr, .w1 = &args->reg, .n1 = 1, .r = out->data + sizeof(head), .rn = args->len};
  ERR_TRY(raw(c, &x));
  memcpy(out->data, &head, sizeof(head));
  out->len = need;
  return NULL;
}

OP_WRITE_TAIL(I2C, RAW) {
  ERR_CHECK(tail_len > 0, ERR_CMD_BAD_LEN, .got = (uint16_t)sizeof(*args), .need = (uint16_t)(sizeof(*args) + 1));
  const i2c_xfer_t x = {.addr = args->addr, .w1 = &args->reg, .n1 = 1, .w2 = tail, .n2 = tail_len};
  return raw(c, &x);
}

OP_READ(I2C, PROBE) {
  ERR_CHECK(args->addr <= SYS_I2C_ADDR_MAX, ERR_I2C_BAD_DEV_CFG, .bus = c->id, .addr = args->addr);
  err_h err = probe(c, args->addr);
  const bool absent = err_drop(&err, ERR_I2C_DEV_NOT_FOUND);  // an answer, not a failure
  ERR_TRY(err);
  return CMD_REPLY(I2C, PROBE, !absent);
}

/* NOLOCK (the contract's table): runs without the device's lock, so the chips' transfers go in between the probes;
 * each probe takes the driver's bus lock. It reads only c->bus, which lives until REMOVE (REMOVE waits for a SCAN in
 * flight), and changes nothing an op under the lock reads. At most 112 probe timeouts long. */
OP_READ(I2C, SCAN) {
  i2c_scan_t res = {{0}};
  for (uint8_t addr = SYS_I2C_SCAN_ADDR_FIRST; addr <= SYS_I2C_SCAN_ADDR_LAST; addr++) {
    err_h err = probe(c, addr);
    if (err_drop(&err, ERR_I2C_DEV_NOT_FOUND)) {
      continue;
    }
    ERR_TRY(err);  // a stuck bus or a driver failure: the rest of the scan would fail too
    res.found[addr / 32] |= 1u << (addr % 32);
  }
  return cmd_reply(out, &res, sizeof(res));
}

// Recovery of a stuck bus: SCL clocked until a chip lets SDA go.
OP_WRITE(I2C, RECOVER) {
  esp_err_t rc = i2c_master_bus_reset(c->bus);
  return rc == ESP_OK ? NULL : ERR_WRAP(ERR_ESP(rc), ERR_I2C_BUS_RESET, .bus = c->id);
}

/* ---- Lifecycle ---- */

// The device's resource: the controller, deleted when it was made. Added after the pins, so it goes before them.
static err_h bus_release(void* arg) {
  esp_i2c_t* c = arg;
  if (c->bus == NULL) {
    return NULL;
  }
  err_h err = NULL;
  for (uint32_t addr = 0; addr <= SYS_I2C_ADDR_MAX; addr++) {
    ERR_KEEP_FIRST(err, detach(c, (uint8_t)addr));  // none left normally: its chips were removed first
  }
  ERR_KEEP_FIRST(err, ERR_ESP(i2c_del_master_bus(c->bus)));
  c->bus = NULL;
  return err;
}

/* A bus has no outputs to make safe: SUSPEND and RESUME do nothing, and its transfers stay open (ALWAYS) for the
 * suspend hooks of its chips. */
static err_h suspend(esp_i2c_t* c) {
  (void)c;
  return NULL;
}

static err_h resume(esp_i2c_t* c) {
  (void)c;
  return NULL;
}

/* ---- Create ---- */

static err_h create(uint8_t id, esp_i2c_t* c, const esp32s3_i2c_cfg_t* cfg) {
  c->id = id;
  SYS_DEV_CHECK_IN_RANGE(id, cfg->port, 0, SOC_I2C_NUM - 1);
  // Locked on the GPIO device: its IO writes are refused while the bus has them.
  ERR_TRY(sys_io_claim_peripheral(id, &c->sda, cfg->sda));
  ERR_TRY(sys_io_claim_peripheral(id, &c->scl, cfg->scl));
  ERR_TRY(sys_device_res_add(id, &c->bus_res, bus_release, c));
  const i2c_master_bus_config_t bus_cfg = {
      .i2c_port = (i2c_port_num_t)cfg->port,
      .sda_io_num = cfg->sda.pin,
      .scl_io_num = cfg->scl.pin,
      .clk_source = I2C_CLK_SRC_DEFAULT,
      .glitch_ignore_cnt = CONFIG_ESP32S3_I2C_GLITCH_IGNORE_CNT,
      .flags.enable_internal_pullup = cfg->pullup != 0,
  };
  esp_err_t rc = i2c_new_master_bus(&bus_cfg, &c->bus);
  if (rc != ESP_OK) {
    c->bus = NULL;
    return ERR_WRAP(ERR_ESP(rc), ERR_I2C_BUS_INIT, .bus = id, .sda = cfg->sda.pin, .scl = cfg->scl.pin);
  }
  return NULL;
}

/* ---- Type ---- */

static const sys_device_op_t k_ops[] = {
    OP_ENTRY_W(I2C, ATTACH),  OP_ENTRY_W(I2C, DETACH), OP_ENTRY_W(I2C, XFER),
    OP_ENTRY_R(I2C, PROBE),   OP_ENTRY_R(I2C, SCAN),   OP_ENTRY_W(I2C, RECOVER),
    OP_ENTRY(I2C, RAW),
};

SYS_DEVICE_TYPE_DEFINE(ESP32S3_I2C, esp32s3_i2c_cfg_t, create, suspend, resume, k_ops)
