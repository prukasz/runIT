#pragma once
#include <stdint.h>
#include <stdio.h>

// Error owners and tags for sys_project. sys_errors aggregates this map through
// its include dirs (sys_error_codes.h). This folder holds only error maps.
#define SYS_PROJECT_OWNER_MAP(X)                                       \
  X(OWNER_SYS_PROJECT_BASE, 0xB200, "OWNER_SYS_PROJECT_BASE")          \
  X(OWNER_SYS_PROJECT_INIT, 0xB201, "OWNER_SYS_PROJECT_INIT")          \
  X(OWNER_SYS_PROJECT_STORE, 0xB202, "OWNER_SYS_PROJECT_STORE")        \
  X(OWNER_SYS_PROJECT_COMMIT, 0xB203, "OWNER_SYS_PROJECT_COMMIT")      \
  X(OWNER_SYS_PROJECT_READ, 0xB204, "OWNER_SYS_PROJECT_READ")          \
  X(OWNER_SYS_PROJECT_REPLAY, 0xB205, "OWNER_SYS_PROJECT_REPLAY")

#define SYS_ERROR_PROJECT_MAP(X)                                                                                              \
  X(ERR_PROJECT_TOO_BIG, 0xB201, SE_LEVEL_MEDIUM, struct { uint32_t length; uint32_t capacity; })                             \
  X(ERR_PROJECT_NOT_STORING, 0xB202, SE_LEVEL_LOW, struct { uint8_t unused; })                                               \
  X(ERR_PROJECT_OFFSET, 0xB203, SE_LEVEL_LOW, struct { uint32_t offset; uint32_t expected; })                                 \
  X(ERR_PROJECT_INCOMPLETE, 0xB204, SE_LEVEL_LOW, struct { uint32_t received; uint32_t length; })                             \
  X(ERR_PROJECT_CRC, 0xB205, SE_LEVEL_MEDIUM, struct { uint32_t crc; uint32_t expected; })                                    \
  X(ERR_PROJECT_BAD_FRAME, 0xB206, SE_LEVEL_MEDIUM, struct { uint16_t index; uint32_t offset; uint16_t len; })                \
  X(ERR_PROJECT_FRAME_REFUSED, 0xB207, SE_LEVEL_LOW, struct { uint16_t index; uint8_t class_header; /*@id rx-class*/ uint8_t packet_header; /*@id rx-packet class_header*/ }) \
  X(ERR_PROJECT_SCHEMA, 0xB208, SE_LEVEL_MEDIUM, struct { uint32_t stored; uint32_t firmware; })                              \
  X(ERR_PROJECT_NOT_STORED, 0xB209, SE_LEVEL_LOW, struct { uint8_t unused; })                                                \
  X(ERR_PROJECT_READ_RANGE, 0xB20A, SE_LEVEL_LOW, struct { uint32_t offset; uint32_t len; uint32_t length; })

#define SYS_ERROR_PROJECT_LOGGER_MAP(X) \
  X(ERR_PROJECT_TOO_BIG)                \
  X(ERR_PROJECT_NOT_STORING)            \
  X(ERR_PROJECT_OFFSET)                 \
  X(ERR_PROJECT_INCOMPLETE)             \
  X(ERR_PROJECT_CRC)                    \
  X(ERR_PROJECT_BAD_FRAME)              \
  X(ERR_PROJECT_FRAME_REFUSED)          \
  X(ERR_PROJECT_SCHEMA)                 \
  X(ERR_PROJECT_NOT_STORED)             \
  X(ERR_PROJECT_READ_RANGE)

#define LOG_BODY_ERR_PROJECT_TOO_BIG(p, out, out_size) snprintf((out), (out_size), "stored code of %lu bytes doesn't fit %lu", (unsigned long)(p)->length, (unsigned long)(p)->capacity)
#define LOG_BODY_ERR_PROJECT_NOT_STORING(p, out, out_size) snprintf((out), (out_size), "no store in progress (begin first)")
#define LOG_BODY_ERR_PROJECT_OFFSET(p, out, out_size) snprintf((out), (out_size), "stored code chunk at %lu, expected %lu (chunks go in order)", (unsigned long)(p)->offset, (unsigned long)(p)->expected)
#define LOG_BODY_ERR_PROJECT_INCOMPLETE(p, out, out_size) snprintf((out), (out_size), "stored code has %lu of %lu bytes", (unsigned long)(p)->received, (unsigned long)(p)->length)
#define LOG_BODY_ERR_PROJECT_CRC(p, out, out_size) snprintf((out), (out_size), "stored code CRC 0x%08lX, expected 0x%08lX", (unsigned long)(p)->crc, (unsigned long)(p)->expected)
#define LOG_BODY_ERR_PROJECT_BAD_FRAME(p, out, out_size) snprintf((out), (out_size), "stored frame %u at byte %lu has length %u", (p)->index, (unsigned long)(p)->offset, (p)->len)
#define LOG_BODY_ERR_PROJECT_FRAME_REFUSED(p, out, out_size) snprintf((out), (out_size), "stored frame %u (class 0x%02X packet 0x%02X) can't be part of stored code", (p)->index, (p)->class_header, (p)->packet_header)
#define LOG_BODY_ERR_PROJECT_SCHEMA(p, out, out_size) snprintf((out), (out_size), "stored code is for schema 0x%08lX, firmware is 0x%08lX", (unsigned long)(p)->stored, (unsigned long)(p)->firmware)
#define LOG_BODY_ERR_PROJECT_NOT_STORED(p, out, out_size) snprintf((out), (out_size), "no stored code")
#define LOG_BODY_ERR_PROJECT_READ_RANGE(p, out, out_size) snprintf((out), (out_size), "read %lu bytes at %lu, stored code has %lu", (unsigned long)(p)->len, (unsigned long)(p)->offset, (unsigned long)(p)->length)
