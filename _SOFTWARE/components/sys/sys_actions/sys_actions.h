#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "errors.h"
#include "router.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Actions: lists of router commands stored in NVS under an ID
 * (1..255) with a name next to it, replayed through router_route
 * (SYS_ACTIONS.MD).
 *
 * An action is filled by recording a client's live commands (the router tap)
 * or by uploading the whole image, and read back as the whole image. It runs
 * only on a router task: a remote RUN, or router_inject of a RUN frame
 * (sys_actions_request) from anywhere else. So a replay never interleaves with
 * live normal frames, and an urgent STOP still overtakes it.
 *
 * Image: frames back to back, each [u16 len LE][keys + payload] (what
 * router_route takes: no seq / flags), 1..CONFIG_ROUTER_FRAME_MAX - 2 bytes.
 * The name is a label only: 0..15 printable ASCII characters, not unique.
 */

#define SYS_ACTIONS_ID_NONE 0u      // "no action"; IDs are 1..255
#define SYS_ACTIONS_NAME_MAX 16u    // including the NUL: 15 characters

// clang-format off
#define ERR_SYS_ACTIONS_MAP(X)                                                                                   \
  X(ERR_ACTION_NOT_FOUND,  ERR_TAG(ERR_MOD_SYS_ACTIONS, 1),  ERR_SEV_MEDIUM, { uint8_t id; })                    \
  X(ERR_ACTION_BAD_NAME,   ERR_TAG(ERR_MOD_SYS_ACTIONS, 2),  ERR_SEV_HIGH,   {}) /* longer than 15 or not printable ASCII */ \
  X(ERR_ACTION_BUSY,       ERR_TAG(ERR_MOD_SYS_ACTIONS, 3),  ERR_SEV_MEDIUM, {}) /* a recording or upload is open */ \
  X(ERR_ACTION_NO_SESSION, ERR_TAG(ERR_MOD_SYS_ACTIONS, 4),  ERR_SEV_MEDIUM, {}) /* no recording / upload of that kind open */ \
  X(ERR_ACTION_TOO_BIG,    ERR_TAG(ERR_MOD_SYS_ACTIONS, 5),  ERR_SEV_MEDIUM, { uint16_t len; uint16_t max; }) \
  X(ERR_ACTION_OFFSET,     ERR_TAG(ERR_MOD_SYS_ACTIONS, 6),  ERR_SEV_MEDIUM, { uint16_t got; uint16_t want; }) /* upload write out of order */ \
  X(ERR_ACTION_INCOMPLETE, ERR_TAG(ERR_MOD_SYS_ACTIONS, 7),  ERR_SEV_MEDIUM, { uint16_t got; uint16_t want; }) /* commit before every byte came */ \
  X(ERR_ACTION_CRC,        ERR_TAG(ERR_MOD_SYS_ACTIONS, 8),  ERR_SEV_MEDIUM, { uint32_t got; uint32_t want; }) \
  X(ERR_ACTION_BAD_FRAME,  ERR_TAG(ERR_MOD_SYS_ACTIONS, 9),  ERR_SEV_MEDIUM, { uint16_t offset; }) /* image framing broken there */ \
  X(ERR_ACTION_EMPTY,      ERR_TAG(ERR_MOD_SYS_ACTIONS, 10), ERR_SEV_LOW,    {}) /* recording stopped with no frame */ \
  X(ERR_ACTION_DEPTH,      ERR_TAG(ERR_MOD_SYS_ACTIONS, 11), ERR_SEV_MEDIUM, { uint8_t depth; }) /* RUN nested deeper than CONFIG_SYS_ACTIONS_MAX_DEPTH */ \
  X(ERR_ACTION_WRONG_TASK, ERR_TAG(ERR_MOD_SYS_ACTIONS, 12), ERR_SEV_HIGH,   {}) /* sys_actions_run outside a router task */ \
  X(ERR_ACTION_READ_RANGE, ERR_TAG(ERR_MOD_SYS_ACTIONS, 13), ERR_SEV_MEDIUM, { uint16_t offset; uint16_t len; uint16_t size; }) \
  X(ERR_ACTION_NO_MEM,     ERR_TAG(ERR_MOD_SYS_ACTIONS, 14), ERR_SEV_HIGH,   { uint32_t size; }) \
  X(ERR_ACTION_STEP,       ERR_TAG(ERR_MOD_SYS_ACTIONS, 15), ERR_SEV_NONE,   { uint8_t id; uint16_t step; }) /* context: frame `step` (from 0) failed */ \
  X(ERR_ACTION_BAD_ARG,    ERR_TAG(ERR_MOD_SYS_ACTIONS, 16), ERR_SEV_HIGH,   {}) \
  X(ERR_ACTION_BAD_ID,     ERR_TAG(ERR_MOD_SYS_ACTIONS, 17), ERR_SEV_HIGH,   {}) /* ID 0 (SYS_ACTIONS_ID_NONE) */
