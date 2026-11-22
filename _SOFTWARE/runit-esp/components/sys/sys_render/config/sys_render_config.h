/**
 * @file
 * @brief Limits of the render: constants
 */
#pragma once
#include <stdint.h>

/** @brief The slowest refresh rate install takes (Hz) */
#define SYS_RENDER_REFRESH_HZ_MIN 1u
/** @brief The fastest refresh rate install takes (Hz) */
#define SYS_RENDER_REFRESH_HZ_MAX 60u

/** @brief The smallest pool install takes (B) */
#define SYS_RENDER_HEAP_BYTES_MIN 32768u
/** @brief The largest pool install takes (B) */
#define SYS_RENDER_HEAP_BYTES_MAX 204800u

/** @brief What sys_render_pool_check keeps free for what the engine allocates by itself (B) */
#define SYS_RENDER_POOL_RESERVE_BYTES 2048u

/** @brief The longest wait for the end of a FLUSH (ms) */
#define SYS_RENDER_FLUSH_TIMEOUT_MS 200u

/** @brief Whole rows of the panel's native width in one draw buffer (before flush_max limits it) */
#define SYS_RENDER_BUF_LINES 20u
/** @brief Draw buffers (double buffering) */
#define SYS_RENDER_BUF_COUNT 2u

/* ---- The render task (its stack and priority are set for the whole application when it is
 *      adapted; these are the values the module runs with until then) ---- */

/** @brief Stack of the render task (StackType_t units: bytes on ESP-IDF). LVGL's software
 * renderer and the label layout need about 4 KB; the rest is margin for the logging */
#define SYS_RENDER_TASK_STACK 8192u
/** @brief Priority of the render task: above the application's idle work, below the system's
 * communication tasks */
#define SYS_RENDER_TASK_PRIORITY 4u
/** @brief Longest sleep of the render task while RUNNING (ms): widgets changed under the lock
 * without a wake-up are drawn at the latest then */
#define SYS_RENDER_TASK_SLEEP_MAX_MS 100u
/** @brief What sys_render_task_step returns when nothing is to be drawn: sleep until woken */
#define SYS_RENDER_TASK_SLEEP_FOREVER UINT32_MAX
