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
    DEVICE_TAG(ERR_IO_PIN_UNCONFIGURED)
    DEVICE_TAG(ERR_IO_PIN_UNAVAILABLE)
    DEVICE_TAG(ERR_IO_PIN_ALREADY_IN_USE)
    DEVICE_TAG(ERR_IO_PIN_FEATURE_UNSUPPORTED)
    DEVICE_TAG(ERR_IO_PIN_LOCKED)
    DEVICE_TAG(ERR_IO_PIN_MODE_UNSUPPORTED)
#undef DEVICE_TAG
    case ERR_POWER_BUDGET_EXCEEDED:
      *id = ((err_payload_ERR_POWER_BUDGET_EXCEEDED_t*)node->payload)->device_id;
      return true;
    default:
      return false;
  }
}

static err_h dispatch_domain(err_h node, err_h chain) {
  switch (node->owner & 0xFF00u) {
#define DOMAIN(owner, hook) \
  case owner:               \
    return hook ? hook(node, chain) : NULL;
    DOMAIN(OWNER_SYS_I2C_BASE, sys_i2c_report_fault)
    DOMAIN(OWNER_SYS_IO_BASE, sys_io_report_fault)
    DOMAIN(OWNER_SYS_POWER_BASE, sys_power_report_fault)
    DOMAIN(OWNER_SYS_BLE_BASE, sys_ble_report_fault)
    DOMAIN(OWNER_SYS_INTERFACE_BASE, sys_interface_report_fault)
    DOMAIN(OWNER_SYS_BUFFERS_BASE, sys_buffers_report_fault)
    DOMAIN(OWNER_SYS_ERRORS_BASE, sys_errors_report_fault)
    DOMAIN(OWNER_VM_BASE, sys_vm_report_fault)
    DOMAIN(OWNER_SYS_ACTIONS_BASE, sys_actions_report_fault)
#undef DOMAIN
    default:
      return NULL;
  }
}

static void send_response(err_h response) {
  if (response) {
    SE_release(SE_send(response));
    SE_release(response);
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
      if (device && sys_device_is_ignored && sys_device_is_ignored(id)) break;
      sys_device_err_level_e level = SE_get_tag_level(node->tag);
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
            if (sys_device_is_ignored && sys_device_is_ignored(other)) break;
            sys_device_err_level_e candidate = SE_get_tag_level(nodes[k]->tag);
            if (other == id && candidate > level) {
              level        = candidate;
              device_fault = nodes[k];
            }
          }
          if (sys_device_report_error_with_level) {
            send_response(sys_device_report_error_with_level(id, level, device_fault));
            if (level == SYS_DEV_ERR_CRITICAL) system_handled = true;
          }
        }
      }
      send_response(dispatch_domain(node, err));
      if (level == SYS_DEV_ERR_CRITICAL && !system_handled && sys_system_report_fault) {
        system_handled = true;
        send_response(sys_system_report_fault(node, err));
      }
    }
  }
  SE_release(SE_send(err));
  SE_release(err);
  in_handler = previous;
}
