#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "errors.h"
#include "sys_ota_errors.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief A new firmware into the other app slot (SYS_OTA.MD): begin, write in order, commit, then a restart boots it. It
 * stays only when something confirms it after the boot: else the next restart goes back to the old one. Nothing here
 * runs while the guard says the board is busy (a flash write stalls what runs from flash).
 */

//#ref-enum @alias Firmware state
typedef enum {
  SYS_OTA_STATE_UNKNOWN = 0,  //@alias Flashed @description Flashed by cable: nothing to confirm
  SYS_OTA_STATE_VALID = 1,    //@alias Confirmed @description Written over the air and confirmed
  SYS_OTA_STATE_PENDING = 2,  //@alias Not confirmed @description The first boot of a new firmware: the next restart goes back unless it is confirmed
} sys_ota_state_e;

/** @brief What the boards runs now, and the write in progress. */
typedef struct {
  uint8_t slot;        /**< The app slot it runs from: 0 or 1 */
  uint8_t state;       /**< sys_ota_state_e of that firmware */
  bool open;           /**< A write is in progress */
  uint32_t written;    /**< Its bytes so far */
  uint32_t slot_bytes; /**< The largest image the other slot takes */
} sys_ota_info_t;

/** @brief Whether the board is too busy for an update (the VM runs). */
typedef bool (*sys_ota_busy_f)(void);

/** @brief Sets what BEGIN asks (NULL: never busy). Boot. */
void sys_ota_set_guard(sys_ota_busy_f busy);

/**
 * @brief Starts a write of `size` bytes into the other slot; a write left open is dropped.
 * @return NULL when open; ERR_OTA_BUSY, ERR_OTA_TOO_BIG, or the flash's error (a firmware still to be confirmed takes no
 * update).
 */
MUST_USE err_h sys_ota_begin(uint32_t size);

/** @brief The next bytes of the image. ERR_OTA_NOT_OPEN, ERR_OTA_OFFSET unless `offset` is the bytes so far, ERR_OTA_TOO_BIG. */
MUST_USE err_h sys_ota_write(uint32_t offset, const uint8_t* data, uint32_t len);

/**
 * @brief Ends the write: checks the image (ERR_OTA_INCOMPLETE, ERR_OTA_BAD_IMAGE) and makes it what the next boot starts.
 * The write is closed, also on failure. The running firmware is not touched.
 */
MUST_USE err_h sys_ota_commit(void);

/** @brief Drops the write in progress. No-op when none is open. */
void sys_ota_abort(void);

/** @brief Keeps the running firmware: no restart goes back any more. No-op unless it is to be confirmed. */
MUST_USE err_h sys_ota_confirm(void);

/** @brief What runs now and the write in progress. */
sys_ota_info_t sys_ota_info(void);

#ifdef __cplusplus
}
#endif
