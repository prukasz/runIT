#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "errors.h"
#include "sys_device.h"
#include "sys_device_driver.h"  // sys_device_res_t: a chip on a bus is a resource of its owner
#include "sys_i2c_errors.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * The I2C contract, I2C: 0x02xx, where 0x02 is the contract and xx its op. A bus is a device: the ESP32-S3's own
 * controller (device_esp32s3_i2c), and so a mux channel, a bit-banged bus, a simulated one. As many as the project
 * creates, each with its device ID. A chip names its bus by that ID (a u8 of its create struct) and reaches it only
 * through the ops below, never through its implementation.
 * ATTACH  - claims a 7-bit address on the bus for the calling device and probes it; the bus keeps what it needs to
 *           reach the chip. Local
 * DETACH  - lets the address go. Local
 * XFER    - one transfer to an attached address (its args are pointers). Local
 * PROBE   - whether a chip acks an address (1) or not (0)
 * SCAN    - every address 0x08..0x77 that acks. NOLOCK: 112 probes would hold the device's lock longer than its
 *           timeout; without it each probe takes only the bus itself, and the chips' transfers go in between
 * RECOVER - clocks a stuck bus free
 * RAW     - a register of an address no chip is attached at, read or written: the app's diagnostics. The bus attaches
 *           the address for the one transfer (so it fails on a chip a driver owns, ERR_I2C_ADDR_TAKEN) and lets it go
 * ATTACH, DETACH and XFER are ALWAYS: a bus has no outputs to make safe, so a suspended bus keeps carrying the
 * transfers of its chips' own suspend hooks. They are LOCAL: only a driver's C reaches them (the client below), never
 * the app or a VM program, whose diagnostics are PROBE, SCAN and RAW. An attached address has an owner, the device
 * whose op called ATTACH (sys_device_caller()): only it runs XFER and DETACH on the address (ERR_I2C_NOT_OWNER{owner}
 * for anyone else), and ERR_I2C_ADDR_TAKEN names it. A chip's create() declares the use (sys_i2c_dev_attach), so the
 * registry refuses to remove a bus while a chip on it exists and suspends the chips before their bus.
 * No events.
 * The client below wraps the ops for a chip's driver: attach, transfers, register helpers.
 * @usage ERR_TRY(sys_i2c_dev_attach(id, &c->i2c, cfg->i2c_bus, cfg->i2c_addr, 400000));
 *        ERR_TRY(sys_i2c_reg_update(&c->i2c, REG_MODE1, MODE1_AI, MODE1_AI));
 */

#define SYS_I2C_ADDR_MAX        0x7Fu  // 7-bit addresses
#define SYS_I2C_SCAN_ADDR_FIRST 0x08u  // 0x00-0x07 and 0x78-0x7F are reserved addresses
#define SYS_I2C_SCAN_ADDR_LAST  0x77u

/**
 * @brief Byte order of a multi-byte register value on the wire.
 */
//#ref-enum @alias I2C Byte Order
typedef enum {
  SYS_I2C_BE = 0,  //@alias Be @description Big-endian: high byte first (INA3221)
  SYS_I2C_LE = 1,  //@alias Le @description Little-endian: low byte first (TPS55289)
} sys_i2c_order_e;

/** @brief DETACH args, the PROBE key: an address */
typedef struct __attribute__((packed)) {
  uint8_t addr;  /**< 7-bit address */
} i2c_addr_args_t;

/** @brief ATTACH args */
typedef struct __attribute__((packed)) {
  uint8_t addr;       /**< 7-bit address */
  uint32_t speed_Hz;  /**< SCL clock for this chip */
} i2c_attach_args_t;

/**
 * @brief XFER args: one transfer by the buffers that are set. `w1` (write), `w1` + `w2` (one write from two buffers, a
 * register byte and its data), `r` (read), `w1` + `r` (write, repeated start, read). Any other mix is ERR_I2C_BAD_ARG.
 */
typedef struct {
  uint8_t addr;       /**< An attached 7-bit address */
  const uint8_t* w1;  /**< First write buffer */
  size_t n1;
  const uint8_t* w2;  /**< Second write buffer, sent right after w1 in the same transfer */
  size_t n2;
  uint8_t* r;         /**< Read buffer */
  size_t rn;
} i2c_xfer_t;

/** @brief SCAN answer */
typedef struct __attribute__((packed)) {
  uint32_t found[4];  /**< Bit n = address n acked, 0x08..0x77 */
} i2c_scan_t;

/** @brief RAW read key: what to read */
typedef struct __attribute__((packed)) {
  uint8_t addr;  /**< 7-bit address, no chip attached at it */
  uint8_t reg;   /**< Register address */
  uint8_t len;   /**< Bytes to read, at least 1 */
} i2c_raw_key_t;

/** @brief RAW write args and the head of the read answer (the mirror): the data follows, at least 1 byte */
typedef struct __attribute__((packed)) {
  uint8_t addr;  /**< 7-bit address, no chip attached at it */
  uint8_t reg;   /**< Register address */
} i2c_raw_t;

#define SYS_I2C_RAW_SPEED_HZ 100000u  // the clock of RAW's one transfer

/**
 * @brief The contract, once: X(P, OP_NAME, op number, key_t, value_t, flags). Sides: ATTACH W, DETACH W, XFER W,
 * PROBE R, SCAN R, RECOVER W, RAW R and W. RAW's value is its head: the data follows it (TAIL), so a bus writes its
 * write as OP_DECLARE_WRITE_TAIL(CONTRACT_I2C, OP_RAW), with at least 1 byte of tail, and its read answers the head and the data, not
 * CMD_REPLY.
 */
