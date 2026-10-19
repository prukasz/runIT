

#include "err_internal.h"

// Filled by the ERR_MODULE_DEFINE constructors before app_main, read-only
// after that - no lock needed.
static struct {
  const err_module_t* modules[CONFIG_ERRORS_MAX_MODULES];
  uint32_t module_count;
} s_tags;

void err_register_module(const err_module_t* module) {
  assert(module != NULL);
  assert(err_module(module->id) == NULL && "error module ID registered twice");
  for (uint16_t i = 0; i < module->count; i++) {
    assert(ERR_MODULE(module->tags[i].tag) == module->id && "tag ID outside its module");
  }

  if (s_tags.module_count >= CONFIG_ERRORS_MAX_MODULES) {
    assert(!"more error modules than ERRORS_MAX_MODULES");
    return;
  }
  s_tags.modules[s_tags.module_count++] = module;
}

const err_module_t* err_module(uint16_t id) {
  for (uint32_t i = 0; i < s_tags.module_count; i++) {
    if (s_tags.modules[i]->id == id) {
      return s_tags.modules[i];
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
