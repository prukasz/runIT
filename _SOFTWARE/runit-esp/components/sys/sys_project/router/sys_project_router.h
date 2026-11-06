#pragma once

#include "router.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Ops under ROUTER_KEY_PROJECT (SYS_PROJECT.MD "Remote"), each above the frame it uses (packed, little-endian;
 * contract_project.json names them), with its sides. Never renumber; 0x09..0x0F are unused: never assign them. */

#define SYS_PROJECT_OP_BEGIN 0x01u  // W: sys_project_begin_args_t
typedef struct __attribute__((packed)) {
  uint32_t len;  /**< Total stream length in bytes */
  uint8_t boot;  /**< sys_project_boot_e, stored and applied at COMMIT */
} sys_project_begin_args_t;

#define SYS_PROJECT_OP_WRITE 0x02u  // W: sys_project_write_args_t + data
typedef struct __attribute__((packed)) {
  uint32_t offset;  /**< Byte offset; the data follows */
} sys_project_write_args_t;

#define SYS_PROJECT_OP_COMMIT 0x03u  // W: sys_project_commit_args_t
typedef struct __attribute__((packed)) {
  uint32_t crc32;  /**< CRC-32 of stream */
} sys_project_commit_args_t;

#define SYS_PROJECT_OP_ABORT 0x04u  // W: -

#define SYS_PROJECT_OP_INFO 0x05u  // R: -> sys_project_info_t

#define SYS_PROJECT_OP_RUN   0x06u  // W: -, RUN_ALL of the project, then its start section
#define SYS_PROJECT_OP_ERASE 0x07u  // W: -, the project's devices, the project record, both banks
#define SYS_PROJECT_OP_LOAD  0x12u  // W: -, RUN_ALL of the project, the program left stopped (no start section)

#define SYS_PROJECT_OP_OPTIONS 0x08u  // R: -> sys_project_options_t; W: sys_project_options_t
typedef struct __attribute__((packed)) {
  uint8_t boot;  /**< sys_project_boot_e */
} sys_project_options_t;

#define SYS_PROJECT_OP_SECTION 0x10u  // W: sys_project_section_t, a frame of the stream only: opens a section, refused as a command
typedef struct __attribute__((packed)) {
  uint32_t id;  /**< Target record ID (0 = program) */
} sys_project_section_t;

#define SYS_PROJECT_OP_MARK 0x11u  // W: sys_project_mark_t, first frame of the project record: bank, CRC and length of the stream (a no-op when run)

/* The project's source (SYS_PROJECT.MD "The source"): a file of the app's. */
#define SYS_PROJECT_OP_SOURCE_APPEND 0x13u  // W: sys_project_source_append_args_t + data
typedef struct __attribute__((packed)) {
  uint32_t offset;  /**< The file's length; the data follows */
} sys_project_source_append_args_t;

#define SYS_PROJECT_OP_SOURCE_ERASE 0x14u  // W: -

#define SYS_PROJECT_OP_SOURCE_INFO 0x15u  // R: -> sys_nvs_json_info_t { u32 bytes, u32 crc32, u16 chunks }

#define SYS_PROJECT_OP_SOURCE_READ 0x16u  // R: sys_project_source_read_args_t -> the chunk's bytes
typedef struct __attribute__((packed)) {
  uint16_t chunk;  /**< 0..chunks-1 */
} sys_project_source_read_args_t;

/**
 * @brief The project upload, its stored record and boot option, under ROUTER_KEY_PROJECT.
 * Ops and layouts: SYS_PROJECT.MD "Remote".
 * @usage ERR_TRY(router_register(ROUTER_KEY_PROJECT, sys_project_router));
 */
ROUTER_MODULE_DECLARE(sys_project_router);

#ifdef __cplusplus
}
#endif
