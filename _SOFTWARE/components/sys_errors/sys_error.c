#include "sys_error.h"
#include <esp_log.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "enc_sys_errors.h"
#include "sys_ble.h"

// enc_sys_errors.h leaves OWNER set to OWNER_ENC_SYS_ERRORS; take it back so
// this file's own SE_* macros are tagged as sys_errors, not as the encoder.
#undef OWNER
#define OWNER OWNER_SYS_ERRORS_CONFIG

static const char* TAG = __FILE_NAME__;

const char* SE_get_owner_name(uint32_t owner) {
  switch (owner) {
#define X_OWNER_CASE(tag, id, name) \
  case id:                          \
    return name;
    SYS_OWNER_MAP(X_OWNER_CASE)
#undef X_OWNER_CASE
    default:
      return "OWNER_UNKNOWN";
  }
}

const char* SE_get_tag_name(err_tag_e tag) {
  switch (tag) {
#define X_TAG_CASE(tag_name, struct_def) \
  case tag_name:                         \
    return #tag_name;
    SYS_ERROR_MAP(X_TAG_CASE)
#undef X_TAG_CASE
    default:
      return "TAG_UNKNOWN";
  }
}

size_t SE_get_payload_size(err_tag_e tag) {
  switch (tag) {
#define X_TAG_SIZE(tag_name, struct_def) \
  case tag_name:                         \
    return sizeof(err_payload_##tag_name##_t);
    SYS_ERROR_MAP(X_TAG_SIZE)
#undef X_TAG_SIZE
    default:
      return 0;
  }
}

_Static_assert(ERR_MAX_COUNT <= 0xFFFF, "err_tag_e no longer fits the uint16 tag field of the error packet");

#define X_CHK(tag, struct_def)                                                                                                         \
  _Static_assert(sizeof(sys_err_t) + sizeof(err_payload_##tag##_t) <= ERR_BUF_SIZE / 4, #tag " payload too large for the error ring"); \
  SYS_ERROR_MAP(X_CHK)
#undef X_CHK

#define X_OWNER_CHK(tag, id, name)                                                                            \
  _Static_assert((id) <= 0xFFFF, #tag " owner id no longer fits the uint16 owner field of the error packet"); \
  SYS_OWNER_MAP(X_OWNER_CHK)
#undef X_OWNER_CHK

#define ERR_BUF_SIZE 1024
static uint8_t err_buffer[ERR_BUF_SIZE] __attribute__((aligned(8)));
static uint32_t head_idx = 0;
static volatile int8_t s_suspend_depth = 0;

static sys_error_cfg_t s_cfg = SYS_ERROR_CFG_DEFAULT();

// Re-entrancy guard, not a lock: sys_ble_char_send() logs on its own error
// paths, and that log would come straight back here. A nested call finds the
// flag set and takes the serial-only path instead of recursing.
static volatile bool s_in_ble_log = false;

err_h SE_alloc_bytes(size_t payload_size, err_tag_e tag, uint32_t owner) {
  uint32_t total_size = sizeof(sys_err_t) + payload_size;
  total_size = (total_size + 7) & ~7u;  // align to 8 bytes

  uint32_t old_head, new_head, alloc_idx;
  do {
    old_head = __atomic_load_n(&head_idx, __ATOMIC_RELAXED);

    if (old_head + total_size > ERR_BUF_SIZE) {
      alloc_idx = 0;
      new_head = total_size;
    } else {
      alloc_idx = old_head;
      new_head = old_head + total_size;
    }
  } while (!__atomic_compare_exchange_n(&head_idx, &old_head, new_head, false, __ATOMIC_SEQ_CST, __ATOMIC_RELAXED));

  err_h err = (err_h)&err_buffer[alloc_idx];
  memset(err, 0, sizeof(sys_err_t));  // payload is zero-filled by the compound-literal assignment
  err->tag = tag;
  err->owner = owner;
  err->next_cause = NULL;

  return err;
}

// Nesting-safe: a suspended section may call into another function that
// also suspends/resumes without prematurely re-enabling error reporting.
void SE_suspend(void) {
  s_suspend_depth++;
}
void SE_resume(void) {
  if (s_suspend_depth > 0) {
    s_suspend_depth--;
  }
}
bool SE_is_suspended(void) {
  return s_suspend_depth > 0;
}

// One esp_log call == one whole line, so the rendered buffer can go out as a
// single BLE notification with no accumulation. This holds under log v1
// (CONFIG_LOG_VERSION_1); log v2 splits a line across three vprintf calls and
// would need the line reassembled before sending.
static int se_log_vprintf(const char* fmt, va_list args) {
  if (s_cfg.logs.ble_enable && !__atomic_exchange_n(&s_in_ble_log, true, __ATOMIC_SEQ_CST)) {
    char line[SE_LOG_LINE_MAX];
    va_list rendered;
    va_copy(rendered, args);
    int len = vsnprintf(line, sizeof(line), fmt, rendered);
    va_end(rendered);

    if (len > 0) {
      // vsnprintf reports what it *would* have written - clamp to what it did.
      size_t out_len = ((size_t)len < sizeof(line)) ? (size_t)len : sizeof(line) - 1u;

      // ignere errors generated during sendings
      (void)sys_ble_char_send(s_cfg.logs.char_uuid, s_cfg.logs.tx_header, (const uint8_t*)line, out_len, true);
    }
    __atomic_store_n(&s_in_ble_log, false, __ATOMIC_SEQ_CST);
  }

  // mirror if selected
  if (s_cfg.logs.mirror_on_serial) {
    return vprintf(fmt, args);
  }
  return 0;
}

err_h SE_configure(const sys_error_cfg_t* cfg) {
  SE_CHECK_NOT_NULL(cfg);

  if (cfg->logs.ble_enable && cfg->logs.char_uuid == 0) {
    SE_RET_ERR(ERR_INVALID_VAL_UI32, .val = 0, .min = 1, .max = 0xFFFF);
  }
  if (cfg->errors.ble_enable && cfg->errors.char_uuid == 0) {
    SE_RET_ERR(ERR_INVALID_VAL_UI32, .val = 0, .min = 1, .max = 0xFFFF);
  }

  sys_error_cfg_t applied = *cfg;
  if (applied.errors.packet_max < ENC_SYS_ERRORS_MIN_BUF || applied.errors.packet_max > SE_ERR_PACKET_MAX) {
    applied.errors.packet_max = SE_ERR_PACKET_MAX;
  }

  esp_log_level_set("*", applied.global_level);

  // Publish before installing the hook so the hook never runs against stale settings.
  s_cfg = applied;

  // Idempotent: re-pointing to the same function on a later SE_configure()
  // call is a no-op. The return value (the previous hook) is never captured -
  // nothing else in this codebase installs one, so it's always plain vprintf.
  (void)esp_log_set_vprintf(se_log_vprintf);

  ESP_LOGI(TAG, "config: level=%d | logs serial=%d ble=%d chr=0x%04X hdr=0x%02X | errors trace=%d ble=%d chr=0x%04X hdr=0x%02X", (int)applied.global_level, applied.logs.mirror_on_serial, applied.logs.ble_enable, applied.logs.char_uuid, applied.logs.tx_header, applied.errors.serial_trace,
      applied.errors.ble_enable, applied.errors.char_uuid, applied.errors.tx_header);
  return NULL;
}

err_h SE_get_config(sys_error_cfg_t* out_cfg) {
  SE_CHECK_NOT_NULL(out_cfg);
  *out_cfg = s_cfg;
  return NULL;
}
