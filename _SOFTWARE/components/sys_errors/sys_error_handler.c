#include "sys_error.h"
#include "sys_error_hooks.h"
#include "sys_error_config.h"
#include "sys_error_log.h"
#include <esp_log.h>
#include "devices_owners.h"

#define DBG_ENABLE CONFIG_DBG_ENABLE_SYS_ERRORS


// -----------------------------------------------------------------------------
// Handler Initialization
// -----------------------------------------------------------------------------

void SE_init(void) {
  se_log_init();
}

// -----------------------------------------------------------------------------
// Synchronous Error Dispatch Pipeline
// -----------------------------------------------------------------------------

#define MAX_DEVICES_PER_CHAIN 8

typedef enum {
  SE_DISPATCH_CONTINUE = 0,
  SE_DISPATCH_STOP     = 1,
} se_dispatch_action_e;

/**
 * @brief Dispatches an individual error node to its owning domain handler.
 */
static se_dispatch_action_e dispatch_node_by_owner(err_h node, err_h full_chain,
                                                    uint8_t handled_devices[], uint8_t* handled_count) {
  uint16_t domain = node->owner & 0xFF00;
  sys_device_err_level_e level = SE_get_tag_level(node->tag);

  switch (domain) {
    case OWNER_DEVICE_BASE:       // 0xD000: Physical Device Providers
    case OWNER_SYS_DEVICE_BASE: { // 0xA100: Device System Manager
      uint8_t dev_id = 0xFF;
      if (node->tag == ERR_DEV_DEP_FAILED) {
        dev_id = ((err_payload_ERR_DEV_DEP_FAILED_t*)node->payload)->dev_id;
      } else if (node->tag == ERR_DEV_INSTALL_FAILED) {
        dev_id = ((err_payload_ERR_DEV_INSTALL_FAILED_t*)node->payload)->dev_id;
      } else if (node->tag == ERR_DEV_NO_HANDLE) {
        dev_id = ((err_payload_ERR_DEV_NO_HANDLE_t*)node->payload)->dev_id;
      }

      if (dev_id != 0xFF) {
        // If device importance is NONE (test device):
        // STOP analysis of chain immediately and proceed to send and log only!
        if (sys_device_is_ignored && sys_device_is_ignored(dev_id)) {
          return SE_DISPATCH_STOP;
        }

        bool already_handled = false;
        for (uint8_t i = 0; i < *handled_count; i++) {
          if (handled_devices[i] == dev_id) {
            already_handled = true;
            break;
          }
        }

        if (!already_handled) {
          if (*handled_count < MAX_DEVICES_PER_CHAIN) {
            handled_devices[(*handled_count)++] = dev_id;
          }
          if (sys_device_report_error) {
            err_h policy_err = sys_device_report_error(dev_id, full_chain);
            if (policy_err) {
              SE_send(policy_err);
            }
          }
        }
      }
      break;
    }

    case OWNER_SYS_I2C_BASE: {    // 0xA200: I2C Bus Operations
      if (sys_i2c_report_fault) {
        err_h fault_err = sys_i2c_report_fault(node, full_chain);
        if (fault_err) SE_send(fault_err);
      }
      break;
    }

    case OWNER_SYS_IO_BASE: {     // 0xA300: Digital/Analog I/O
      if (sys_io_report_fault) {
        err_h fault_err = sys_io_report_fault(node, full_chain);
        if (fault_err) SE_send(fault_err);
      }
      break;
    }

    case OWNER_SYS_POWER_BASE: {  // 0xA400: Power & Rails
      if (sys_power_report_fault) {
        err_h fault_err = sys_power_report_fault(node, full_chain);
        if (fault_err) SE_send(fault_err);
      }
      break;
    }

    case OWNER_SYS_BLE_BASE: {    // 0xA500: Bluetooth Low Energy
      if (sys_ble_report_fault) {
        err_h fault_err = sys_ble_report_fault(node, full_chain);
        if (fault_err) SE_send(fault_err);
      }
      break;
    }

    case OWNER_SYS_INTERFACE_BASE: { // 0xA600: Interface Router & Decoders
      if (sys_interface_report_fault) {
        err_h fault_err = sys_interface_report_fault(node, full_chain);
        if (fault_err) SE_send(fault_err);
      }
      break;
    }

    case OWNER_SYS_BUFFERS_BASE: {   // 0xA700: Memory Pools & Ring Buffers
      if (sys_buffers_report_fault) {
        err_h fault_err = sys_buffers_report_fault(node, full_chain);
        if (fault_err) SE_send(fault_err);
      }
      break;
    }

    case OWNER_SYS_ERRORS_BASE: {    // 0xA800: Error Subsystem
      if (sys_errors_report_fault) {
        err_h fault_err = sys_errors_report_fault(node, full_chain);
        if (fault_err) SE_send(fault_err);
      }
      break;
    }

    case OWNER_VM_BASE: {            // 0xA900: Virtual Machine
      if (sys_vm_report_fault) {
        err_h fault_err = sys_vm_report_fault(node, full_chain);
        if (fault_err) SE_send(fault_err);
      }
      break;
    }

    case OWNER_SYS_ACTIONS_BASE: {   // 0xAA00: Actions Registry & Executor
      if (sys_actions_report_fault) {
        err_h fault_err = sys_actions_report_fault(node, full_chain);
        if (fault_err) SE_send(fault_err);
      }
      break;
    }

    default:
      break;
  }

  // Cross-cutting critical safety check (for non-device or unhandled critical nodes)
  if (level == SYS_DEV_ERR_CRITICAL) {
    if (sys_device_report_error) {
      err_h policy_err = sys_device_report_error(0, full_chain);
      if (policy_err) SE_send(policy_err);
    }
  }

  return SE_DISPATCH_CONTINUE;
}

/**
 * @brief Main synchronous error entry point:
 * Traverses from last error (head) down to cause (root).
 * If an error should be ignored (e.g. device importance == NONE),
 * chain analysis stops immediately and proceeds to send and log only.
 */
void SE_push_to_handler(err_h err) {
  if (!err || SE_is_suspended()) return;

  // 1. Recursion guard: never feed policy failures back into the policy
  for (err_h curr = err; curr != NULL; curr = curr->next_cause) {
    if (curr->tag == ERR_DEV_FAULT_RESPONSE_FAILED) {
      SE_send(err);
      return;
    }
  }

  // 2. Traversal from last error (head) down to cause (root)
  uint8_t handled_devices[MAX_DEVICES_PER_CHAIN];
  uint8_t handled_count = 0;

  for (err_h curr = err; curr != NULL; curr = curr->next_cause) {
    if (!SE_is_valid_error_ptr(curr)) break;

    se_dispatch_action_e action = dispatch_node_by_owner(curr, err, handled_devices, &handled_count);
    if (action == SE_DISPATCH_STOP) {
      break;
    }
  }

  // 3. Dispatch diagnostics across configured channels (formatted logging + binary telemetry)
  SE_send(err);
}
