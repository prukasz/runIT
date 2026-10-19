#pragma once

#include "errors.h"
#include "router.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Records: lists of router commands stored in NVS under a 32-bit ID,
 * replayed through router_route (SYS_ACTIONS.MD). One store for everything the
 * board keeps as commands: the user's actions, the system's settings (log,
 * power) and the default commands of each device.
 *
 * A record is filled by recording a client's live commands (the router tap)
 * or by uploading the whole image, and read back frame by frame, as it was
 * recorded. Nothing replays by itself: the VM or the app calls RUN. A record
 * runs only on a router task: a remote RUN, or router_inject of a RUN frame
 * (sys_actions_request) from anywhere else. So a replay never interleaves with
 * live normal frames, and an urgent STOP still overtakes it.
 *
 * Image: frames back to back, each [u16 len LE][keys + payload] (what
 * router_route takes: no seq / flags), 1..CONFIG_ROUTER_FRAME_MAX - 2 bytes.
 */

/* ----------------------------------------------------------------------------
 * IDs: [class u8][index u24]. The one place they are assigned; never renumber.
 * 0 is "no record". Any client may write any class; the class only keeps the
 * ranges apart.
 * ------------------------------------------------------------------------- */

#define SYS_ACTIONS_ID_NONE 0u

//#ref-defines action_class @alias Action classes @prefix SYS_ACTIONS_CLASS_
#define SYS_ACTIONS_CLASS_USER    0x00u  //@alias Action @description the app's own actions
#define SYS_ACTIONS_CLASS_SYSTEM  0x01u  //@alias Settings @description settings of the system modules
#define SYS_ACTIONS_CLASS_DEVICE  0x02u  //@alias Device @description install and default commands
#define SYS_ACTIONS_CLASS_PROJECT 0x03u  //@alias Project @description stored project run list and sections

#define SYS_ACTIONS_ID(class, n) ((((uint32_t)(class)) << 24) | ((uint32_t)(n) & 0xFFFFFFu))

//#ref-defines settings_record @alias Settings records @prefix SYS_ACTIONS_ID_
#define SYS_ACTIONS_ID_LOG             SYS_ACTIONS_ID(SYS_ACTIONS_CLASS_SYSTEM, 1)  //@alias Log @description Log options SET frame
#define SYS_ACTIONS_ID_POWER_CFG       SYS_ACTIONS_ID(SYS_ACTIONS_CLASS_SYSTEM, 2)  //@alias Power configuration @description Power configuration SET_CONFIG
#define SYS_ACTIONS_ID_POWER_PSU       SYS_ACTIONS_ID(SYS_ACTIONS_CLASS_SYSTEM, 3)  //@alias Power supply @description Power supply SET_PSU
#define SYS_ACTIONS_ID_POWER_BATTERY   SYS_ACTIONS_ID(SYS_ACTIONS_CLASS_SYSTEM, 4)  //@alias Battery @description Battery SET_BATTERY
#define SYS_ACTIONS_ID_POWER_RESPONSES SYS_ACTIONS_ID(SYS_ACTIONS_CLASS_SYSTEM, 5)  //@alias Power responses @description Power responses SET_RESPONSES
#define SYS_ACTIONS_ID_BLE             SYS_ACTIONS_ID(SYS_ACTIONS_CLASS_SYSTEM, 6)  //@alias Bluetooth @description Bluetooth GATT services and APPLY
#define SYS_ACTIONS_ID_CONN            SYS_ACTIONS_ID(SYS_ACTIONS_CLASS_SYSTEM, 7)  //@alias Data streams @description Data streams and bindings

/** @brief The project record: the list of RUN frames sys_project replays (SYS_PROJECT.MD). The other IDs of this class are sys_project's. */
#define SYS_ACTIONS_ID_PROJECT SYS_ACTIONS_ID(SYS_ACTIONS_CLASS_PROJECT, 0)

/** @brief The record of device `dev` (its device ID, u16): install and default commands. */
#define SYS_ACTIONS_ID_DEVICE(dev) SYS_ACTIONS_ID(SYS_ACTIONS_CLASS_DEVICE, dev)

#define ERR_SYS_ACTIONS_MAP(X)                                                                                   \
  X(ERR_ACTION_NOT_FOUND,  ERR_TAG(ERR_MOD_SYS_ACTIONS, 1),  ERR_SEV_MEDIUM, { uint32_t id; })                   \
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
  X(ERR_ACTION_NO_MEM,     ERR_TAG(ERR_MOD_SYS_ACTIONS, 14), ERR_SEV_HIGH,   { uint32_t size; }) \
  X(ERR_ACTION_STEP,       ERR_TAG(ERR_MOD_SYS_ACTIONS, 15), ERR_SEV_NONE,   { uint32_t id; uint16_t step; }) /* context: frame `step` (from 0) failed */ \
  X(ERR_ACTION_BAD_ARG,    ERR_TAG(ERR_MOD_SYS_ACTIONS, 16), ERR_SEV_HIGH,   {}) \
  X(ERR_ACTION_BAD_ID,     ERR_TAG(ERR_MOD_SYS_ACTIONS, 17), ERR_SEV_HIGH,   {}) /* ID 0 (SYS_ACTIONS_ID_NONE) */

