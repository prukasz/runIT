#include "sys_error_config.h"
#include <esp_log.h>
#include "enc_sys_errors.h"
#include "utils.h"

// This file's DBG() calls fire on CONFIG_DBG_GLOBAL or this component's own
// switch (components/utils/Kconfig) - see DBG()'s doc comment in utils.h.
#define DBG_ENABLE CONFIG_DBG_ENABLE_SYS_ERRORS

#undef OWNER
#define OWNER OWNER_SYS_ERRORS_CONFIG

static const char* TAG = __FILE_NAME__;

static sys_error_cfg_t s_cfg = SYS_ERROR_CFG_DEFAULT();

err_h SE_configure(const sys_error_cfg_t* cfg) {
  SE_CHECK_NOT_NULL(cfg);

  sys_error_cfg_t applied = *cfg;
  if (applied.errors.packet_max < ENC_SYS_ERRORS_MIN_BUF || applied.errors.packet_max > SE_ERR_PACKET_MAX) {
    applied.errors.packet_max = SE_ERR_PACKET_MAX;
  }

  s_cfg = applied;

  DBG(ESP_LOGI(TAG, "error telemetry config: hdr=0x%02X max=%u", applied.errors.tx_header, (unsigned)applied.errors.packet_max));
  return NULL;
}

err_h SE_get_config(sys_error_cfg_t* out_cfg) {
  SE_CHECK_NOT_NULL(out_cfg);
  *out_cfg = s_cfg;
  return NULL;
}
