/**
 * @file
 * @brief SPI contract (sys_spi_contract): the CONTRACT_SPI_OPERATIONS table, its argument structs and the
 *        client a chip's driver uses to reach its bus (sys_spi_dev_t and transfers).
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "errors.h"
#include "sys_device.h"
#include "sys_device_driver.h" // sys_device_res_t: a chip on a bus is a resource of its owner
#include "sys_spi_contract/sys_spi_id.h"
#include "sys_spi_errors.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * The SPI contract, SPI: 0x0Cxx, where 0x0C is the contract and xx its op. Plain SPI: MOSI, MISO,
 * SCLK and a chip select per chip. A bus is a device (the MCU's SPI controller, or a simulated one
 * in tests); a chip names its bus by that ID and reaches it only through the ops below.
 * A chip on a bus is known by its chip select `cs` (the GPIO the bus drives low for it).
 * SYS_SPI_CS_NONE is a chip without a CS line from the bus: at most one per bus.
 * ATTACH - claims a chip select for the calling device with its mode, clock and queue depth. Local
 * DETACH - lets the chip select go, always: after it nobody owns it. It waits for the chip's
 *          queued transfers; those still queued after SYS_SPI_WAIT_MAX_MS (ERR_SPI_TIMEOUT) end
 *          first, the bus frees the chip after them. Local
 * XFER   - one transfer, blocking: waits for the chip's queued transfers, then runs it. Local
 * QUEUE  - one transfer, queued: returns at once, `done` is called from the transfer-done ISR (a
 *          display's FLUSH). Local
 * All are ALWAYS (a suspended bus still carries its chips' suspend hooks) and LOCAL (only a
 * driver's C reaches them). An attached chip select has an owner, the device whose op called
 * ATTACH: only it runs the other ops on it (ERR_SPI_NOT_OWNER{owner}), and ERR_SPI_CS_TAKEN names
 * it. A chip's create() declares the use (sys_spi_dev_attach), so the registry keeps the bus while
 * the chip exists and suspends the chip before its bus. The chip's driver claims its CS pin
 * (sys_io_claim_peripheral) in its create(), before the attach: the pin lives as long as the chip,
 * the bus only drives it.
 * A transfer, in one CS: tx_len bytes of tx are sent, then rx_len bytes are read into rx (0xFF
 * sent). With SYS_SPI_XFER_DUPLEX they run at once instead: rx is read while tx is sent, and
 * tx_len == rx_len.
 * The queue: up to queue_depth transfers per chip (ATTACH); a chip's transfers (XFER and QUEUE)
 * run in the order given, those of two chips may interleave a whole transfer at a time.
 * Time: XFER and DETACH wait for the queue at most SYS_SPI_WAIT_MAX_MS, half the registry's lock
 * timeout (ERR_SPI_TIMEOUT: an XFER is not run, a DETACH still lets the chip select go). A driver
 * keeps each transfer well under it. Callbacks (pre, done) run in an ISR: they set a GPIO directly
 * or give a semaphore, never call sys_device_*. No events.
 * @usage ERR_TRY(sys_spi_dev_attach(id, &c->spi, cfg->spi_bus, &args));
 *        ERR_TRY(sys_spi_write(&c->spi, init_seq, sizeof init_seq));
 */

#define SYS_SPI_CS_NONE 0xFFu /**< A chip without a CS line from the bus */
/** Longest wait of XFER and DETACH for the chip's queue: half the registry's lock timeout (500) */
#define SYS_SPI_WAIT_MAX_MS 250u

/**
 * @brief Clock polarity and phase of a chip (CPOL, CPHA)
 */
//#ref-enum @alias SPI Mode
typedef enum sys_spi_mode_e {
    SYS_SPI_MODE_0 = 0, //@alias Mode 0 @description CPOL 0, CPHA 0: idle low, sample on rising
    SYS_SPI_MODE_1 = 1, //@alias Mode 1 @description CPOL 0, CPHA 1: idle low, sample on falling
    SYS_SPI_MODE_2 = 2, //@alias Mode 2 @description CPOL 1, CPHA 0: idle high, sample on falling
    SYS_SPI_MODE_3 = 3, //@alias Mode 3 @description CPOL 1, CPHA 1: idle high, sample on rising
} sys_spi_mode_e;

