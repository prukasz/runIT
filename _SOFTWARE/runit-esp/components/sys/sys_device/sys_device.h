#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "cmd.h"
#include "cmd_table.h"
#include "errors.h"
#include "sys_device_errors.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Device registry, the part everyone uses: the IDs, the ops and contracts, the calls. A driver includes
 * sys_device_driver.h as well, the app's wiring sys_device_hooks.h.
 *
 * Op = [contract u8][op u8]: 0x20xx the device (lifecycle, below), 0x01xx-0x1Fxx a contract (each contract gives its
 * own ID in its header, see below), 0xFFxx the device's own (SYS_DEVICE_OWN_OP). An op has a read side, a write
 * side or both (cmd_dir_e): the read takes the key (a pin, a channel; nothing) and answers what the write takes
 * (ROUTER.MD).
 * A contract is ONE table P_OPERATIONS of X(P, OP_NAME, n, key_t, value_t, flags); SYS_DEVICE_CONTRACT_DECLARE(P,
 * MODULE_ID_<NAME>) gives P_<OP_NAME>, P_FLAGS_<OP_NAME>, P_key_<OP_NAME>_t, P_value_<OP_NAME>_t. A
 * device's own ops are a table of the same format, in its driver: SYS_DEVICE_OWN_DECLARE(P) (sys_device_driver.h).
 */

//#ref-defines device_id @alias Device ID @prefix SYS_DEVICE_ID_
#define SYS_DEVICE_ID_NONE 0xFFu  //@alias No device @description Never a device: no device in a pin reference; SUSPEND, RESUME, RESET and REMOVE written to it run on every device

/** @brief The number of devices a board holds: the IDs are 0..SYS_DEVICE_COUNT-1. */
#define SYS_DEVICE_COUNT 64u

#define SYS_DEVICE_OP(contract, n) ((uint16_t)(((contract) << 8) | (n)))

/* Contract IDs. A contract's ID is its module's number, MODULE_ID_<NAME> (MODULE_ID_DECLARE, module_id.h): the same
 * as the root key of its commands, the module of its errors and of its events. The registry's own is
 * MODULE_ID_SYS_DEVICE (the device's lifecycle ops); 0xFF is a device's own ops, which no contract takes. Every other
 * contract gives its number in its own <contract>_id.h, MODULE_ID_DECLARE(MODULE_ID_IO, 0x01), and its table, after the header includes it,
 * SYS_DEVICE_CONTRACT_DECLARE(CONTRACT_IO, MODULE_ID_IO). No list of contracts exists and no file includes them all
 * (module_id.h: how two with one number are caught). The ID is wire format (an op is [contract u8][op u8]): never
 * renumber it. */

//#ref-defines device_contract_own @alias Device own ops @prefix SYS_DEVICE_CONTRACT_
#define SYS_DEVICE_CONTRACT_OWN    0xFFu  //@alias Device own ops @description Op prefix reserved for a device driver's own commands (not a shared contract)

enum { SYS_DEVICE__CONTRACT_ID_0xFF };  // reserved: no contract takes it

#define SYS_DEVICE_OWN_OP(n) SYS_DEVICE_OP(SYS_DEVICE_CONTRACT_OWN, n)  // what a table of own ops gives P_<OP_NAME>

/* An op's flags, the last column of a contract table: what every driver's row of the op gets, so no driver can leave
 * it out.
 * ALWAYS: plumbing, the write runs while the device is suspended (an event delivery, a pin lock); else
 *         ERR_DEV_SUSPENDED.
 * LOCAL:  C only: its args carry a pointer (the display's FLUSH, an I2C bus's XFER), or it is a driver's op and not a
 *         user's (IO's LOCK, UNLOCK). The device router and a remote device refuse it, ERR_DEV_LOCAL_OP.
 * NOLOCK: the handler runs without the lock of the device's slot, so calls of it run side by side with each other and
 *         with the device's locked ops (a UART's READ waiting for bytes on one task, its WRITE on another). The checks
 *         (exists, the op, suspended) are made under the lock as for any op; then the call is counted in flight and
 *         the lock let go. REMOVE waits until no call is in flight, so the driver's struct outlives every one of them.
 *         The driver makes the op safe against its own locked ops (SUSPEND, RESET, ...) and bounds its time (a
 *         timeout of its args): a NOLOCK call that never returns blocks REMOVE.
 * TAIL:   the write's payload is its value_t (the head, packed) and then bytes of any number (an event's payload, the
 *         data of a register write). Its handler is OP_DECLARE_WRITE_TAIL, which gets the head, the tail and its length;
 *         OP_DECLARE_WRITE refuses to compile for it, and OP_DECLARE_WRITE_TAIL for an op without the flag. */
