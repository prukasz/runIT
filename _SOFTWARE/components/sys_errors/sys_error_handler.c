#include "sys_error.h"
#include "sys_error_config.h"
#include "sys_error_log.h"
#include <esp_log.h>
#include "utils.h"

#define DBG_ENABLE CONFIG_DBG_ENABLE_SYS_ERRORS

uint32_t SE_get_dropped_count(void) {
  return 0;
}

void SE_clear_dropped_count(void) {
}

// -----------------------------------------------------------------------------
// Handler Initialization
// -----------------------------------------------------------------------------

void SE_init(void) {
  se_log_init();
}

void SE_push_to_handler(err_h err) {
  if (!err || SE_is_suspended()) return;
}
