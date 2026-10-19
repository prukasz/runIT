

#include "err_internal.h"

// Filled by the ERR_MODULE_DEFINE constructors before app_main, read-only
// after that - no lock needed.
static const err_module_t* s_modules[CONFIG_ERRORS_MAX_MODULES];
static uint32_t s_module_count;

void err_register_module(const err_module_t* module) {
  assert(module != NULL);
  assert(err_module(module->id) == NULL && "error module ID registered twice");
  for (uint16_t i = 0; i < module->count; i++) {
    assert(ERR_MODULE(module->tags[i].tag) == module->id && "tag ID outside its module");
  }

  if (s_module_count >= CONFIG_ERRORS_MAX_MODULES) {
    assert(!"more error modules than ERRORS_MAX_MODULES");
    return;
  }
  s_modules[s_module_count++] = module;
}

const err_module_t* err_module(uint16_t id) {
  for (uint32_t i = 0; i < s_module_count; i++) {
    if (s_modules[i]->id == id) {
      return s_modules[i];
    }
  }
  return NULL;
}

const err_tag_info_t* err_tag_info(uint32_t tag) {
  const err_module_t* module = err_module(ERR_MODULE(tag));
  if (module == NULL) {
    return NULL;
  }
  for (uint16_t i = 0; i < module->count; i++) {
    if (module->tags[i].tag == tag) {
      return &module->tags[i];
    }
  }
  return NULL;
}