// clang-format on

ERR_TAGS_DECLARE(ERR_SYS_ACTIONS_MAP)

typedef struct {
  uint16_t bytes;                   // image size
  uint16_t count;                   // frames
  uint32_t crc32;                   // of the image (esp_rom_crc32_le(0, ...), zlib / IEEE)
  char name[SYS_ACTIONS_NAME_MAX];  // NUL-ended, may be empty
} sys_actions_info_t;

/** @return true to go on, false to stop the listing. */
typedef bool (*sys_actions_list_f)(uint8_t id, const char* name, void* ctx);

/**
 * @brief The recorder. The app installs it (router_set_tap) next to
 * registering sys_actions_router; recording works only with it installed.
 */
extern const router_tap_t g_sys_actions_tap;

/**
 * @brief Opens the actions namespace (partition CONFIG_SYS_ACTIONS_PARTITION,
 * erased when NVS can't use it). Boot.
 */
MUST_USE err_h sys_actions_init(void);

/**
 * @brief Runs an action now: every frame through router_route, in order,
 * stopping at the first failure (wrapped in ERR_ACTION_STEP). Answers are
 * dropped. Only on a router task (ERR_ACTION_WRONG_TASK otherwise); a RUN in
 * an action nests up to CONFIG_SYS_ACTIONS_MAX_DEPTH.
 */
MUST_USE err_h sys_actions_run(uint8_t id);

/**
 * @brief Queues a RUN of `id` on the internal link (router_inject), for
 * callers that aren't a router task (events, the VM, boot). Never blocks.
 * NULL = queued, not done: the run's errors are reported by the router.
 */
MUST_USE err_h sys_actions_request(uint8_t id, bool urgent);

/**
 * @brief Starts recording the frames of one client (`link`, `peer`) into
 * action `id`, named `name`. Recorded: frames from that client except the
 * sys key and the actions ops other than RUN. With `execute`, frames run as
 * usual and only those that succeed are recorded; without, they are recorded
 * and answered OK without running. One recording or upload at a time
 * (ERR_ACTION_BUSY).
 */
MUST_USE err_h sys_actions_record_start(uint8_t id, const char* name, bool execute, uint8_t link, uint32_t peer);

/**
 * @brief Stores the recording (replacing an action with that ID) and closes
 * it, also on failure. ERR_ACTION_EMPTY with no frame, ERR_ACTION_TOO_BIG when
 * it outgrew CONFIG_SYS_ACTIONS_MAX_BYTES.
 */
MUST_USE err_h sys_actions_record_stop(sys_actions_info_t* out);

/**
 * @brief Upload of a whole image: begin, write in order, commit. Commit checks
 * the length, the CRC and the framing, stores the image (replacing an action
 * with that ID) and closes the upload, also on failure.
 */
MUST_USE err_h sys_actions_upload_begin(uint8_t id, const char* name, uint16_t len);
MUST_USE err_h sys_actions_upload_write(uint16_t offset, const uint8_t* data, uint16_t len);
MUST_USE err_h sys_actions_upload_commit(uint32_t crc32);

/** @brief Drops an open recording or upload (nothing stored). No-op when none is open. */
void sys_actions_abort(void);

MUST_USE err_h sys_actions_info(uint8_t id, sys_actions_info_t* out);

/** @brief Copies `len` bytes of the image from `offset` (ERR_ACTION_READ_RANGE past its end). */
MUST_USE err_h sys_actions_read(uint8_t id, uint16_t offset, uint8_t* out, uint16_t len);

/** @brief Gives a stored action another name; the frames stay. */
MUST_USE err_h sys_actions_rename(uint8_t id, const char* name);

MUST_USE err_h sys_actions_remove(uint8_t id);

/**
 * @brief Calls `fn` for each stored action with ID >= `start`, by ascending
 * ID, until it returns false.
 * @param total Number of stored actions (all IDs); may be NULL.
 */
MUST_USE err_h sys_actions_list(uint8_t start, sys_actions_list_f fn, void* ctx, uint16_t* total);

#ifdef __cplusplus
}
#endif
