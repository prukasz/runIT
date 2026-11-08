#pragma once

#include "errors.h"
#include "router.h"
#include "sys_actions_errors.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Records: lists of router commands stored in NVS under a 32-bit ID,
 * replayed through router_route (SYS_ACTIONS.MD). One store for everything the
 * board keeps as commands: the user's actions, the system's settings (log,
 * power) and the default commands of each device.
 *
 * A record is written whole by its client (begin, write, commit) and read back by offset, the image as it
 * was stored. Nothing replays by itself: the VM or the app calls RUN. A record
 * runs only on a router task: a remote RUN, or router_inject of a RUN frame
 * (sys_actions_request) from anywhere else. So a replay never interleaves with
 * live normal frames, and an urgent STOP still overtakes it.
 *
 * Image: frames back to back, each [u16 len LE][keys + payload] (what
 * router_route takes: no seq / flags), 1..CMD_DATA_MAX bytes.
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
#define SYS_ACTIONS_ID_LOG             SYS_ACTIONS_ID(SYS_ACTIONS_CLASS_SYSTEM, 1)  //@alias Log @description Log options: the CONFIG write
#define SYS_ACTIONS_ID_POWER_CFG       SYS_ACTIONS_ID(SYS_ACTIONS_CLASS_SYSTEM, 2)  //@alias Power configuration @description Power configuration: the CONFIG write
#define SYS_ACTIONS_ID_POWER_PSU       SYS_ACTIONS_ID(SYS_ACTIONS_CLASS_SYSTEM, 3)  //@alias Power supply @description Power supply: the PSU write
#define SYS_ACTIONS_ID_POWER_BATTERY   SYS_ACTIONS_ID(SYS_ACTIONS_CLASS_SYSTEM, 4)  //@alias Battery @description Battery: the BATTERY write
#define SYS_ACTIONS_ID_POWER_RESPONSES SYS_ACTIONS_ID(SYS_ACTIONS_CLASS_SYSTEM, 5)  //@alias Power responses @description Power responses: the MATRIX write
#define SYS_ACTIONS_ID_BLE             SYS_ACTIONS_ID(SYS_ACTIONS_CLASS_SYSTEM, 6)  //@alias Bluetooth @description Bluetooth GATT services and APPLY
#define SYS_ACTIONS_ID_STREAM          SYS_ACTIONS_ID(SYS_ACTIONS_CLASS_SYSTEM, 7)  //@alias Data streams @description Data streams and bindings
#define SYS_ACTIONS_ID_POWER_PD        SYS_ACTIONS_ID(SYS_ACTIONS_CLASS_SYSTEM, 8)  //@alias USB-PD request @description USB-PD request: the PD write
#define SYS_ACTIONS_ID_LINKS           SYS_ACTIONS_ID(SYS_ACTIONS_CLASS_SYSTEM, 9)  //@alias Links to boards @description The BLE links to other boards: BLE CONNECT and SESSION ATTACH per board

/**
 * @brief The firmware's own actions: user-class records sys_actions_init() writes at every boot (SYS_ACTIONS.MD "Onboard
 * actions"), replacing what is stored under these IDs. An app stores its own actions under other IDs.
 */
//#ref-defines onboard_action @alias Onboard actions @prefix SYS_ACTIONS_ONBOARD_
#define SYS_ACTIONS_ONBOARD_RESUME     SYS_ACTIONS_ID(SYS_ACTIONS_CLASS_USER, 3)  //@alias Resume all @description Resume every device
#define SYS_ACTIONS_ONBOARD_SUSPEND    SYS_ACTIONS_ID(SYS_ACTIONS_CLASS_USER, 4)  //@alias Suspend all @description Suspend every device (outputs off)
#define SYS_ACTIONS_ONBOARD_RESET      SYS_ACTIONS_ID(SYS_ACTIONS_CLASS_USER, 5)  //@alias Reset all @description Reset every device (newest first)
#define SYS_ACTIONS_ONBOARD_HARD_RESET SYS_ACTIONS_ID(SYS_ACTIONS_CLASS_USER, 6)  //@alias Hard reset @description Restart the board

