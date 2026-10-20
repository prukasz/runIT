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
 *    them IDs one after another; the app names none. After the other sections;
 *  - id == SYS_PROJECT_SECTION_START: what starts the project once it is loaded (the VM's EXEC). Run by RUN, left out
 *    by LOAD. The last section.
 *
 * Nothing changes while the stream is coming: every section is staged under IDs of its own and only COMMIT (length
 * and CRC checked) makes the project the stored one, with one record store (the project record: the RUN frames of the
 * sections, in order). A broken or aborted save leaves the stored project and the settings as they were. COMMIT with
 * the project's boot option (sys_project_boot_e, kept in NVS) then loads or runs it, as at a boot. All ops run on a
 * router task.
 */

#define SYS_PROJECT_SECTION_PROGRAM 0u
#define SYS_PROJECT_SECTION_START   0xFFFFFFFFu

/* What the board does with the stored project when it starts (and right after a save). */
//#ref-enum @alias Project at start
typedef enum {
  SYS_PROJECT_BOOT_NONE = 0,  //@alias Nothing @description The board starts with its defaults; the project waits for LOAD or RUN
  SYS_PROJECT_BOOT_LOAD = 1,  //@alias Load @description Settings, devices and program loaded; the program waits to be started
  SYS_PROJECT_BOOT_RUN  = 2,  //@alias Run @description Loaded and started
  SYS_PROJECT_BOOT_COUNT,
} sys_project_boot_e;

/* The record IDs of the pipeline: class PROJECT, index [bank + 1 u8][n u16]. Two banks alternate, so a save never
 * touches the stored project's records. n = 0 of a bank is the program's list of chunks, 1.. are given in order,
 * SYS_PROJECT_N_START the start section's record. */
#define SYS_PROJECT_BANKS   2u
#define SYS_PROJECT_N_START 0xFFFFu
#define SYS_PROJECT_ID(bank, n) \
  SYS_ACTIONS_ID(SYS_ACTIONS_CLASS_PROJECT, ((((uint32_t)(bank) + 1u) << 16) | (uint32_t)(n)))

#define ERR_SYS_PROJECT_MAP(X)                                                                                                                                                   \
  X(ERR_PROJECT_BUSY,          ERR_TAG(ERR_MOD_SYS_PROJECT, 1),  ERR_SEV_MEDIUM, {})                               /* a save is open */                                          \
  X(ERR_PROJECT_NO_SESSION,    ERR_TAG(ERR_MOD_SYS_PROJECT, 2),  ERR_SEV_MEDIUM, {})                                                                                             \
  X(ERR_PROJECT_TOO_BIG,       ERR_TAG(ERR_MOD_SYS_PROJECT, 3),  ERR_SEV_MEDIUM, { uint32_t len; uint32_t max; })                                                                \
  X(ERR_PROJECT_OFFSET,        ERR_TAG(ERR_MOD_SYS_PROJECT, 4),  ERR_SEV_MEDIUM, { uint32_t got; uint32_t want; })                                                               \
  X(ERR_PROJECT_INCOMPLETE,    ERR_TAG(ERR_MOD_SYS_PROJECT, 5),  ERR_SEV_MEDIUM, { uint32_t got; uint32_t want; })                                                               \
  X(ERR_PROJECT_CRC,           ERR_TAG(ERR_MOD_SYS_PROJECT, 6),  ERR_SEV_MEDIUM, { uint32_t got; uint32_t want; })                                                               \
  X(ERR_PROJECT_BAD_FRAME,     ERR_TAG(ERR_MOD_SYS_PROJECT, 7),  ERR_SEV_MEDIUM, { uint32_t offset; })             /* length 0 or above a link frame */                          \
  X(ERR_PROJECT_TRUNCATED,     ERR_TAG(ERR_MOD_SYS_PROJECT, 8),  ERR_SEV_MEDIUM, {})                               /* the stream ends inside a frame */                          \
  X(ERR_PROJECT_NO_SECTION,    ERR_TAG(ERR_MOD_SYS_PROJECT, 9),  ERR_SEV_MEDIUM, {})                               /* a frame before the first SECTION */                        \
  X(ERR_PROJECT_BAD_SECTION,   ERR_TAG(ERR_MOD_SYS_PROJECT, 10), ERR_SEV_MEDIUM, { uint32_t id; })                 /* class not allowed, or a SECTION frame of the wrong size */ \
  X(ERR_PROJECT_SECTION_DUP,   ERR_TAG(ERR_MOD_SYS_PROJECT, 11), ERR_SEV_MEDIUM, { uint32_t id; })                                                                               \
  X(ERR_PROJECT_SECTION_FULL,  ERR_TAG(ERR_MOD_SYS_PROJECT, 12), ERR_SEV_MEDIUM, { uint32_t max; })                                                                              \
  X(ERR_PROJECT_SECTION_BIG,   ERR_TAG(ERR_MOD_SYS_PROJECT, 13), ERR_SEV_MEDIUM, { uint32_t id; uint32_t max; })                                                                 \
  X(ERR_PROJECT_SECTION_EMPTY, ERR_TAG(ERR_MOD_SYS_PROJECT, 14), ERR_SEV_LOW,    { uint32_t id; })                                                                               \
  X(ERR_PROJECT_ORDER,         ERR_TAG(ERR_MOD_SYS_PROJECT, 15), ERR_SEV_MEDIUM, {})                               /* a second program or start, or a section after them */      \
  X(ERR_PROJECT_CHUNKS,        ERR_TAG(ERR_MOD_SYS_PROJECT, 16), ERR_SEV_MEDIUM, { uint32_t max; })                /* the program needs more chunks */                           \
  X(ERR_PROJECT_EMPTY,         ERR_TAG(ERR_MOD_SYS_PROJECT, 17), ERR_SEV_LOW,    {})                               /* a stream with no section */                                \
  X(ERR_PROJECT_NOT_STORED,    ERR_TAG(ERR_MOD_SYS_PROJECT, 18), ERR_SEV_LOW,    {})                                                                                             \
  X(ERR_PROJECT_NOT_IN_STREAM, ERR_TAG(ERR_MOD_SYS_PROJECT, 19), ERR_SEV_MEDIUM, {})                               /* a SECTION frame sent as a command */                       \
  X(ERR_PROJECT_RUN,           ERR_TAG(ERR_MOD_SYS_PROJECT, 20), ERR_SEV_NONE,   {})                               /* context: stored, but the run failed */                     \
  X(ERR_PROJECT_BAD_ARG,       ERR_TAG(ERR_MOD_SYS_PROJECT, 21), ERR_SEV_HIGH,   {})                                                                                             \
  X(ERR_PROJECT_BAD_BOOT,      ERR_TAG(ERR_MOD_SYS_PROJECT, 22), ERR_SEV_MEDIUM, { uint8_t value; })               /* not a sys_project_boot_e */

ERR_TAGS_DECLARE(ERR_SYS_PROJECT_MAP)

/* First frame of the project record: [ROUTER_KEY_PROJECT][SYS_PROJECT_OP_MARK] and this. */
typedef struct __attribute__((packed)) {
  uint8_t bank;    /**< Active project bank */
  uint32_t crc32;  /**< Stream CRC-32 */
  uint32_t bytes;  /**< Stream length in bytes */
} sys_project_mark_t;

typedef struct __attribute__((packed)) {
  uint8_t stored;     /**< 1 = project stored, 0 = no project */
  uint8_t boot;       /**< sys_project_boot_e */
  uint8_t bank;       /**< Bank holding program records */
  uint32_t crc32;     /**< Stream CRC-32 */
  uint32_t bytes;     /**< Stream length in bytes */
} sys_project_info_t;

/** @brief Opens the project's options in the actions partition. Boot, after sys_actions_init. */
MUST_USE err_h sys_project_init(void);

/**
 * @brief Starts a save of a stream of `len` bytes (ERR_PROJECT_BUSY while another is open). `boot` (sys_project_boot_e)
 * becomes the boot option when the save commits. Clears the records a failed save left in the spare bank.
 */
MUST_USE err_h sys_project_begin(uint32_t len, uint8_t boot);

/** @brief The next bytes of the stream; `offset` must equal the bytes so far. A failure drops the save. */
MUST_USE err_h sys_project_write(uint32_t offset, const uint8_t* data, uint32_t len);

/**
 * @brief Checks length, CRC and framing, stores the project and makes it the stored one; the stale records of the
 * other bank go. Then it loads or runs it as its boot option says (ERR_PROJECT_RUN wraps a failure: the project is
 * stored anyway). The save is closed also on a failure.
 */
MUST_USE err_h sys_project_commit(uint32_t crc32);

/** @brief Drops an open save; the stored project stays. No-op when none is open. */
void sys_project_abort(void);

MUST_USE err_h sys_project_info(sys_project_info_t* out);

/**
 * @brief Loads the stored project (RUN_ALL of its record: one failing section doesn't stop the others), then, with
 * `start` and when all of it loaded, runs its start section. Router task only.
 */
MUST_USE err_h sys_project_run(bool start);

/**
 * @brief Removes the devices the project created (the board's stay), the stored project and its program records (the
 * settings and device records stay). Stop the VM first: its program would call devices that are gone.
 */
MUST_USE err_h sys_project_erase(void);

/** @brief Sets the boot option (sys_project_boot_e; ERR_PROJECT_BAD_BOOT for another value). */
MUST_USE err_h sys_project_set_boot(uint8_t boot);

/** @brief The boot option (SYS_PROJECT_BOOT_NONE when never set). */
uint8_t sys_project_get_boot(void);

/** @brief Boot, once the router and the VM are up: queues LOAD or RUN of the stored project, as its boot option says. */
MUST_USE err_h sys_project_boot(void);

#ifdef __cplusplus
}
#endif
