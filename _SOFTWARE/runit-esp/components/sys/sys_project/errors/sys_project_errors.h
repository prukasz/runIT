#pragma once

#include "errors.h"

#define ERR_SYS_PROJECT_MAP(X)                                                                                                                                                   \
  X(ERR_PROJECT_BUSY,          ERR_TAG(ERR_MOD_SYS_PROJECT, 1),  ERR_SEV_MEDIUM, {})                               /* a save is open */                                          \
  X(ERR_PROJECT_NO_SESSION,    ERR_TAG(ERR_MOD_SYS_PROJECT, 2),  ERR_SEV_MEDIUM, {})                                                                                             \
  X(ERR_PROJECT_TOO_BIG,       ERR_TAG(ERR_MOD_SYS_PROJECT, 3),  ERR_SEV_MEDIUM, { uint32_t len; uint32_t max; })                                                                \
  X(ERR_PROJECT_OFFSET,        ERR_TAG(ERR_MOD_SYS_PROJECT, 4),  ERR_SEV_MEDIUM, { uint32_t got; uint32_t want; })                                                               \
  X(ERR_PROJECT_INCOMPLETE,    ERR_TAG(ERR_MOD_SYS_PROJECT, 5),  ERR_SEV_MEDIUM, { uint32_t got; uint32_t want; })                                                               \
  X(ERR_PROJECT_CRC,           ERR_TAG(ERR_MOD_SYS_PROJECT, 6),  ERR_SEV_MEDIUM, { uint32_t got; uint32_t want; })                                                               \
  X(ERR_PROJECT_BAD_FRAME,     ERR_TAG(ERR_MOD_SYS_PROJECT, 7),  ERR_SEV_MEDIUM, { uint32_t offset; })             /* length 0 or above a link frame */                          \
  X(ERR_PROJECT_TRUNCATED,     ERR_TAG(ERR_MOD_SYS_PROJECT, 8),  ERR_SEV_MEDIUM, {})                               /* the stream ends inside a frame */                          \
  X(ERR_PROJECT_NO_SECTION,    ERR_TAG(ERR_MOD_SYS_PROJECT, 9),  ERR_SEV_MEDIUM, {})                               /* a frame before the first SECTION */                        \
  X(ERR_PROJECT_BAD_SECTION,   ERR_TAG(ERR_MOD_SYS_PROJECT, 10), ERR_SEV_MEDIUM, { uint32_t id; })                 /* class not allowed, or a SECTION frame of the wrong size */ \
  X(ERR_PROJECT_SECTION_DUP,   ERR_TAG(ERR_MOD_SYS_PROJECT, 11), ERR_SEV_MEDIUM, { uint32_t id; })                                                                               \
  X(ERR_PROJECT_SECTION_FULL,  ERR_TAG(ERR_MOD_SYS_PROJECT, 12), ERR_SEV_MEDIUM, { uint32_t max; })                                                                              \
  X(ERR_PROJECT_SECTION_BIG,   ERR_TAG(ERR_MOD_SYS_PROJECT, 13), ERR_SEV_MEDIUM, { uint32_t id; uint32_t max; })                                                                 \
  X(ERR_PROJECT_SECTION_EMPTY, ERR_TAG(ERR_MOD_SYS_PROJECT, 14), ERR_SEV_LOW,    { uint32_t id; })                                                                               \
  X(ERR_PROJECT_ORDER,         ERR_TAG(ERR_MOD_SYS_PROJECT, 15), ERR_SEV_MEDIUM, {})                               /* a second program or start, or a section after them */      \
  X(ERR_PROJECT_CHUNKS,        ERR_TAG(ERR_MOD_SYS_PROJECT, 16), ERR_SEV_MEDIUM, { uint32_t max; })                /* the program needs more chunks */                           \
  X(ERR_PROJECT_EMPTY,         ERR_TAG(ERR_MOD_SYS_PROJECT, 17), ERR_SEV_LOW,    {})                               /* a stream with no section */                                \
  X(ERR_PROJECT_NOT_STORED,    ERR_TAG(ERR_MOD_SYS_PROJECT, 18), ERR_SEV_LOW,    {})                                                                                             \
  X(ERR_PROJECT_NOT_IN_STREAM, ERR_TAG(ERR_MOD_SYS_PROJECT, 19), ERR_SEV_MEDIUM, {})                               /* a SECTION frame sent as a command */                       \
  X(ERR_PROJECT_RUN,           ERR_TAG(ERR_MOD_SYS_PROJECT, 20), ERR_SEV_NONE,   {})                               /* context: stored, but the run failed */                     \
  X(ERR_PROJECT_BAD_ARG,       ERR_TAG(ERR_MOD_SYS_PROJECT, 21), ERR_SEV_HIGH,   {})                                                                                             \
  X(ERR_PROJECT_BAD_BOOT,      ERR_TAG(ERR_MOD_SYS_PROJECT, 22), ERR_SEV_MEDIUM, { uint8_t value; })               /* not a sys_project_boot_e */

ERR_TAGS_DECLARE(ERR_SYS_PROJECT_MAP)
