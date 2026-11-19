/* The board's identity for its hellos, the one part of the session that is ESP-IDF's: the id in NVS, a random id per
 * boot, the firmware's build. The host tests give their own (host/). */
#include <string.h>

#include "esp_app_desc.h"
#include "esp_random.h"
#include "nvs.h"
#include "sdkconfig.h"
#include "sys_session_internal.h"

#define NVS_NAMESPACE "session"
#define NVS_KEY_ID    "id"

// The default NVS partition, made ready by sys_nvs_init at boot.
uint8_t sys_session_board_load_id(void) {
  uint8_t id = CONFIG_SYS_SESSION_BOARD_ID_DEFAULT;
  nvs_handle_t nvs;
  if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs) == ESP_OK) {
    if (nvs_get_u8(nvs, NVS_KEY_ID, &id) != ESP_OK || id > CMD_SRC_BOARD_LAST) {
      id = CONFIG_SYS_SESSION_BOARD_ID_DEFAULT;
    }
    nvs_close(nvs);
  }
  return id;
}

err_h sys_session_board_save_id(uint8_t id) {
  nvs_handle_t nvs;
  ERR_TRY_ESP(nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs));
  esp_err_t rc = nvs_set_u8(nvs, NVS_KEY_ID, id);
  if (rc == ESP_OK) {
    rc = nvs_commit(nvs);
  }
  nvs_close(nvs);
  return ERR_ESP(rc);
}

uint32_t sys_session_board_boot_id(void) {
  return esp_random();
}

uint32_t sys_session_board_build_id(void) {
  uint32_t id;
  memcpy(&id, esp_app_get_description()->app_elf_sha256, sizeof(id));
  return id;
}
