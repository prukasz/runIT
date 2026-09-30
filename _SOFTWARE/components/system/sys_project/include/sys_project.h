#pragma once
/**
 * @file sys_project.h
 * @brief Stored code: the frame list that configures the board from its
 * defaults, kept in the `project` NVS partition and replayed at boot.
 *
 * The app builds the complete list and writes it as data (begin / write /
 * commit); nothing is executed while storing. At boot the board replays the
 * committed list through sys_interface_decode(), after its own init. Separate
 * from sys_actions (runtime recordings). Design: SYS_PROJECT.MD,
 * app/docs/03_records_as_storage.md.
 *
 * Frame list: `[u16 len LE][class][packet][payload] ...`, no seq byte (the same
 * record format sys_actions uses).
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "sys_error.h"

/** Decides whether a stored frame may be part of the code (commit refuses the list otherwise). */
typedef bool (*sys_project_frame_filter_f)(const uint8_t* frame, size_t len);

//#ref-enum @alias Replay State
typedef enum sys_project_replay_e {
  SYS_PROJECT_REPLAY_NONE = 0,     //@alias Not run @description No stored code at boot, or the replay hasn't run yet.
  SYS_PROJECT_REPLAY_DONE = 1,     //@alias Replayed @description Every frame was decoded; `failed` counts the refused ones.
  SYS_PROJECT_REPLAY_SKIPPED = 2,  //@alias Skipped @description Built for another firmware (schema ID differs); nothing applied.
  SYS_PROJECT_REPLAY_CORRUPT = 3,  //@alias Corrupt @description The stored list failed its CRC or framing check; nothing applied.
} sys_project_replay_e;

/** Result of the last boot replay (kept in RAM: no client is connected at boot). */
typedef struct sys_project_report_t {
  sys_project_replay_e state;
  uint16_t applied;
  uint16_t failed;
  uint16_t first_failed;  // frame index, UINT16_MAX when none failed
  uint16_t first_tag;     // error tag of the first refused frame
  uint16_t first_owner;
  bool vm_failed;         // a VM frame was refused: autostart is skipped
  uint32_t crc32;         // CRC-32 of the code this boot replayed (0: none); differs from the stored one after a store without restart
} sys_project_report_t;

typedef struct sys_project_info_t {
  bool stored;
  uint32_t length;       // bytes of the frame list
  uint16_t frame_count;
  uint32_t crc32;
  uint32_t schema_id;    // SE_schema_id() the code was built for
  uint32_t capacity;     // most bytes a stored list may hold
  bool autostart;
  sys_project_report_t report;
} sys_project_info_t;

/** Open the `project` NVS partition (erased and re-created if unreadable). Boot step, after sys_settings_init(). */
SE_MUST_USE err_h sys_project_init(void);

/** Start storing a new list of `length` bytes into the inactive generation; drops a store in progress. */
SE_MUST_USE err_h sys_project_begin(uint32_t length);

/** Append bytes at `offset`; chunks must arrive in order (offset = bytes received so far). */
SE_MUST_USE err_h sys_project_write(uint32_t offset, const uint8_t* data, size_t len);

/**
 * Check the new list (all bytes received, CRC, framing, `filter` on every
 * frame, `schema_id` equal to the firmware's) and make it the stored code.
 * On any failure the previous code stays.
 */
SE_MUST_USE err_h sys_project_commit(uint32_t crc32, uint32_t schema_id, sys_project_frame_filter_f filter);

/** Drop a store in progress. */
SE_MUST_USE err_h sys_project_abort(void);

/** Remove the stored code: the next boot starts from the board defaults. */
SE_MUST_USE err_h sys_project_erase(void);

/** Store the project options (sys_settings key `prj_opts`). */
SE_MUST_USE err_h sys_project_set_options(bool autostart);

SE_MUST_USE err_h sys_project_get_info(sys_project_info_t* out);

/** Copy `len` bytes of the stored list from `offset` (recovery). */
SE_MUST_USE err_h sys_project_read(uint32_t offset, uint8_t* out, size_t len);

/**
 * Replay the stored code (boot): every frame through sys_interface_decode()
 * with RX suspended; a refused frame is reported and the replay continues.
 * `is_vm_frame` marks the frames whose refusal cancels autostart.
 */
SE_MUST_USE err_h sys_project_replay(bool (*is_vm_frame)(const uint8_t* frame, size_t len));

/** The last replay's report and whether autostart is set. */
void sys_project_report(sys_project_report_t* out, bool* out_autostart);

/** Restart the board shortly after the current command is answered (the restart replays the code). */
SE_MUST_USE err_h sys_project_load(void);
