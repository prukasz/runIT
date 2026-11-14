#pragma once

#include <stdint.h>

#include "sdkconfig.h"
#include "sys_stream_router.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Between sys_stream.c and sys_stream_router.c only: LIST's copy of the streams.
 * Not on the component's public include path.
 */

typedef struct {
  sys_stream_head_t head;
  sys_stream_target_t targets[CONFIG_SYS_STREAM_TARGETS];
} sys_stream_info_t;

/** @brief Copies every stream and its targets into `out`; returns how many. */
uint8_t sys_stream_list(sys_stream_info_t out[CONFIG_SYS_STREAM_MAX]);

#ifdef __cplusplus
}
#endif
