#pragma once

#include <stdint.h>

#include "errors.h"
#include "sys_flasher_errors.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file sys_flasher.h
 * @brief The board programs the ESP32-S3 on its link (SYS_FLASHER.MD): it lends the link's UART, puts that board in
 * its ROM bootloader with the BOOT and EN wires, writes it its own firmware (bootloader, partition table, the running
 * app into ota_0) with otadata and nvs erased, checks each region's MD5, restarts it and takes the link back, where
 * the new board says hello with id 0. A job on a task of its own: START answers at once, STATUS follows it.
 * @usage ERR_TRY(sys_flasher_init(RUNIT_BOARD_PROVIDER_UART_LINK));
 *        ERR_TRY(sys_flasher_start());
 */

//#ref-enum @alias Flasher state
typedef enum {
  SYS_FLASHER_IDLE = 0,     //@alias Idle @description Nothing was flashed since the start
  SYS_FLASHER_RUNNING = 1,  //@alias Running @description The attached board is being flashed
  SYS_FLASHER_DONE = 2,     //@alias Done @description Flashed, checked and restarted
  SYS_FLASHER_FAILED = 3,   //@alias Failed @description Stopped by an error or ABORT: the error says which
} sys_flasher_state_e;

//#ref-enum @alias Flasher step
typedef enum {
  SYS_FLASHER_STEP_CONNECT = 0,     //@alias Connect @description The ROM bootloader: BOOT, EN, SYNC, the rate, the flash
  SYS_FLASHER_STEP_BOOTLOADER = 1,  //@alias Bootloader @description The bootloader and the partition table
  SYS_FLASHER_STEP_ERASE = 2,       //@alias Erase @description nvs and otadata
  SYS_FLASHER_STEP_APP = 3,         //@alias Firmware @description The app into ota_0
  SYS_FLASHER_STEP_RESTART = 4,     //@alias Restart @description The board restarts into it; the link is taken back
} sys_flasher_step_e;

/** @brief Where a flashing is. */
typedef struct {
  uint8_t state;   /**< sys_flasher_state_e */
  uint8_t step;    /**< sys_flasher_step_e */
  uint32_t done;   /**< Bytes written and checked */
  uint32_t total;  /**< Bytes to write */
  uint32_t error;  /**< FAILED: the tag of its error; else 0 */
} sys_flasher_status_t;

/**
 * @brief Readies the flasher: the BOOT and EN wires (CONFIG_SYS_FLASHER_BOOT_PIN, _EN_PIN) left floating. Boot step,
 * after sys_uart_init().
 * @param link_provider  The link's provider id (sys_uart_init()).
 * @return NULL; the GPIO's error.
 */
MUST_USE err_h sys_flasher_init(uint8_t link_provider);

/**
 * @brief Starts flashing the board on the link, on a task of its own; sys_flasher_status() follows it. Any task.
 * @return NULL when started; ERR_FLASHER_BUSY, ERR_FLASHER_BOARD_ID (this board has id 0, the new board's), ERR_FLASHER_IMAGE.
 */
MUST_USE err_h sys_flasher_start(void);

/** @brief Stops a flashing between two of its packets: it ends FAILED with ERR_FLASHER_ABORTED. */
void sys_flasher_abort(void);

/** @brief Where the last flashing is. Any task. */
sys_flasher_status_t sys_flasher_status(void);

#ifdef __cplusplus
}
#endif
