/* Host stand-in for ESP-IDF's esp_memory_utils.h: on the PC every buffer is "DMA-capable". */
#pragma once

#include <stdbool.h>
#include <stddef.h>

static inline bool esp_ptr_dma_capable(const void *p)
{
    return p != NULL;
}
