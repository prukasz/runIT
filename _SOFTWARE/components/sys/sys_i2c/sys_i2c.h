#pragma once

#include "driver/i2c_master.h"
#include "errors.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief I2C master on the two board buses. Bus pins and options come from
 * Kconfig (menu "sys_i2c"); sys_i2c_init() creates the enabled buses. A chip
 * is a sys_i2c_dev_t embedded in its owner, attached with sys_i2c_dev_add().
 * Transfers are thread-safe: the ESP-IDF driver locks the bus per transfer.
 */

//#ref-enum @alias I2C Bus
typedef enum {
  SYS_I2C_BUS_0 = 0,  //@alias Bus 0 @description The first I2C bus
  SYS_I2C_BUS_1 = 1,  //@alias Bus 1 @description The second I2C bus
  SYS_I2C_BUS_COUNT,
} sys_i2c_bus_e;

/**
 * @brief Byte order of a multi-byte register value on the wire.
 */
//#ref-enum @alias I2C Byte Order
typedef enum {
  SYS_I2C_BE = 0,  //@alias Be @description high byte first (INA3221, DAC53202, ADS7128)
  SYS_I2C_LE = 1,  //@alias Le @description low byte first (AP33772S, TPS55289)
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
 * Errors (module ERR_MOD_SYS_I2C)
 *
 * Transfer failures are split by cause: ERR_I2C_NO_ACK (the chip did not
 * answer or the transfer was aborted), ERR_I2C_BUS_BUSY (the bus lock was not
 * free in time), else ERR_I2C_XFER over ERR_ESP_ERR. Every transfer error
 * carries bus and address; the register helpers wrap it in ERR_I2C_REG.
 * ------------------------------------------------------------------------- */

// clang-format off
#define ERR_SYS_I2C_MAP(X)                                                                                               \
  X(ERR_I2C_BAD_ARG,        ERR_TAG(ERR_MOD_SYS_I2C, 1),  ERR_SEV_HIGH,   {}) /* NULL pointer or zero length */       \
  X(ERR_I2C_BAD_BUS,        ERR_TAG(ERR_MOD_SYS_I2C, 2),  ERR_SEV_HIGH,   { uint8_t bus; }) /* no such bus, or disabled / not initialized */ \
  X(ERR_I2C_BAD_DEV_CFG,    ERR_TAG(ERR_MOD_SYS_I2C, 3),  ERR_SEV_HIGH,   { uint8_t bus; uint8_t addr; }) /* not a 7-bit address, or speed 0 */ \
  X(ERR_I2C_BUS_INIT,       ERR_TAG(ERR_MOD_SYS_I2C, 4),  ERR_SEV_HIGH,   { uint8_t bus; uint8_t sda; uint8_t scl; }) /* over ERR_ESP_ERR */ \
  X(ERR_I2C_BUS_STUCK,      ERR_TAG(ERR_MOD_SYS_I2C, 5),  ERR_SEV_HIGH,   { uint8_t bus; uint8_t addr; }) /* probe timed out: bus busy, SCL held low or no pull-ups */ \
  X(ERR_I2C_BUS_RESET,      ERR_TAG(ERR_MOD_SYS_I2C, 6),  ERR_SEV_HIGH,   { uint8_t bus; }) /* over ERR_ESP_ERR */    \
  X(ERR_I2C_DEV_NOT_FOUND,  ERR_TAG(ERR_MOD_SYS_I2C, 7),  ERR_SEV_HIGH,   { uint8_t bus; uint8_t addr; }) /* nothing acked the address */ \
  X(ERR_I2C_DEV_ADD,        ERR_TAG(ERR_MOD_SYS_I2C, 8),  ERR_SEV_HIGH,   { uint8_t bus; uint8_t addr; }) /* over ERR_ESP_ERR */ \
  X(ERR_I2C_DEV_REMOVE,     ERR_TAG(ERR_MOD_SYS_I2C, 9),  ERR_SEV_MEDIUM, { uint8_t bus; uint8_t addr; }) /* over ERR_ESP_ERR */ \
  X(ERR_I2C_DEV_NOT_ADDED,  ERR_TAG(ERR_MOD_SYS_I2C, 10), ERR_SEV_HIGH,   { uint8_t bus; uint8_t addr; }) /* transfer before sys_i2c_dev_add */ \
  X(ERR_I2C_NO_ACK,         ERR_TAG(ERR_MOD_SYS_I2C, 11), ERR_SEV_HIGH,   { uint8_t bus; uint8_t addr; }) /* NACK / aborted transfer */ \
  X(ERR_I2C_BUS_BUSY,       ERR_TAG(ERR_MOD_SYS_I2C, 12), ERR_SEV_MEDIUM, { uint8_t bus; uint8_t addr; }) /* bus lock timed out */ \
  X(ERR_I2C_XFER,           ERR_TAG(ERR_MOD_SYS_I2C, 13), ERR_SEV_HIGH,   { uint8_t bus; uint8_t addr; }) /* other failure, over ERR_ESP_ERR */ \
  X(ERR_I2C_REG,            ERR_TAG(ERR_MOD_SYS_I2C, 14), ERR_SEV_NONE,   { uint8_t reg; uint8_t write; }) /* context only: register access */ \
  X(ERR_I2C_ADDR_TAKEN,     ERR_TAG(ERR_MOD_SYS_I2C, 15), ERR_SEV_HIGH,   { uint8_t bus; uint8_t addr; }) /* another device is added at this address */ \
  X(ERR_I2C_REMOTE_DENIED,  ERR_TAG(ERR_MOD_SYS_I2C, 16), ERR_SEV_MEDIUM, { uint8_t bus; }) /* remote op on a bus other than CONFIG_SYS_I2C_REMOTE_BUS */
// clang-format on

ERR_TAGS_DECLARE(ERR_SYS_I2C_MAP)

/* ----------------------------------------------------------------------------
 * Buses
 * ------------------------------------------------------------------------- */

/**
 * @brief Creates every bus enabled in Kconfig. A bus already created is
 * skipped, so it can be called again after a failure.
 */
MUST_USE err_h sys_i2c_init(void);

/**
 * @brief Whether `bus` exists and was created.
 */
bool sys_i2c_bus_ready(uint8_t bus);

/**
 * @brief Configuration and state of one bus.
 */
typedef struct {
  bool enabled;      // in Kconfig
  bool ready;        // created by sys_i2c_init
  bool pullup;       // internal pull-ups on
  uint8_t sda;       // GPIO
  uint8_t scl;       // GPIO
  uint32_t used[4];  // bit n = a device is added at address n
} sys_i2c_bus_info_t;

/**
 * @brief Fills `info` for `bus` (snapshot). ERR_I2C_BAD_BUS only for a bus
 * index out of range; a disabled bus is reported, not an error.
 */
MUST_USE err_h sys_i2c_bus_info(uint8_t bus, sys_i2c_bus_info_t* info);

/**
 * @brief Checks whether a chip acks `addr` (probe at 100 kHz, no data).
 * @return NULL if it answered, ERR_I2C_DEV_NOT_FOUND if nothing did,
 *         ERR_I2C_BUS_STUCK if the bus timed out.
 * @usage for (uint8_t a = 0x08; a < 0x78; a++) { err_h e = sys_i2c_probe(SYS_I2C_BUS_1, a); ... }
 */
MUST_USE err_h sys_i2c_probe(uint8_t bus, uint8_t addr);

/**
 * @brief Resets the bus (clocks SCL to free a chip holding SDA low). For
 * recovery after ERR_I2C_BUS_STUCK / ERR_I2C_NO_ACK.
 */
MUST_USE err_h sys_i2c_bus_reset(uint8_t bus);

/* ----------------------------------------------------------------------------
 * Devices
 * ------------------------------------------------------------------------- */

/**
 * @brief Fills `dev` (no bus access). Call before sys_i2c_dev_add().
 * @usage sys_i2c_dev_init(&c->i2c, cfg->i2c_bus, cfg->i2c_addr, 400000);
 */
static inline void sys_i2c_dev_init(sys_i2c_dev_t* dev, uint8_t bus, uint8_t addr, uint32_t speed_Hz) {
  *dev = (sys_i2c_dev_t){.handle = NULL, .speed_Hz = speed_Hz, .addr = addr, .bus = bus};
}

/**
 * @brief Probes the chip and attaches it to its bus. One device per address
 * and bus. A device already added is left as it is (returns NULL).
 * @return ERR_I2C_BAD_BUS, ERR_I2C_BAD_DEV_CFG, ERR_I2C_ADDR_TAKEN, the
 *         sys_i2c_probe errors, or ERR_I2C_DEV_ADD.
 */
MUST_USE err_h sys_i2c_dev_add(sys_i2c_dev_t* dev);

/**
 * @brief Detaches the chip and frees its address. Safe on a device that was
 * never added; `handle` is NULL afterwards even when the driver fails.
 */
MUST_USE err_h sys_i2c_dev_remove(sys_i2c_dev_t* dev);

/* ----------------------------------------------------------------------------
 * Transfers (`dev` added). Errors carry bus and address; the owner adds its
 * own context (device ID) on top.
 * ------------------------------------------------------------------------- */

MUST_USE err_h sys_i2c_write(sys_i2c_dev_t* dev, const uint8_t* data, size_t len);
MUST_USE err_h sys_i2c_read(sys_i2c_dev_t* dev, uint8_t* data, size_t len);

/**
 * @brief Write, then read after a repeated start (no stop in between).
 */
MUST_USE err_h sys_i2c_write_read(sys_i2c_dev_t* dev, const uint8_t* wdata, size_t wlen, uint8_t* rdata, size_t rlen);

/* ----------------------------------------------------------------------------
 * Register helpers for the usual "register byte, then data" chips. Failures
 * are wrapped in ERR_I2C_REG (register, direction).
 * ------------------------------------------------------------------------- */

/**
 * @brief Writes `len` bytes starting at `reg` (register byte and data in one
 * transfer, no copy, no length limit).
 */
MUST_USE err_h sys_i2c_reg_write(sys_i2c_dev_t* dev, uint8_t reg, const uint8_t* data, size_t len);

/**
 * @brief Reads `len` bytes starting at `reg`.
 */
MUST_USE err_h sys_i2c_reg_read(sys_i2c_dev_t* dev, uint8_t reg, uint8_t* data, size_t len);

/**
 * @brief 16-bit register value in the chip's byte order (`order`).
 * @usage ERR_TRY(sys_i2c_reg_read_u16(&c->i2c, INA_REG_BUS_V, &raw, SYS_I2C_BE));
 */
MUST_USE err_h sys_i2c_reg_read_u16(sys_i2c_dev_t* dev, uint8_t reg, uint16_t* val, sys_i2c_order_e order);
MUST_USE err_h sys_i2c_reg_write_u16(sys_i2c_dev_t* dev, uint8_t reg, uint16_t val, sys_i2c_order_e order);

/**
 * @brief Read-modify-write of one register: reg = (reg & ~mask) | (val & mask).
 * Not atomic against another task writing the same register.
 */
MUST_USE err_h sys_i2c_reg_update(sys_i2c_dev_t* dev, uint8_t reg, uint8_t mask, uint8_t val);

#ifdef __cplusplus
}
#endif
