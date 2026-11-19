#pragma once

/**
 * @file board_id.h
 * @brief The board's identity (BOARD_ID.MD): its id (its place in the project, the header's src: CMD_SRC_*), kept in
 * the store and used from the start after it was set; its boot id (random per boot); its firmware's build. What is this
 * chip's (the random number, the firmware's description) is esp/.
 * @usage ERR_TRY(board_id_init());
 *        hdr.src = board_id();
 */

#include <stdint.h>

#include "errors.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Reads the board's id from the store (CONFIG_BOARD_ID_DEFAULT when none is kept) and draws the boot id. Boot,
 * after nvs_store_init of the default partition, before anything reads them.
 * @return NULL; the store's error (the id is the default then)
 */
MUST_USE err_h board_id_init(void);

/** @brief The board's id: CMD_SRC_NONE (none given) or CMD_SRC_BOARD_FIRST..CMD_SRC_BOARD_LAST. */
uint8_t board_id(void);

/**
 * @brief Keeps the board's id in the store: it is the board's from the next start.
 * @param[in] id CMD_SRC_NONE, or CMD_SRC_BOARD_FIRST..CMD_SRC_BOARD_LAST
 * @return NULL; ERR_CMD_BAD_ARG, the store's error
 */
MUST_USE err_h board_id_save(uint8_t id);

/** @brief Random per boot: a peer that sees a new one knows the board restarted. */
uint32_t board_boot_id(void);

/** @brief The firmware's build: the first 4 bytes of its ELF's SHA-256 (whether two boards run the same build). */
uint32_t board_build_id(void);

#ifdef __cplusplus
}
#endif
