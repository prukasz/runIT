#include "vm_block_io_get_voltage.h"

void vm_blk_io_get_voltage(vm_block_h b) {
  vm_block_io_get_voltage_data_t d;
  memcpy(&d, vm_block_get_custom_data(b), sizeof(d));

  IF_BLOCK_ENABLED(b) {
    int32_t mV = 0;
    BLOCK_CALL(sys_io_get_voltage(SYS_IO_REF(d.device_id, d.pin), &mV), b);
    if (likely(!vm_block_failed(b))) {
      BLOCK_CALL(VM_OBJ_SET_SCALAR_AT_IDX((float)mV / 1000.0f, vm_block_get_outputs(b)[VM_OUT_IO_GET_VOLTAGE_VOLTAGE], 0), b);
    }
    vm_block_set_eno(b, !vm_block_failed(b));
    return;
  }
  vm_block_set_eno(b, false);
}
