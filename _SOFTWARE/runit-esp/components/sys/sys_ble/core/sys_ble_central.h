#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "errors.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file sys_ble_central.h
 * @brief This board as a BLE central (SYS_BLE.MD "Central"): it finds other runIT boards and keeps a link to each one
 * it was told to, as a framed provider (SYS_DATA_PROVIDER.MD) over that board's runIT pair. Nothing runs and nothing
 * is allocated until a scan or a connect: the app turns it on when the project has a board on BLE.
 */

#define SYS_BLE_CENTRAL_NAME_MAX 16u  // a found board's name, NUL included (a longer one is cut)

/** A runIT board seen in a scan. */
typedef struct {
  uint8_t addr_type;                     /**< BLE_ADDR_* of NimBLE: 0 public, 1 random */
  uint8_t addr[6];                       /**< Its address, least significant byte first */
  int8_t rssi;                           /**< dBm, the last advertisement's */
  char name[SYS_BLE_CENTRAL_NAME_MAX];  /**< Its advertised name, NUL-padded */
} sys_ble_found_t;

/**
 * @brief Scans for runIT boards (their advertisement carries the runIT service) for `duration_ms`, the list of found
 * boards cleared first. Returns at once; the scan ends by itself.
 * @param duration_ms  How long to scan.
 * @return NULL when started; ERR_BLE_NOT_STARTED before the stack runs, ERR_BLE_SCAN when NimBLE refuses (a connect
 * being made).
 */
MUST_USE err_h sys_ble_central_scan(uint16_t duration_ms);

/**
 * @brief The boards the last scan found.
 * @param out       Room for `max`.
 * @param max       How many fit.
 * @param scanning  Set to whether the scan still runs.
 * @return How many were written.
 */
uint8_t sys_ble_central_found(sys_ble_found_t* out, uint8_t max, bool* scanning);

/**
 * @brief Makes provider `provider` a link to the board at `addr`: framed, up while connected to it and subscribed to
 * its runIT TX. The link connects as soon as the stack runs and again after every loss, until the provider is removed
 * (PROVIDER REMOVE). The session attached to it as SLAVE sends its hello when it comes up (SYS_SESSION.MD).
 * @param provider   Its id.
 * @param addr_type  0 public, 1 random.
 * @param addr       The board's address, least significant byte first.
 * @return NULL when made; ERR_BLE_NO_MEM (every CONFIG_SYS_BLE_CENTRAL_LINKS link taken, or no heap), the registry's
 * ERR_PROV_EXISTS / ERR_PROV_FULL.
 */
MUST_USE err_h sys_ble_central_connect(uint8_t provider, uint8_t addr_type, const uint8_t addr[6]);

#ifdef __cplusplus
}
#endif
