#pragma once
#include <stdint.h>
#include <stdio.h>

#define SYS_BUFFERS_OWNER_MAP(X) \
  X(OWNER_SYS_BUFFERS_BASE, 0xA700, "OWNER_SYS_BUFFERS_BASE") \
  X(OWNER_SYS_BUFF_INIT, 0xA701, "OWNER_SYS_BUFF_INIT") \
  X(OWNER_SYS_BUFF_FREE, 0xA702, "OWNER_SYS_BUFF_FREE") \
  X(OWNER_SYS_BUFF_PUSH, 0xA703, "OWNER_SYS_BUFF_PUSH") \
  X(OWNER_SYS_BUFF_POP_FRAMED, 0xA704, "OWNER_SYS_BUFF_POP_FRAMED") \
  X(OWNER_SYS_BUFF_POP_RAW, 0xA705, "OWNER_SYS_BUFF_POP_RAW")

#define SYS_ERROR_BUFFERS_MAP(X) \
  X(ERR_BUFFERS_ITEM_TOO_LONG, 0xA701, SE_LEVEL_MEDIUM, struct { uint32_t len; uint32_t max; })

/** @brief Human-readable descriptions for the sys_buffers tags - see SE_describe_payload() in sys_error.h. */
#define SYS_ERROR_BUFFERS_LOGGER_MAP(X) \
  X(ERR_BUFFERS_ITEM_TOO_LONG)

#define LOG_BODY_ERR_BUFFERS_ITEM_TOO_LONG(p, out, out_size) \
  snprintf((out), (out_size), "item of %lu bytes dropped: destination holds %lu", (unsigned long)(p)->len, (unsigned long)(p)->max)
