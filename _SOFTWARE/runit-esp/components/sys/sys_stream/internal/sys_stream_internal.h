#pragma once

/*
The stream's own router (router/sys_stream_router.c) and nothing else: the streams' state for LIST (the layouts it
sends are in sys_stream_router.h). Not on the component's public include path.
*/

#include "sdkconfig.h"
#include "sys_stream.h"
#include "sys_stream_router.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief A stream, copied out for LIST: its head and its targets, as the router sends them. */
typedef struct {
  sys_stream_head_t head;
  sys_stream_target_t targets[CONFIG_SYS_STREAM_TARGETS];
} sys_stream_info_t;

/** @brief Copies every stream out (LIST); returns how many. */
uint8_t sys_stream_list(sys_stream_info_t out[CONFIG_SYS_STREAM_MAX]);

#ifdef __cplusplus
}
#endif