/* spi_xfer_t.flags; any other bit is ERR_BAD_ARG */
#define SYS_SPI_XFER_DUPLEX (1u << 0) /**< rx read while tx is sent, tx_len == rx_len */

/**
 * @brief From a transfer's ISR: set a GPIO (a display's D/C pin), give a semaphore, nothing else
 * @param ctx The transfer's ctx
 */
typedef void (*sys_spi_cb_f)(void *ctx);

/** @brief DETACH args: a chip select */
typedef struct {
    uint8_t cs; /**< Chip select GPIO, or SYS_SPI_CS_NONE */
} __attribute__((packed)) spi_cs_args_t;

/** @brief ATTACH args */
typedef struct {
    uint8_t cs;          /**< Chip select GPIO, or SYS_SPI_CS_NONE */
    uint8_t mode;        /**< sys_spi_mode_e */
    uint8_t queue_depth; /**< Transfers QUEUE may hold at once, at least 1 */
    uint32_t clock_Hz;   /**< SCLK for this chip, at most the bus's maximum */
} __attribute__((packed)) spi_attach_args_t;

/**
 * @brief XFER and QUEUE args: one transfer, tx, rx or both. QUEUE copies this struct: the buffers
 * and ctx stay valid until `done`.
 */
typedef struct {
    uint8_t cs;        /**< An attached chip select (the client sets it) */
    uint8_t flags;     /**< SYS_SPI_XFER_* */
    const uint8_t *tx; /**< Data sent first, NULL: none */
    size_t tx_len;     /**< Bytes of tx */
    uint8_t *rx;       /**< Data read after tx (with SYS_SPI_XFER_DUPLEX: during it), NULL: none */
    size_t rx_len;     /**< Bytes of rx */
    sys_spi_cb_f pre;  /**< ISR, before the transfer starts (a D/C pin); NULL: none */
    sys_spi_cb_f done; /**< ISR, after the transfer ended; NULL: none */
    void *ctx;         /**< Passed to pre and done */
} spi_xfer_t;

/** @cond */ /* sizes, the same on every target */
_Static_assert(sizeof(spi_cs_args_t) == 1, "spi_cs_args_t: 1 byte");
_Static_assert(sizeof(spi_attach_args_t) == 7, "spi_attach_args_t: 7 bytes");
/** @endcond */

/**
 * @brief The contract, once: X(P, OP_NAME, op number, key_t, value_t, flags). Sides: all W.
 * @param X Macro applied to each op
 * @param P Contract prefix
 */
#define CONTRACT_SPI_OPERATIONS(X, P)                                                              \
    X(P, OP_ATTACH, 0x00, cmd_none_t, spi_attach_args_t,                                           \
      SYS_DEVICE_OP_ALWAYS | SYS_DEVICE_OP_LOCAL)                                                  \
    X(P, OP_DETACH, 0x01, cmd_none_t, spi_cs_args_t, SYS_DEVICE_OP_ALWAYS | SYS_DEVICE_OP_LOCAL)   \
    X(P, OP_XFER, 0x02, cmd_none_t, spi_xfer_t, SYS_DEVICE_OP_ALWAYS | SYS_DEVICE_OP_LOCAL)        \
    X(P, OP_QUEUE, 0x03, cmd_none_t, spi_xfer_t, SYS_DEVICE_OP_ALWAYS | SYS_DEVICE_OP_LOCAL)

SYS_DEVICE_CONTRACT_DECLARE(CONTRACT_SPI, MODULE_ID_SPI)

/* ----------------------------------------------------------------------------
 * The client: a chip on a bus
 * ------------------------------------------------------------------------- */

/** @brief A chip on a bus, embedded in its owner's struct. Zeroed: not attached. */
typedef struct {
    sys_device_res_t res; /**< The chip select as a resource of its owner: the registry's */
    uint8_t bus;          /**< The bus's device ID */
    uint8_t cs;           /**< Chip select GPIO, or SYS_SPI_CS_NONE */
    uint8_t attached;     /**< 1 after sys_spi_dev_attach() */
} sys_spi_dev_t;

