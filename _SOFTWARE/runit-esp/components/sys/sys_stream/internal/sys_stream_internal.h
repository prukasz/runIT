#pragma once

/*
The stream's own router (router/sys_stream_router.c) and nothing else: the streams' state for LIST. Not on the
component's public include path.
*/

#include "sdkconfig.h"
#include "sys_stream.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief A target as LIST sends it. */
typedef struct __attribute__((packed)) {
  uint8_t type;                     /**< sys_stream_target_e */
  uint8_t peer;                     /**< PEER: its id; else 0 */
  sys_data_provider_id_t provider;  /**< PROVIDER: the binding; else zero */
  uint16_t dropped;                 /**< Messages not given to it: busy, down, too long, no route; stops at 0xFFFF */
} sys_stream_target_t;

/** @brief A stream's head as LIST sends it: what it is, its state, how many targets follow. */
typedef struct __attribute__((packed)) {
  sys_stream_cfg_t cfg;
  uint8_t state;  /**< SYS_STREAM_STATE_* */
  uint8_t targets;
} sys_stream_head_t;

/** @brief A stream, copied out for LIST. */
typedef struct {
  sys_stream_head_t head;
  sys_stream_target_t targets[CONFIG_SYS_STREAM_TARGETS];
} sys_stream_info_t;

/** @brief Copies every stream out (LIST); returns how many. */
uint8_t sys_stream_list(sys_stream_info_t out[CONFIG_SYS_STREAM_MAX]);

#ifdef __cplusplus
}
#endif
