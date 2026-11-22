/**
 * @file
 * @brief The memory contract (sys_render_buff_interface.h) on ESP-IDF's heap_caps. Not used yet,
 *        not tested in TDD.
 *
 * BOARD_MEM_SPIRAM | BOARD_MEM_DMA: heap_caps marks no PSRAM region DMA-capable, but the ESP32-S3's
 * GDMA reads PSRAM through the cache, so such a block is taken from SPIRAM aligned (also its size)
 * to the cache line.
 */
#include <stdbool.h>

#include "esp_heap_caps.h"

#include "sys_render/sys_render_buff_interface.h"

/** @brief Alignment of a block for DMA: the PSRAM cache line (covers internal RAM's 4) */
#define DMA_ALIGN 64u
/** @brief Alignment of any other block */
#define ANY_ALIGN 8u

/** @brief heap_caps flags for BOARD_MEM_* caps; 0 when the board cannot give the combination */
static uint32_t to_heap_caps(uint32_t caps)
{
    const bool internal = (caps & BOARD_MEM_INTERNAL) != 0;
    const bool spiram = (caps & BOARD_MEM_SPIRAM) != 0;
    const bool dma = (caps & BOARD_MEM_DMA) != 0;
    if (internal && spiram) {
        return 0;
    }
    uint32_t heap = MALLOC_CAP_8BIT;
    if (spiram) {
        heap |= MALLOC_CAP_SPIRAM; /* DMA on PSRAM: the alignment, see @file */
    } else if (internal) {
        heap |= MALLOC_CAP_INTERNAL;
    }
    if (dma && !spiram) {
        heap |= MALLOC_CAP_DMA;
    }
    return heap;
}

void *board_mem_alloc(uint32_t bytes, uint32_t caps)
{
    const uint32_t heap = to_heap_caps(caps);
    if (heap == 0 || bytes == 0) {
        return NULL;
    }
    if ((caps & BOARD_MEM_DMA) != 0) {
        const uint32_t size = (bytes + DMA_ALIGN - 1u) & ~(DMA_ALIGN - 1u);
        return heap_caps_aligned_alloc(DMA_ALIGN, size, heap);
    }
    return heap_caps_aligned_alloc(ANY_ALIGN, bytes, heap);
}

void board_mem_free(void *ptr)
{
    heap_caps_free(ptr); /* NULL does nothing */
}

uint32_t board_mem_largest_free(uint32_t caps)
{
    const uint32_t heap = to_heap_caps(caps);
    return heap == 0 ? 0u : (uint32_t)heap_caps_get_largest_free_block(heap);
}