/** @brief The project record: the list of RUN frames sys_project replays (SYS_PROJECT.MD). The other IDs of this class are sys_project's. */
#define SYS_ACTIONS_ID_PROJECT SYS_ACTIONS_ID(SYS_ACTIONS_CLASS_PROJECT, 0)

/** @brief The record of device `dev` (its device ID, u16): install and default commands. */
#define SYS_ACTIONS_ID_DEVICE(dev) SYS_ACTIONS_ID(SYS_ACTIONS_CLASS_DEVICE, dev)

typedef struct {
  uint16_t bytes;  /**< Image size in bytes */
  uint16_t count;  /**< Frame count */
  uint32_t crc32;  /**< CRC-32 of image */
} sys_actions_info_t;

/**
 * @brief Opens the actions namespace (partition CONFIG_SYS_ACTIONS_PARTITION,
 * initialised by the boot step "nvs"). Boot.
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
 * @brief Queues a RUN of `id` as the board's own command (router_inject), for
 * callers that aren't a router task (events, the VM). Never blocks.
 * NULL = queued, not done: the run's errors are reported by the router.
 */
MUST_USE err_h sys_actions_request(uint32_t id, bool urgent);

/**
 * @brief Writes a whole image: begin, write in order, commit. Commit checks the length, the CRC and the framing, stores
 * the image (replacing a record with that ID) and closes the draft, also on failure. One at a time (ERR_ACTION_BUSY);
 * the image is on the heap only while it is written.
 */
MUST_USE err_h sys_actions_record_begin(uint32_t id, uint16_t len);
MUST_USE err_h sys_actions_record_write(uint16_t offset, const uint8_t* data, uint16_t len);
MUST_USE err_h sys_actions_record_commit(uint32_t crc32);

/**
 * @brief Stores a whole image from the firmware (the app's onboard actions), replacing a record with that ID. The
 * framing is checked like a commit's; nothing is written when the same image is already stored. Does not touch an
 * open draft.
 */
MUST_USE err_h sys_actions_set(uint32_t id, const uint8_t* image, uint16_t len);

/** @brief Copies record `from` to `to` (replacing it); CONFIG_SYS_ACTIONS_MAX_BYTES applies. Nothing is written when `to` already holds the same image. */
MUST_USE err_h sys_actions_copy(uint32_t from, uint32_t to);

/** @brief Drops the record being written (nothing stored). No-op when none is open. */
void sys_actions_abort(void);

MUST_USE err_h sys_actions_info(uint32_t id, sys_actions_info_t* out);

/**
 * @brief Copies the stored image from byte `offset` into `buf`: as many bytes as are left, at most `cap` (`*len`; 0 at
 * the end). The mirror of an upload's writes; the image is what INFO's CRC covers.
 * @return ERR_ACTION_NOT_FOUND, ERR_ACTION_OFFSET for an offset past the image.
 */
MUST_USE err_h sys_actions_read(uint32_t id, uint16_t offset, uint8_t* buf, uint32_t cap, uint32_t* len);

/** @brief Called once per frame, in order: the frame exactly as recorded (keys + payload), borrowed. */
typedef err_h (*sys_actions_frame_f)(const uint8_t* frame, uint16_t len, bool last, void* ctx);

/**
 * @brief Walks a record frame by frame, as it was recorded or uploaded: `fn` for every frame, in order, until
 * one fails (that error is returned). ERR_ACTION_NOT_FOUND, ERR_ACTION_BAD_FRAME for a stored image that is damaged.
 */
MUST_USE err_h sys_actions_dump(uint32_t id, sys_actions_frame_f fn, void* ctx);

MUST_USE err_h sys_actions_remove(uint32_t id);

/**
 * @brief Removes every stored record, of every class (settings, devices, project, user). ERR_ACTION_BUSY while a
 * record is being written. Stops at the first failing removal and returns its error; the records after it
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