ERR_TAGS_DECLARE(ERR_SYS_ACTIONS_MAP)

typedef struct {
  uint16_t bytes;  /**< Image size in bytes */
  uint16_t count;  /**< Frame count */
  uint32_t crc32;  /**< CRC-32 of image */
} sys_actions_info_t;

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
 * @brief Runs a record now: every frame through router_route, in order. Answers are dropped. A failing frame is
 * wrapped in ERR_ACTION_STEP. With `keep_going` false the run stops at the first failure; with true it runs every
 * frame (the way to start a project: one failing device doesn't leave the rest unconfigured), reports each later
 * failure (err_report) and returns the first. Only on a router task (ERR_ACTION_WRONG_TASK otherwise); a RUN in a
 * record nests up to CONFIG_SYS_ACTIONS_MAX_DEPTH.
 */
MUST_USE err_h sys_actions_run(uint32_t id, bool keep_going);

/**
 * @brief Queues a RUN of `id` on the internal link (router_inject), for
 * callers that aren't a router task (events, the VM). Never blocks.
 * NULL = queued, not done: the run's errors are reported by the router.
 */
MUST_USE err_h sys_actions_request(uint32_t id, bool urgent);

/**
 * @brief Starts recording the frames of one client (its `link`) into
 * record `id`. Recorded: frames from that client except the sys key and the
 * actions ops other than RUN. With `execute`, frames run as usual and only
 * those that succeed are recorded; without, they are recorded and answered OK
 * without running. One recording or upload at a time (ERR_ACTION_BUSY).
 */
MUST_USE err_h sys_actions_record_start(uint32_t id, bool execute, uint8_t link);

/**
 * @brief Stores the recording (replacing a record with that ID) and closes
 * it, also on failure. ERR_ACTION_EMPTY with no frame, ERR_ACTION_TOO_BIG when
 * it outgrew CONFIG_SYS_ACTIONS_MAX_BYTES.
 */
MUST_USE err_h sys_actions_record_stop(sys_actions_info_t* out);

/**
 * @brief Upload of a whole image: begin, write in order, commit. Commit checks
 * the length, the CRC and the framing, stores the image (replacing a record
 * with that ID) and closes the upload, also on failure.
 */
MUST_USE err_h sys_actions_upload_begin(uint32_t id, uint16_t len);
MUST_USE err_h sys_actions_upload_write(uint16_t offset, const uint8_t* data, uint16_t len);
MUST_USE err_h sys_actions_upload_commit(uint32_t crc32);

/**
 * @brief Stores a whole image from the firmware (the app's onboard actions), replacing a record with that ID. The
 * framing is checked like an upload's; nothing is written when the same image is already stored. Does not touch an
 * open recording or upload.
 */
MUST_USE err_h sys_actions_set(uint32_t id, const uint8_t* image, uint16_t len);

/** @brief Copies record `from` to `to` (replacing it); the limit of `to`'s class applies. Nothing is written when `to` already holds the same image. */
MUST_USE err_h sys_actions_copy(uint32_t from, uint32_t to);

/** @brief The largest image a record with this ID may have (its class decides). */
uint32_t sys_actions_max_bytes(uint32_t id);

/** @brief Drops an open recording or upload (nothing stored). No-op when none is open. */
void sys_actions_abort(void);

MUST_USE err_h sys_actions_info(uint32_t id, sys_actions_info_t* out);

/** @brief Called once per frame, in order: the frame exactly as recorded (keys + payload), borrowed. */
typedef err_h (*sys_actions_frame_f)(const uint8_t* frame, uint16_t len, bool last, void* ctx);

/**
 * @brief Reads a record back frame by frame, as it was recorded or uploaded: `fn` for every frame, in order, until
 * one fails (that error is returned). ERR_ACTION_NOT_FOUND, ERR_ACTION_BAD_FRAME for a stored image that is damaged.
 */
MUST_USE err_h sys_actions_dump(uint32_t id, sys_actions_frame_f fn, void* ctx);

MUST_USE err_h sys_actions_remove(uint32_t id);

/**
 * @brief Removes every stored record, of every class (settings, devices, project, user). ERR_ACTION_BUSY while a
 * recording or upload is open. Stops at the first failing removal and returns its error; the records after it
 * stay.
 */
MUST_USE err_h sys_actions_remove_all(void);

/**
 * @brief The stored IDs from `start` up, ascending, at most `cap` of them.
 * @param count Set to the number written.
 * @param total Number of stored records (all IDs); may be NULL.
 */
MUST_USE err_h sys_actions_list(uint32_t start, uint32_t* ids, uint16_t cap, uint16_t* count, uint16_t* total);

#ifdef __cplusplus
}
#endif
