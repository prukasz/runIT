#pragma once

#include "driver/i2c_master.h"
#include "errors.h"
#include "sys_i2c_errors.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief I2C master on two buses, created with their pins by sys_i2c_bus_create()
 * (the board's profile, SYS_BOARD.MD). A chip is a sys_i2c_dev_t embedded in
 * its owner, attached with sys_i2c_dev_add().
 * Transfers are thread-safe: the ESP-IDF driver locks the bus per transfer.
 */

//#ref-enum @alias I2C Bus
typedef enum {
  SYS_I2C_BUS_0 = 0,  //@alias Bus 0 @description I2C Bus 0
  SYS_I2C_BUS_1 = 1,  //@alias Bus 1 @description I2C Bus 1
  SYS_I2C_BUS_COUNT,
} sys_i2c_bus_e;

/**
 * @brief Byte order of a multi-byte register value on the wire.
 */
//#ref-enum @alias I2C Byte Order
typedef enum {
  SYS_I2C_BE = 0,  //@alias Be @description Big-endian: high byte first (INA3221)
  SYS_I2C_LE = 1,  //@alias Le @description Little-endian: low byte first (TPS55289)
} sys_i2c_order_e;

/**
 * @brief One chip on a bus. Embedded by value in the owner's state; fill it
 * with sys_i2c_dev_init().
 */
typedef struct {
  i2c_master_dev_handle_t handle;  // NULL until sys_i2c_dev_add()
  uint32_t speed_Hz;
  uint8_t addr;  // 7-bit
  uint8_t bus;   // sys_i2c_bus_e
} sys_i2c_dev_t;

/* ----------------------------------------------------------------------------
 * Buses
 * ------------------------------------------------------------------------- */

/** @brief A bus and its pins: the frame of BUS_CREATE, defined in sys_i2c_router.h. */
typedef struct sys_i2c_bus_cfg sys_i2c_bus_cfg_t;

/**
 * @brief Creates a bus on its pins, both of the ESP32-S3's GPIO device, which must exist.
 * @param[in] cfg The bus and its pins
 * @return ERR_I2C_BAD_PIN for a pin of another device (the controller can't reach it), ERR_I2C_BAD_BUS: no such bus,
 * ERR_I2C_BUS_EXISTS: created already, ERR_I2C_BUS_INIT: the driver refused it
 */
MUST_USE err_h sys_i2c_bus_create(const sys_i2c_bus_cfg_t* cfg);

/**
 * @brief What `bus` was created with.
 * @param[in]  bus Bus index (sys_i2c_bus_e)
 * @param[out] cfg The bus and its pins
 * @return ERR_I2C_BAD_BUS: no such bus, or not created
 */
MUST_USE err_h sys_i2c_bus_get(uint8_t bus, sys_i2c_bus_cfg_t* cfg);

/**
 * @brief Removes `bus` and frees its pins. No scan or raw access may be running on it.
 * @param[in] bus Bus index (sys_i2c_bus_e)
 * @return ERR_I2C_BAD_BUS: no such bus, ERR_I2C_BUS_IN_USE while a device is added on it
 */
MUST_USE err_h sys_i2c_bus_remove(uint8_t bus);

/**
 * @brief Whether `bus` is created.
 * @param[in] bus Bus index (sys_i2c_bus_e)
 * @return true when created
 */
bool sys_i2c_bus_ready(uint8_t bus);

/**
 * @brief Checks whether a chip acks `addr` (probe at 100 kHz, no data).
 * @param[in] bus  Bus index (sys_i2c_bus_e)
 * @param[in] addr 7-bit address
 * @return NULL if it answered, ERR_I2C_DEV_NOT_FOUND if nothing did, ERR_I2C_BUS_STUCK if the bus timed out
 * @usage for (uint8_t a = 0x08; a < 0x78; a++) { err_h e = sys_i2c_probe(SYS_I2C_BUS_1, a); ... }
 */
MUST_USE err_h sys_i2c_probe(uint8_t bus, uint8_t addr);

/**
 * @brief Resets the bus (clocks SCL to free a chip holding SDA low). For recovery after ERR_I2C_BUS_STUCK /
 * ERR_I2C_NO_ACK.
 * @param[in] bus Bus index (sys_i2c_bus_e)
 * @return ERR_I2C_BAD_BUS: no such bus, or the driver's error
 */
MUST_USE err_h sys_i2c_bus_reset(uint8_t bus);

/* ----------------------------------------------------------------------------
 * Devices
 * ------------------------------------------------------------------------- */

/**
 * @brief Fills `dev` (no bus access). Call before sys_i2c_dev_add().
 * @param[out] dev      The chip
 * @param[in]  bus      Bus index (sys_i2c_bus_e)
 * @param[in]  addr     7-bit address
 * @param[in]  speed_Hz Bus clock for this chip
 * @usage sys_i2c_dev_init(&c->i2c, cfg->i2c_bus, cfg->i2c_addr, 400000);
 */
static inline void sys_i2c_dev_init(sys_i2c_dev_t* dev, uint8_t bus, uint8_t addr, uint32_t speed_Hz) {
  *dev = (sys_i2c_dev_t){.handle = NULL, .speed_Hz = speed_Hz, .addr = addr, .bus = bus};
}

/**
 * @brief Probes the chip and attaches it to its bus. One device per address
 * and bus. A device already added is left as it is (returns NULL).
 * @param[in,out] dev The chip, filled by sys_i2c_dev_init(); its handle is set
 * @return ERR_I2C_BAD_BUS, ERR_I2C_BAD_DEV_CFG, ERR_I2C_ADDR_TAKEN, the sys_i2c_probe errors, or ERR_I2C_DEV_ADD
 */
MUST_USE err_h sys_i2c_dev_add(sys_i2c_dev_t* dev);

/**
 * @brief sys_i2c_dev_init() then sys_i2c_dev_add(): the create path of a chip that is attached at once.
 * @param[out] dev      The chip
 * @param[in]  bus      Bus index (sys_i2c_bus_e)
 * @param[in]  addr     7-bit address
 * @param[in]  speed_Hz Bus clock for this chip
 * @return The errors of sys_i2c_dev_add()
 */
MUST_USE err_h sys_i2c_dev_attach(sys_i2c_dev_t* dev, uint8_t bus, uint8_t addr, uint32_t speed_Hz);

/**
 * @brief Whether the chip is attached (added, not removed): the owner's "was it built" check on teardown.
 * @param[in] dev The chip
 * @return true while attached
 */
static inline bool sys_i2c_dev_added(const sys_i2c_dev_t* dev) {
  return dev->handle != NULL;
}

/**
 * @brief Detaches the chip and frees its address. Safe on a device that was
 * never added; `handle` is NULL afterwards even when the driver fails.
 * @param[in,out] dev The chip
 * @return The driver's error
 */
MUST_USE err_h sys_i2c_dev_remove(sys_i2c_dev_t* dev);

/* ----------------------------------------------------------------------------
 * Transfers (`dev` added). Errors carry bus and address; the owner adds its
 * own context (device ID) on top.
 * ------------------------------------------------------------------------- */

/**
 * @brief Writes `len` bytes to the chip.
 * @param[in] dev  The chip, added
 * @param[in] data Bytes to send
 * @param[in] len  Number of bytes
 * @return ERR_I2C_* of the transfer, with the bus and address
 */
MUST_USE err_h sys_i2c_write(sys_i2c_dev_t* dev, const uint8_t* data, size_t len);

/**
 * @brief Reads `len` bytes from the chip.
 * @param[in]  dev  The chip, added
 * @param[out] data Buffer for the bytes
 * @param[in]  len  Number of bytes
 * @return ERR_I2C_* of the transfer, with the bus and address
 */
MUST_USE err_h sys_i2c_read(sys_i2c_dev_t* dev, uint8_t* data, size_t len);

/**
 * @brief Write, then read after a repeated start (no stop in between).
 * @param[in]  dev   The chip, added
 * @param[in]  wdata Bytes to send
 * @param[in]  wlen  Number of bytes to send
 * @param[out] rdata Buffer for the bytes read
 * @param[in]  rlen  Number of bytes to read
 * @return ERR_I2C_* of the transfer, with the bus and address
 */
MUST_USE err_h sys_i2c_write_read(sys_i2c_dev_t* dev, const uint8_t* wdata, size_t wlen, uint8_t* rdata, size_t rlen);

/* ----------------------------------------------------------------------------
 * Register helpers for the usual "register byte, then data" chips. Failures
 * are wrapped in ERR_I2C_REG (register, direction).
 * ------------------------------------------------------------------------- */

/**
 * @brief Writes `len` bytes starting at `reg` (register byte and data in one
 * transfer, no copy, no length limit).
 * @param[in] dev  The chip, added
 * @param[in] reg  First register
 * @param[in] data Bytes to write
 * @param[in] len  Number of bytes
 * @return ERR_I2C_REG wrapping the transfer's error
 */
MUST_USE err_h sys_i2c_reg_write(sys_i2c_dev_t* dev, uint8_t reg, const uint8_t* data, size_t len);

/**
 * @brief Reads `len` bytes starting at `reg`.
 * @param[in]  dev  The chip, added
 * @param[in]  reg  First register
 * @param[out] data Buffer for the bytes
 * @param[in]  len  Number of bytes
 * @return ERR_I2C_REG wrapping the transfer's error
 */
MUST_USE err_h sys_i2c_reg_read(sys_i2c_dev_t* dev, uint8_t reg, uint8_t* data, size_t len);

/**
 * @brief Reads a 16-bit register value in the chip's byte order (`order`).
 * @param[in]  dev   The chip, added
 * @param[in]  reg   Register
 * @param[out] val   The value
 * @param[in]  order SYS_I2C_BE or SYS_I2C_LE
 * @return ERR_I2C_REG wrapping the transfer's error
 * @usage ERR_TRY(sys_i2c_reg_read_u16(&c->i2c, INA_REG_BUS_V, &raw, SYS_I2C_BE));
 */
MUST_USE err_h sys_i2c_reg_read_u16(sys_i2c_dev_t* dev, uint8_t reg, uint16_t* val, sys_i2c_order_e order);

/**
 * @brief Writes a 16-bit register value in the chip's byte order (`order`).
 * @param[in] dev   The chip, added
 * @param[in] reg   Register
 * @param[in] val   The value
 * @param[in] order SYS_I2C_BE or SYS_I2C_LE
 * @return ERR_I2C_REG wrapping the transfer's error
 */
MUST_USE err_h sys_i2c_reg_write_u16(sys_i2c_dev_t* dev, uint8_t reg, uint16_t val, sys_i2c_order_e order);

/**
 * @brief Read-modify-write of one register: reg = (reg & ~mask) | (val & mask).
 * Not atomic against another task writing the same register.
 * @param[in] dev  The chip, added
 * @param[in] reg  Register
 * @param[in] mask Bits to change
 * @param[in] val  New value of those bits
 * @return ERR_I2C_REG wrapping the transfer's error
 */
MUST_USE err_h sys_i2c_reg_update(sys_i2c_dev_t* dev, uint8_t reg, uint8_t mask, uint8_t val);

#ifdef __cplusplus
}
#endif

