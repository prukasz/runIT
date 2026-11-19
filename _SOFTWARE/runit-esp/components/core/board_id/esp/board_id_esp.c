/**
 * @file board_id_esp.c
 * @brief This chip's part of the board's identity (internal/board_id_port.h): ESP-IDF's random number and the
 * firmware's description. The one file that names them.
 */

#include <string.h>

#include "board_id_port.h"
#include "esp_app_desc.h"
#include "esp_random.h"

uint32_t board_id_port_random(void) {
  return esp_random();
}

uint32_t board_id_port_build(void) {
  uint32_t id;
  memcpy(&id, esp_app_get_description()->app_elf_sha256, sizeof(id));
  return id;
}
