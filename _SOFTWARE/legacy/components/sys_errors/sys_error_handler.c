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
    SYS_ERROR_DEVICE_TAGS(DEVICE_TAG)
#undef DEVICE_TAG
    default:
      return false;
  }
}

// A dependency wrapper only says where a failure passed through (which device, which layer); it adds no
// severity of its own. Without this a user's out-of-range argument, wrapped by the dispatcher as
// ERR_DEV_DEP_FAILED (HIGH), would count as a HIGH fault of the device and run its action.
static bool is_dependency_wrapper(uint16_t tag) {
  return tag == ERR_DEV_DEP_FAILED || tag == ERR_DEP_FAILED;
}

// Severity of a device node: its own, unless it is a dependency wrapper; then the severity of the first node
// below it that is not one (the real failure). A chain of wrappers only keeps the node's own severity.
static se_level_e device_node_level(const err_h* nodes, size_t count, size_t i) {
  for (size_t k = i; k < count; ++k) {
    if (!is_dependency_wrapper(nodes[k]->tag)) return SE_get_tag_level(nodes[k]->tag);
  }
  return SE_get_tag_level(nodes[i]->tag);
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
    bool    ignoring       = false;
    for (size_t i = 0; i < count; ++i) {
      err_h      node = nodes[i];
      uint8_t    id;
      bool       device = device_id_of(node, &id);
      se_level_e level  = device ? device_node_level(nodes, count, i) : SE_get_tag_level(node->tag);
      // An ignored (NONE) device suppresses this node and its causes, critical
      // ones included (the user chose to ignore it), but not preceding responses.
      if (device && s_device_ignored && s_device_ignored(id)) ignoring = true;
      if (ignoring) continue;
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
            se_level_e candidate = device_node_level(nodes, count, k);
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

