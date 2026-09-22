#include "sys_error.h"
#include "sys_error_hooks.h"
#include "sys_error_log.h"

void SE_init(void) {
  se_log_init();
}

// Tag-based attribution is independent of the subsystem raising the error.
static bool device_id_of(err_h node, uint8_t* id) {
  switch (node->tag) {
#define DEVICE_TAG(tag)                                    \
  case tag:                                                \
    *id = ((err_payload_##tag##_t*)node->payload)->dev_id; \
    return true;
    DEVICE_TAG(ERR_DEV_DEP_FAILED)
    DEVICE_TAG(ERR_DEV_INSTALL_FAILED)
    DEVICE_TAG(ERR_DEV_NO_HANDLE)
    DEVICE_TAG(ERR_DEV_NOT_FOUND)
    DEVICE_TAG(ERR_DEV_ALREADY_EXIST)
    DEVICE_TAG(ERR_DEV_FEATURE_UNAVAILABLE)
    DEVICE_TAG(ERR_DEV_SUSPENDED)
    DEVICE_TAG(ERR_DEV_NOT_INSTALLED)
    DEVICE_TAG(ERR_DEV_DRIVER_FAILED)
    DEVICE_TAG(ERR_IO_PIN_UNCONFIGURED)
    DEVICE_TAG(ERR_IO_PIN_UNAVAILABLE)
    DEVICE_TAG(ERR_IO_PIN_ALREADY_IN_USE)
    DEVICE_TAG(ERR_IO_PIN_FEATURE_UNSUPPORTED)
    DEVICE_TAG(ERR_IO_PIN_LOCKED)
    DEVICE_TAG(ERR_IO_PIN_MODE_UNSUPPORTED)
    DEVICE_TAG(ERR_POWER_BUDGET_EXCEEDED)
#undef DEVICE_TAG
    default:
      return false;
  }
}

typedef struct {
  uint16_t domain;
  se_fault_hook_f hook;
} domain_hook_t;

static domain_hook_t s_domain_hooks[CONFIG_SYS_ERRORS_MAX_DOMAIN_HOOKS];
static se_fault_hook_f s_system_hook;
static se_device_report_f s_device_report;
static se_device_ignored_f s_device_ignored;

#undef OWNER
#define OWNER OWNER_SYS_ERRORS_CONFIG
err_h SE_register_domain_hook(uint16_t domain, se_fault_hook_f hook) {
  domain &= 0xFF00u;
  for (size_t i = 0; i < CONFIG_SYS_ERRORS_MAX_DOMAIN_HOOKS; ++i) {
    if (s_domain_hooks[i].hook == NULL || s_domain_hooks[i].domain == domain) {
      s_domain_hooks[i] = (domain_hook_t){.domain = domain, .hook = hook};
      return NULL;
    }
  }
  SE_FAIL(ERR_BASE_NO_MEM, 0);
}

void SE_register_system_hook(se_fault_hook_f hook) {
  s_system_hook = hook;
}

void SE_register_device_router(se_device_report_f report, se_device_ignored_f is_ignored) {
  s_device_report = report;
  s_device_ignored = is_ignored;
}

static SE_MUST_USE err_h dispatch_domain(err_h node, err_h chain) {
  uint16_t domain = (uint16_t)(node->owner & 0xFF00u);
  for (size_t i = 0; i < CONFIG_SYS_ERRORS_MAX_DOMAIN_HOOKS && s_domain_hooks[i].hook; ++i) {
    if (s_domain_hooks[i].domain == domain) return s_domain_hooks[i].hook(node, chain);
  }
  return NULL;
}

static void send_response(err_h response) {
  if (response) {
    SE_log(response);
  }
}

// A per-task guard suppresses policy re-entry without suppressing another task.
static __thread bool in_handler;

void SE_push_to_handler(err_h err) {
  if (!err) return;
  if (SE_is_suspended()) {
    SE_release(err);
    return;
  }
  err_h  nodes[SE_MAX_CHAIN_DEPTH];
  bool   complete;
  size_t count            = SE_collect_chain(err, nodes, &complete);
  bool   diagnostics_only = in_handler || !complete;
  for (size_t i = 0; i < count; ++i) {
    if (nodes[i]->tag == ERR_DEV_FAULT_RESPONSE_FAILED) diagnostics_only = true;
  }
  bool previous = in_handler;
  in_handler    = true;
  if (!diagnostics_only) {
    uint8_t devices[SE_MAX_CHAIN_DEPTH];
    size_t  handled        = 0;
    bool    system_handled = false;
    for (size_t i = 0; i < count; ++i) {
      err_h   node = nodes[i];
      uint8_t id;
      bool    device = device_id_of(node, &id);
      // NONE suppresses this node and its causes, but not preceding responses.
      if (device && s_device_ignored && s_device_ignored(id)) break;
      se_level_e level = SE_get_tag_level(node->tag);
      if (device) {
        size_t j = 0;
        while (j < handled && devices[j] != id) ++j;
        if (j == handled) {
          devices[handled++] = id;
          err_h device_fault = node;
          // Use the highest local severity for repeated mentions of this device,
          // without looking through an ignored-device boundary.
          for (size_t k = i + 1; k < count; ++k) {
            uint8_t other;
            if (!device_id_of(nodes[k], &other)) continue;
            if (s_device_ignored && s_device_ignored(other)) break;
            se_level_e candidate = SE_get_tag_level(nodes[k]->tag);
            if (other == id && candidate > level) {
              level        = candidate;
              device_fault = nodes[k];
            }
          }
          if (s_device_report) {
            send_response(s_device_report(id, level, device_fault));
            if (level == SE_LEVEL_CRITICAL) system_handled = true;
          }
        }
      }
      send_response(dispatch_domain(node, err));
      if (level == SE_LEVEL_CRITICAL && handled == 0 && !system_handled && s_system_hook) {
        system_handled = true;
        send_response(s_system_hook(node, err));
      }
    }
  }
  SE_log(err);
  in_handler = previous;
}

