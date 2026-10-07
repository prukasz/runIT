#include "vm_block_power_monitor.h"

void vm_blk_power_monitor(vm_block_h b) {
  vm_block_power_monitor_data_t d;
  memcpy(&d, vm_block_get_custom_data(b), sizeof(d));

  IF_BLOCK_ENABLED(b) {
    if (b->cfg.q_cnt > VM_OUT_POWER_MONITOR_VOLTAGE && vm_block_get_outputs(b)[VM_OUT_POWER_MONITOR_VOLTAGE]) {
      int32_t mV = 0;
      BLOCK_CALL(sys_power_monitor_get_voltage(d.device_id, d.channel, &mV), b);
      if (likely(!vm_block_failed(b))) {
        BLOCK_CALL(VM_OBJ_SET_SCALAR_AT_IDX((float)mV / 1000.0f, vm_block_get_outputs(b)[VM_OUT_POWER_MONITOR_VOLTAGE], 0), b);
      }
    }
    if (likely(!vm_block_failed(b)) && b->cfg.q_cnt > VM_OUT_POWER_MONITOR_CURRENT && vm_block_get_outputs(b)[VM_OUT_POWER_MONITOR_CURRENT]) {
      int32_t mA = 0;
      BLOCK_CALL(sys_power_monitor_get_current(d.device_id, d.channel, &mA), b);
      if (likely(!vm_block_failed(b))) {
        BLOCK_CALL(VM_OBJ_SET_SCALAR_AT_IDX((float)mA / 1000.0f, vm_block_get_outputs(b)[VM_OUT_POWER_MONITOR_CURRENT], 0), b);
      }
    }
    vm_block_set_eno(b, !vm_block_failed(b));
    return;
  }
  vm_block_set_eno(b, false);
}
