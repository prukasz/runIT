/* Host stand-in for ESP-IDF's esp_heap_caps.h: the capabilities are ignored on the PC. */
#pragma once

#include <stdlib.h>

#define MALLOC_CAP_DMA      (1u << 3)
#define MALLOC_CAP_INTERNAL (1u << 11)

static inline void *heap_caps_calloc(size_t n, size_t size, unsigned caps)
{
    (void)caps;
    return calloc(n, size);
}

static inline void heap_caps_free(void *p)
{
    free(p);
}