#define SYS_DEVICE_OP_ALWAYS (1u << 0)
#define SYS_DEVICE_OP_LOCAL  (1u << 1)
#define SYS_DEVICE_OP_NOLOCK (1u << 2)
#define SYS_DEVICE_OP_TAIL   (1u << 3)

/* A contract is a table of ops in the format of every set of commands (cmd_table.h: P_OPERATIONS(X, P) of rows
 * X(P, OP_NAME, n, key_t, value_t, flags), cmd_none_t for a side with nothing). SYS_DEVICE_CONTRACT_DECLARE(P, id)
 * gives what the table gives everywhere, the types P_key_<OP_NAME>_t and P_value_<OP_NAME>_t, and what a contract
 * adds: P_<OP_NAME> = SYS_DEVICE_OP(id, n) and P_FLAGS_<OP_NAME> (SYS_DEVICE_OP_*). `id` is the contract's
 * MODULE_ID_<NAME>; P__ID (the anchor the rows reach it by) is the table's own, never used outside it. */
#define SYS_DEVICE__ID(P, name, n, key_t, value_t, flags) P##_##name = SYS_DEVICE_OP(P##__ID, n),
#define SYS_DEVICE__FLAGS(P, name, n, key_t, value_t, flags) P##_FLAGS_##name = (flags),
#define SYS_DEVICE__TABLE(P, id)                                   \
  enum { P##__ID = (id), P##_OPERATIONS(SYS_DEVICE__ID, P) };      \
  enum { P##_OPERATIONS(SYS_DEVICE__FLAGS, P) };                   \
  P##_OPERATIONS(CMD__TABLE_TYPES, P)
#define SYS_DEVICE_CONTRACT_DECLARE(P, id) SYS_DEVICE__TABLE(P, id)

/** @brief INFO answer. */
typedef struct __attribute__((packed)) {
  uint16_t type_id;   /**< Device type ID */
  uint8_t suspended;  /**< 1 if suspended, 0 otherwise */
} sys_device_info_t;

/**
 * @brief Device contract (the module's number, 0x20xx, prefix CONTRACT_SYS_DEVICE): the lifecycle, X(P, OP_NAME, n, key_t, value_t, flags). CREATE
 * (0x00, write) and CREATE_REMOTE (0x08) are not in the table: they have no instance yet. Writes: RESET, SUSPEND,
 * RESUME, REMOVE; reads: INFO, OPS (answered as a list, so its value is none here). SUSPEND, RESUME, REMOVE, INFO and
 * OPS are the registry's for every device: a driver's op table lists none of them. 0x07 is unused: never assign it.
 * A device's error policy is not here: sys_device_policy.h.
 */
#define CONTRACT_SYS_DEVICE_OPERATIONS(X, P)               \
  X(P, OP_RESET,   0x01, cmd_none_t, cmd_none_t,        0) \
  X(P, OP_SUSPEND, 0x02, cmd_none_t, cmd_none_t,        0) \
  X(P, OP_RESUME,  0x03, cmd_none_t, cmd_none_t,        0) \
  X(P, OP_REMOVE,  0x04, cmd_none_t, cmd_none_t,        0) \
  X(P, OP_INFO,    0x05, cmd_none_t, sys_device_info_t, 0) \
  X(P, OP_OPS,     0x06, cmd_none_t, cmd_none_t,        0)

/* CREATE's [flags u8]. SYSTEM: the board's own device, it can't be removed (ERR_DEV_SYSTEM) and the REMOVE sweep leaves
 * it, so changing the project removes only the other devices. */
#define SYS_DEVICE_CREATE_HDR    3u     // [type u16][flags u8], then the create struct
#define SYS_DEVICE_CREATE_SYSTEM 0x01u

enum {
  CONTRACT_SYS_DEVICE_OP_CREATE = SYS_DEVICE_OP(MODULE_ID_SYS_DEVICE, 0x00),  // [type u16][flags u8][create struct]
  CONTRACT_SYS_DEVICE_OP_CREATE_REMOTE = SYS_DEVICE_OP(MODULE_ID_SYS_DEVICE, 0x08),  // [type u16][sys_device_remote_t]
};
SYS_DEVICE_CONTRACT_DECLARE(CONTRACT_SYS_DEVICE, MODULE_ID_SYS_DEVICE)

/* A remote device's flags (sys_device_remote_t.flags): how its calls go (SYS_DEVICE_REMOTE.MD "A call"). Exactly one. */
//#ref-defines device_remote_flag @alias Remote device flags @prefix SYS_DEVICE_REMOTE_
#define SYS_DEVICE_REMOTE_WIRED    (1u << 0)  //@alias Wired @description A neighbour on a wired link: every call waits for its answer, no copies
#define SYS_DEVICE_REMOTE_WIRELESS (1u << 1)  //@alias Wireless @description A board over a radio: a write returns once taken, a read gives the latest copy

/** @brief Where a remote device is: CREATE_REMOTE's struct, after the type. */
typedef struct __attribute__((packed)) {
  uint8_t board;      /**< The board the device is on */
  uint8_t remote_id;  /**< Its ID there: below the number of devices a board holds */
  uint8_t flags;      /**< SYS_DEVICE_REMOTE_*, exactly one */
} sys_device_remote_t;

_Static_assert(sizeof(sys_device_remote_t) == 3, "remote record");

/* ----------------------------------------------------------------------------
 * Calls
 * ------------------------------------------------------------------------- */

/**
 * @brief Runs side `dir` (cmd_dir_e) of op `op` of device `id` without a transport: device-to-device calls, the
 * contract wrappers, event listeners, timers, the board creating its devices (op CREATE, a write, args [type u16][flags
 * u8][create struct]). `in` and `out` are the op's wire payload and answer, the same types a handler gets (any size:
 * the payload is used in place).
 * @param[in]     id  Device ID
 * @param[in]     dir cmd_dir_e: the read or the write side
 * @param[in]     op  Op number, SYS_DEVICE_OP(contract, n)
 * @param[in]     in  The op's payload (`data` may be NULL when `len` is 0)
 * @param[in,out] out The answer: a buffer (`data`, `cap`) the op fills, `len` is its size; may be NULL for no answer
 * @return ERR_DEV_NOT_FOUND, ERR_DEV_UNKNOWN_OP, ERR_DEV_SUSPENDED, ERR_DEV_BUSY (the device's lock was not free
 * within CONFIG_SYS_DEVICE_LOCK_TIMEOUT_MS), ERR_DEV_REENTRY (the calling task is inside an op of this device), or
 * the op's error, wrapped in ERR_DEV_ID and ERR_DEV_OP
 * @usage cmd_out_t out = {.data = buf, .cap = sizeof(buf)};
 *        ERR_TRY(sys_device_call(id, CMD_DIR_READ, CONTRACT_SYS_DEVICE_OP_OPS, (cmd_in_t){NULL, 0}, &out));
 */
MUST_USE err_h sys_device_call(uint8_t id, uint8_t dir, uint16_t op, cmd_in_t in, cmd_out_t* out);

/**
 * @brief The write side of `op`: sys_device_call with no answer. The contract wrappers' setters and actions.
 * @param[in] id       Device ID
 * @param[in] op       Op number, SYS_DEVICE_OP(contract, n)
 * @param[in] args     The op's payload
 * @param[in] args_len Payload size in bytes
 * @return As sys_device_call
 */
MUST_USE err_h sys_device_write(uint8_t id, uint16_t op, const void* args, uint32_t args_len);

/** @brief The largest create struct sys_device_create_system takes. */
#define SYS_DEVICE_CREATE_CFG_MAX 32u

/**
 * @brief CREATE of the board's own device (SYS_DEVICE_CREATE_SYSTEM: it can't be removed): the frame
 * [type u16][flags u8][cfg] built here, so the caller names the type and the create struct only. Boot code.
 * @param[in] id      The device ID
 * @param[in] type_id The driver's type
 * @param[in] cfg     The driver's create struct (NULL with `cfg_len` 0 when it has none)
 * @param[in] cfg_len Its size, at most SYS_DEVICE_CREATE_CFG_MAX
 * @return As CREATE; ERR_CMD_BAD_LEN when `cfg_len` is over the limit
 */
MUST_USE err_h sys_device_create_system(uint8_t id, uint16_t type_id, const void* cfg, uint32_t cfg_len);

/**
 * @brief The read side of `op` with the key `args`, whose answer has a fixed size (a reading, a mask): ERR_CMD_BAD_LEN
 * unless the device answered exactly `out_size` bytes. The contract wrappers' getters.
 * @param[in]  id       Device ID
 * @param[in]  op       Op number, SYS_DEVICE_OP(contract, n)
 * @param[in]  args     The key (a pin, a channel), or NULL
 * @param[in]  args_len Key size in bytes
 * @param[out] out      Buffer for the answer
 * @param[in]  out_size The answer's exact size in bytes
 * @return ERR_CMD_BAD_LEN when the answer has another size, else as sys_device_call
 */
MUST_USE err_h sys_device_read(uint8_t id, uint16_t op, const void* args, uint32_t args_len, void* out,
                               uint32_t out_size);

/* The typed calls: op NAME of the contract with prefix P, its number and the sizes of its key and value from the
 * contract table, so the two can't be paired wrongly. SET / GET build the frame from the fields given (the compiler
 * checks their names); WRITE / READ take a frame the caller has already. */

/**
 * @brief Writes op OP_NAME with its value built from the fields given (none for an op without args).
 * @usage ERR_TRY(SYS_DEVICE_SET(motor, CONTRACT_HBRIDGE, OP_DRIVE, .channel = 0, .drive = 800));
 *        ERR_TRY(SYS_DEVICE_SET(id, CONTRACT_SYS_DEVICE, OP_SUSPEND));
 */
#define SYS_DEVICE_SET(id, P, name, ...)                                                                 \
  sys_device_write((id), P##_##name, &(const P##_value_##name##_t){__VA_ARGS__}, sizeof(P##_value_##name##_t))

/**
 * @brief Reads op OP_NAME into `*answer` (its value type), the key built from the fields given after it (none for an op
 * without a key). ERR_CMD_BAD_LEN unless the device answered exactly that size.
 * @usage int32_t mA; ERR_TRY(SYS_DEVICE_GET(mon, CONTRACT_PMON, OP_CURRENT, &mA, .channel = 1));
 *        sys_device_info_t info; ERR_TRY(SYS_DEVICE_GET(id, CONTRACT_SYS_DEVICE, OP_INFO, &info));
 */
#define SYS_DEVICE_GET(id, P, name, answer, ...)                                                         \
  sys_device_read((id), P##_##name, &(const P##_key_##name##_t){__VA_ARGS__}, sizeof(P##_key_##name##_t), \
                  (P##_value_##name##_t*)(answer), sizeof(P##_value_##name##_t))

/**
 * @brief Writes op OP_NAME with a value the caller has (a pointer to its value type).
 * @usage ERR_TRY(SYS_DEVICE_WRITE(id, TOUCH, OP_CALIBRATION, cal));
 */
#define SYS_DEVICE_WRITE(id, P, name, value) \
  sys_device_write((id), P##_##name, (const P##_value_##name##_t*)(value), sizeof(P##_value_##name##_t))

/**
 * @brief Reads op OP_NAME with a key the caller has (a pointer to its key type, NULL for none) into `*value`.
 * @usage ERR_TRY(SYS_DEVICE_READ(id, TOUCH, OP_POINT, NULL, &point));
 */
#define SYS_DEVICE_READ(id, P, name, key, value)                                                  \
  sys_device_read((id), P##_##name, (const P##_key_##name##_t*)(key), sizeof(P##_key_##name##_t), \
                  (P##_value_##name##_t*)(value), sizeof(P##_value_##name##_t))

/**
 * @brief Runs SUSPEND, RESUME, RESET or REMOVE (`op`, else ERR_DEV_UNKNOWN_OP) on every device, in the order of the
 * declared uses: a device before the devices it uses, RESUME the other way round. REMOVE leaves the board's own
 * devices (SYS_DEVICE_CREATE_SYSTEM); RESET skips a device whose type has none. Best effort: all are tried, the first
 * error is returned (wrapped in ERR_DEV_ID), the others go to err_report(); a device whose lock is not free in time
 * fails with ERR_DEV_BUSY and the sweep goes on.
 * SUSPEND of every device is the fault safe state. While it runs, a write that comes from no device (a command, the
 * VM, a module) and a CREATE are refused on every device (ERR_DEV_SUSPENDED), also on the ones the sweep has not
 * reached yet; a device's own calls still go through, so a suspend() hook reaches the devices it uses.
 * One event tells of the sweep (SUSPENDED / RESUMED / RESET with source SYS_DEVICE_ID_NONE), published with every
 * event of the sweep after its last device; REMOVE publishes each device's REMOVED.
 * @param[in] op CONTRACT_SYS_DEVICE_OP_SUSPEND, _RESUME, _RESET or _REMOVE
 * @return ERR_DEV_UNKNOWN_OP for another op, else the first error of the sweep
 * @usage err_report(sys_device_sweep(CONTRACT_SYS_DEVICE_OP_SUSPEND));
 */
MUST_USE err_h sys_device_sweep(uint16_t op);

#ifdef __cplusplus
}
#endif