/**
 * @brief Puts the owner's chip on bus `bus`: declares the use (the bus has the XFER op, else
 * ERR_DEV_UNKNOWN_OP), then ATTACH (chip select claimed). Called from the owner's create(). The
 * chip select is a resource of the owner: the registry lets it go (DETACH, which waits for the
 * queue) when the owner is removed or its create() fails.
 * @param[in]  owner The owner's device ID
 * @param[out] dev   The chip
 * @param[in]  bus   The bus's device ID
 * @param[in]  args  Chip select, mode, queue depth, clock
 * @return ERR_BAD_ARG for a NULL dev or args, ERR_DEV_NOT_FOUND{bus}, ERR_DEV_UNKNOWN_OP, or the
 * bus's ATTACH error (ERR_SPI_CS_TAKEN{owner}, ERR_SPI_BAD_DEV_CFG, ERR_SPI_NO_SLOT,
 * ERR_SPI_DEV_ADD)
 */
MUST_USE err_h sys_spi_dev_attach(uint8_t owner, sys_spi_dev_t *dev, uint8_t bus,
                                  const spi_attach_args_t *args);

/* Transfers. Each is one XFER (blocking) unless named queue; a sequence of them is not atomic on
 * the bus; an XFER runs after the chip's queued transfers. Errors: the bus's (ERR_SPI_TOO_LONG,
 * ERR_SPI_XFER, ERR_SPI_TIMEOUT when the queue did not empty within SYS_SPI_WAIT_MAX_MS) under
 * ERR_DEV_ID / ERR_DEV_OP of the bus, ERR_SPI_DEV_NOT_ADDED before attach, ERR_BAD_ARG for a NULL
 * buffer or a length of 0. They are made from an op of the chip's owner (its create() and
 * suspend() too): the bus refuses a transfer from anyone else (ERR_SPI_NOT_OWNER). */

/**
 * @brief Writes `len` bytes (what comes back is dropped)
 * @param[in] dev  The chip
 * @param[in] data Bytes written
 * @param[in] len  Their count, at least 1
 * @return NULL, or the transfer's error
 */
MUST_USE err_h sys_spi_write(const sys_spi_dev_t *dev, const uint8_t *data, size_t len);

/**
 * @brief Reads `len` bytes (0xFF sent)
 * @param[in]  dev  The chip
 * @param[out] data Bytes read
 * @param[in]  len  Their count, at least 1
 * @return NULL, or the transfer's error
 */
MUST_USE err_h sys_spi_read(const sys_spi_dev_t *dev, uint8_t *data, size_t len);

/**
 * @brief Writes `wlen` bytes, then reads `rlen` bytes, in one CS (a command and its answer)
 * @param[in]  dev   The chip
 * @param[in]  wdata Bytes written
 * @param[in]  wlen  Their count, at least 1
 * @param[out] rdata Bytes read
 * @param[in]  rlen  Their count, at least 1
 * @return NULL, or the transfer's error
 */
MUST_USE err_h sys_spi_write_read(const sys_spi_dev_t *dev, const uint8_t *wdata, size_t wlen,
                                  uint8_t *rdata, size_t rlen);

/**
 * @brief Full duplex (SYS_SPI_XFER_DUPLEX): sends `tx` and receives into `rx` at once, `len` bytes
 * each
 * @param[in]  dev The chip
 * @param[in]  tx  Bytes sent
 * @param[out] rx  Bytes received; may be `tx`
 * @param[in]  len Their count, at least 1
 * @return NULL, or the transfer's error
 */
MUST_USE err_h sys_spi_transfer(const sys_spi_dev_t *dev, const uint8_t *tx, uint8_t *rx,
                                size_t len);

/**
 * @brief Queues one transfer and returns at once (a display's FLUSH): `xfer->pre` runs from the ISR
 * before it starts, `xfer->done` from the ISR after it ends. The bus copies `*xfer`; its buffers
 * and ctx stay valid until done.
 * @param[in] dev  The chip
 * @param[in] xfer The transfer; `xfer->cs` is ignored, the chip's is used
 * @return NULL, ERR_BAD_ARG for a NULL xfer, neither tx nor rx, or an unknown flag,
 * ERR_SPI_QUEUE_FULL{depth}, ERR_SPI_TOO_LONG
 */
MUST_USE err_h sys_spi_queue(const sys_spi_dev_t *dev, const spi_xfer_t *xfer);

#ifdef __cplusplus
}
#endif
