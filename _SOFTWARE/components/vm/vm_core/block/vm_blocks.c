#include "vm_blocks.h"

#include "esp_log.h"

const vm_block_type_t* g_vm_block_types[VM_BLOCK_TYPES_MAX];

static uint16_t s_count;

/* Runs from constructors, before app_main: the only failures are a type declared with a bad or repeated id or without
   a body, all of them mistakes in the source, so they stop the boot with the id in the log. */
void vm_block_type_register(uint8_t id, const vm_block_type_t* type) {
  if (id == VM_BLK_NONE || id >= VM_BLOCK_TYPES_MAX || type == NULL || type->run == NULL || g_vm_block_types[id]) {
    ESP_EARLY_LOGE("vm_blocks", "block type %u: bad id, no body or declared twice", id);
    abort();
  }
  g_vm_block_types[id] = type;
  s_count++;
}

uint16_t vm_block_types_count(void) {
  return s_count;
}