#define CONTRACT_I2C_OPERATIONS(X, P)                                                                                   \
  X(P, OP_ATTACH,  0x00, cmd_none_t,      i2c_attach_args_t, SYS_DEVICE_OP_ALWAYS | SYS_DEVICE_OP_LOCAL)       \
  X(P, OP_DETACH,  0x01, cmd_none_t,      i2c_addr_args_t,   SYS_DEVICE_OP_ALWAYS | SYS_DEVICE_OP_LOCAL)       \
  X(P, OP_XFER,    0x02, cmd_none_t,      i2c_xfer_t,        SYS_DEVICE_OP_ALWAYS | SYS_DEVICE_OP_LOCAL)       \
  X(P, OP_PROBE,   0x03, i2c_addr_args_t, uint8_t,           SYS_DEVICE_OP_DIRECT)                             \
  X(P, OP_SCAN,    0x04, cmd_none_t,      i2c_scan_t,        SYS_DEVICE_OP_NOLOCK | SYS_DEVICE_OP_DIRECT)      \
  X(P, OP_RECOVER, 0x05, cmd_none_t,      cmd_none_t,        0)                                                \
  X(P, OP_RAW,     0x06, i2c_raw_key_t,   i2c_raw_t,         SYS_DEVICE_OP_TAIL | SYS_DEVICE_OP_DIRECT)

SYS_DEVICE_CONTRACT_DECLARE(CONTRACT_I2C, MODULE_ID_I2C)

/* ----------------------------------------------------------------------------
 * The client: a chip on a bus
 * ------------------------------------------------------------------------- */

/** @brief A chip on a bus, embedded in its owner's struct. Zeroed: not attached. */
typedef struct {
  sys_device_res_t res;  /**< The address as a resource of its owner: the registry's */
  uint8_t bus;           /**< The bus's device ID */
  uint8_t addr;          /**< 7-bit address */
  uint8_t attached;      /**< 1 after sys_i2c_dev_attach() */
} sys_i2c_dev_t;

/**
 * @brief Puts the owner's chip on bus `bus`: declares the use (the bus has the XFER op, else ERR_DEV_UNKNOWN_OP), then
 * ATTACH (address claimed, probed). Called from the owner's create(). The address is a resource of the owner: the
 * registry lets it go (DETACH) when the owner is removed or its create() fails.
 * @param[in]  owner    The owner's device ID
 * @param[out] dev      The chip
 * @param[in]  bus      The bus's device ID
 * @param[in]  addr     7-bit address
 * @param[in]  speed_Hz SCL clock for this chip
 * @return ERR_DEV_NOT_FOUND{bus}, ERR_DEV_UNKNOWN_OP, or the bus's ATTACH error (ERR_I2C_ADDR_TAKEN{owner},
 * ERR_I2C_DEV_NOT_FOUND, ERR_I2C_BUS_STUCK...)
 */
MUST_USE err_h sys_i2c_dev_attach(uint8_t owner, sys_i2c_dev_t* dev, uint8_t bus, uint8_t addr, uint32_t speed_Hz);

/* Transfers. Each is one XFER; a sequence of them (sys_i2c_reg_update) is not atomic on the bus. Errors: the bus's
 * (ERR_I2C_NO_ACK, ERR_I2C_BUS_BUSY, ERR_I2C_XFER) under ERR_DEV_ID / ERR_DEV_OP of the bus, ERR_I2C_DEV_NOT_ADDED
 * before attach, ERR_I2C_BAD_ARG for a NULL buffer or a length of 0. They are made from an op of the chip's owner (its
 * create() and suspend() too): the bus refuses a transfer from anyone else (ERR_I2C_NOT_OWNER). The register helpers
 * wrap them in ERR_I2C_REG. */

MUST_USE err_h sys_i2c_write(const sys_i2c_dev_t* dev, const uint8_t* data, size_t len);
MUST_USE err_h sys_i2c_read(const sys_i2c_dev_t* dev, uint8_t* data, size_t len);
/** @brief Write, repeated start, read */
MUST_USE err_h sys_i2c_write_read(const sys_i2c_dev_t* dev, const uint8_t* wdata, size_t wlen, uint8_t* rdata,
                                  size_t rlen);

/** @brief The register byte and `len` bytes of data, as one transfer from two buffers (no copy) */
MUST_USE err_h sys_i2c_reg_write(const sys_i2c_dev_t* dev, uint8_t reg, const uint8_t* data, size_t len);
/** @brief The register byte, repeated start, `len` bytes read */
MUST_USE err_h sys_i2c_reg_read(const sys_i2c_dev_t* dev, uint8_t reg, uint8_t* data, size_t len);
MUST_USE err_h sys_i2c_reg_read_u16(const sys_i2c_dev_t* dev, uint8_t reg, uint16_t* val, sys_i2c_order_e order);
MUST_USE err_h sys_i2c_reg_write_u16(const sys_i2c_dev_t* dev, uint8_t reg, uint16_t val, sys_i2c_order_e order);
/** @brief Read, (r & ~mask) | (val & mask), write: two transfers */
MUST_USE err_h sys_i2c_reg_update(const sys_i2c_dev_t* dev, uint8_t reg, uint8_t mask, uint8_t val);

#ifdef __cplusplus
}
#endif
