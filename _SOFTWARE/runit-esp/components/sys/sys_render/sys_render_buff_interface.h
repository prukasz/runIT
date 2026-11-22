/**
 * @file
 * @brief Memory the platform gives: one contract, provided by the board (on ESP: heap_caps). For
 *        now here; it is meant to move to a board header next to board_time.h.
 */
#pragma once
#include <stdint.h>

/**
 * @brief What a block must be, OR-ed (board_mem_alloc caps). 0 is any memory, 8-byte aligned.
 * The properties are independent: a block can be DMA-capable in PSRAM or in internal RAM. A board
 * that cannot give the combination (INTERNAL and SPIRAM together, SPIRAM without PSRAM) gives
 * NULL, it never gives a block that is something else.
 */
#define BOARD_MEM_INTERNAL (1u << 0) /**< In the chip's internal RAM */
#define BOARD_MEM_SPIRAM (1u << 1)   /**< In the external PSRAM */
#define BOARD_MEM_DMA (1u << 2)      /**< DMA-capable: aligned (also its size) for the bus */

/**
 * @brief Allocates a block.
 * @param[in] bytes Size of the block
 * @param[in] caps BOARD_MEM_* OR-ed, what the block must be
 * @return The block, or NULL when the board has no such memory (or none left)
 */
void *board_mem_alloc(uint32_t bytes, uint32_t caps);

/**
 * @brief Frees a block of board_mem_alloc.
 * @param[in] ptr The block; NULL does nothing
 */
void board_mem_free(void *ptr);

/**
 * @brief The largest block board_mem_alloc could give now.
 * @param[in] caps BOARD_MEM_* OR-ed, what the block must be
 * @return Bytes of the largest free block of such memory, 0 when the board has none
 */
uint32_t board_mem_largest_free(uint32_t caps);
