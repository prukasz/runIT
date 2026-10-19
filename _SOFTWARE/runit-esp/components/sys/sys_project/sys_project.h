#pragma once

#include "errors.h"
#include "sys_actions.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief The stored project (SYS_PROJECT.MD): one stream of frames in, split into records.
 *
 * The app saves a project as ONE stream of frames, cut into sections. A section opens with a SECTION frame
 * ([ROUTER_KEY_PROJECT][SYS_PROJECT_OP_SECTION][u32 id]) and holds the frames after it:
 *  - id != 0: a record under that ID (a system setting, a device's install and defaults, a user action), stored
 *    as it is, in the stream's order;
 *  - id == SYS_PROJECT_SECTION_PROGRAM: the program. The pipeline cuts it into records of whole frames and gives
 *    them IDs one after another; the app names none. It must be the last section.
 *
 * Nothing changes while the stream is coming: every section is staged under IDs of its own and only COMMIT (length
 * and CRC checked) makes the project the stored one, with one record store (the project record: the RUN frames of the
 * sections, in order). A broken or aborted save leaves the stored project and the settings as they were. COMMIT with
 * SYS_PROJECT_FLAG_RUN then runs it. All ops run on a router task.
 */

#define SYS_PROJECT_SECTION_PROGRAM 0u

#define SYS_PROJECT_FLAG_AUTOSTART 0x01u  // BEGIN: replay the project at every boot (kept with the project)
#define SYS_PROJECT_FLAG_RUN 0x02u        // BEGIN: run it as soon as it is committed

/* The record IDs of the pipeline: class PROJECT, index [bank + 1 u8][n u16]. Two banks alternate, so a save never
 * touches the stored project's records. n = 0 of a bank is the program's list of chunks, 1.. are given in order. */
#define SYS_PROJECT_BANKS 2u
#define SYS_PROJECT_ID(bank, n) \
  SYS_ACTIONS_ID(SYS_ACTIONS_CLASS_PROJECT, ((((uint32_t)(bank) + 1u) << 16) | (uint32_t)(n)))

// clang-format off
#define ERR_SYS_PROJECT_MAP(X)                                                                                         \
  X(ERR_PROJECT_BUSY,          ERR_TAG(ERR_MOD_SYS_PROJECT, 1),  ERR_SEV_MEDIUM, {}) /* a save is open */              \
  X(ERR_PROJECT_NO_SESSION,    ERR_TAG(ERR_MOD_SYS_PROJECT, 2),  ERR_SEV_MEDIUM, {})                                  \
  X(ERR_PROJECT_TOO_BIG,       ERR_TAG(ERR_MOD_SYS_PROJECT, 3),  ERR_SEV_MEDIUM, { uint32_t len; uint32_t max; })      \
  X(ERR_PROJECT_OFFSET,        ERR_TAG(ERR_MOD_SYS_PROJECT, 4),  ERR_SEV_MEDIUM, { uint32_t got; uint32_t want; })     \
  X(ERR_PROJECT_INCOMPLETE,    ERR_TAG(ERR_MOD_SYS_PROJECT, 5),  ERR_SEV_MEDIUM, { uint32_t got; uint32_t want; })     \
  X(ERR_PROJECT_CRC,           ERR_TAG(ERR_MOD_SYS_PROJECT, 6),  ERR_SEV_MEDIUM, { uint32_t got; uint32_t want; })     \
  X(ERR_PROJECT_BAD_FRAME,     ERR_TAG(ERR_MOD_SYS_PROJECT, 7),  ERR_SEV_MEDIUM, { uint32_t offset; }) /* length 0 or above a link frame */ \
  X(ERR_PROJECT_TRUNCATED,     ERR_TAG(ERR_MOD_SYS_PROJECT, 8),  ERR_SEV_MEDIUM, {}) /* the stream ends inside a frame */ \
  X(ERR_PROJECT_NO_SECTION,    ERR_TAG(ERR_MOD_SYS_PROJECT, 9),  ERR_SEV_MEDIUM, {}) /* a frame before the first SECTION */ \
  X(ERR_PROJECT_BAD_SECTION,   ERR_TAG(ERR_MOD_SYS_PROJECT, 10), ERR_SEV_MEDIUM, { uint32_t id; }) /* class not allowed, or a SECTION frame of the wrong size */ \
  X(ERR_PROJECT_SECTION_DUP,   ERR_TAG(ERR_MOD_SYS_PROJECT, 11), ERR_SEV_MEDIUM, { uint32_t id; })                    \
  X(ERR_PROJECT_SECTION_FULL,  ERR_TAG(ERR_MOD_SYS_PROJECT, 12), ERR_SEV_MEDIUM, { uint32_t max; })                   \
  X(ERR_PROJECT_SECTION_BIG,   ERR_TAG(ERR_MOD_SYS_PROJECT, 13), ERR_SEV_MEDIUM, { uint32_t id; uint32_t max; })      \
  X(ERR_PROJECT_SECTION_EMPTY, ERR_TAG(ERR_MOD_SYS_PROJECT, 14), ERR_SEV_LOW,    { uint32_t id; })                    \
  X(ERR_PROJECT_ORDER,         ERR_TAG(ERR_MOD_SYS_PROJECT, 15), ERR_SEV_MEDIUM, {}) /* a second program, or a section after it */ \
  X(ERR_PROJECT_CHUNKS,        ERR_TAG(ERR_MOD_SYS_PROJECT, 16), ERR_SEV_MEDIUM, { uint32_t max; }) /* the program needs more chunks */ \
  X(ERR_PROJECT_EMPTY,         ERR_TAG(ERR_MOD_SYS_PROJECT, 17), ERR_SEV_LOW,    {}) /* a stream with no section */   \
  X(ERR_PROJECT_NOT_STORED,    ERR_TAG(ERR_MOD_SYS_PROJECT, 18), ERR_SEV_LOW,    {})                                  \
  X(ERR_PROJECT_NOT_IN_STREAM, ERR_TAG(ERR_MOD_SYS_PROJECT, 19), ERR_SEV_MEDIUM, {}) /* a SECTION frame sent as a command */ \
  X(ERR_PROJECT_RUN,           ERR_TAG(ERR_MOD_SYS_PROJECT, 20), ERR_SEV_NONE,   {}) /* context: stored, but the run failed */ \
  X(ERR_PROJECT_BAD_ARG,       ERR_TAG(ERR_MOD_SYS_PROJECT, 21), ERR_SEV_HIGH,   {})
// clang-format on

ERR_TAGS_DECLARE(ERR_SYS_PROJECT_MAP)

/* First frame of the project record: [ROUTER_KEY_PROJECT][SYS_PROJECT_OP_MARK] and this. */
typedef struct __attribute__((packed)) {
  uint8_t bank;    /**< Active project bank */
  uint32_t crc32;  /**< Stream CRC-32 */
  uint32_t bytes;  /**< Stream length in bytes */
} sys_project_mark_t;

typedef struct __attribute__((packed)) {
  uint8_t stored;     /**< 1 = project stored, 0 = no project */
  uint8_t autostart;  /**< 1 = autostart enabled, 0 = disabled */
  uint8_t bank;       /**< Bank holding program records */
  uint32_t crc32;     /**< Stream CRC-32 */
  uint32_t bytes;     /**< Stream length in bytes */
} sys_project_info_t;

/** @brief Opens the project's options in the actions partition. Boot, after sys_actions_init. */
MUST_USE err_h sys_project_init(void);

/**
 * @brief Starts a save of a stream of `len` bytes (ERR_PROJECT_BUSY while another is open). `flags`
 * SYS_PROJECT_FLAG_*, applied when the save commits. Clears the records a failed save left in the spare bank.
 */
MUST_USE err_h sys_project_begin(uint32_t len, uint8_t flags);

/** @brief The next bytes of the stream; `offset` must equal the bytes so far. A failure drops the save. */
MUST_USE err_h sys_project_write(uint32_t offset, const uint8_t* data, uint32_t len);

/**
 * @brief Checks length, CRC and framing, stores the project and makes it the stored one; the stale records of the
 * other bank go. With SYS_PROJECT_FLAG_RUN it then runs it (ERR_PROJECT_RUN wraps a failure: the project is stored
 * anyway). The save is closed also on a failure.
 */
MUST_USE err_h sys_project_commit(uint32_t crc32);

/** @brief Drops an open save; the stored project stays. No-op when none is open. */
void sys_project_abort(void);

MUST_USE err_h sys_project_info(sys_project_info_t* out);

/** @brief Runs the stored project (RUN_ALL of its record: one failing section doesn't stop the others). Router task only. */
MUST_USE err_h sys_project_run(void);

/** @brief Removes the stored project and its program records (the settings and device records stay). */
MUST_USE err_h sys_project_erase(void);

MUST_USE err_h sys_project_set_autostart(bool on);

/** @brief Boot, once the router and the VM are up: queues a run of the project when it is stored and autostart is on. */
MUST_USE err_h sys_project_boot(void);

#ifdef __cplusplus
}
#endif
