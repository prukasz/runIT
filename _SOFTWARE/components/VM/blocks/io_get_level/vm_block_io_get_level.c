#include "vm_block_io_get_level.h"

void vm_blk_io_get_level(vm_block_h b) {
  vm_block_io_get_level_data_t d;
  memcpy(&d, vm_block_get_custom_data(b), sizeof(d));

  IF_BLOCK_ENABLED(b) {
    bool level = false;
    BLOCK_CALL(sys_io_get_level(SYS_IO_REF(d.device_id, d.pin), &level), b);
    if (likely(!vm_block_failed(b))) {
      BLOCK_CALL(VM_OBJ_SET_SCALAR_AT_IDX((uint8_t)(level ? 1 : 0), vm_block_get_outputs(b)[VM_OUT_IO_GET_LEVEL_LEVEL], 0), b);
    }
    vm_block_set_eno(b, !vm_block_failed(b));
    return;
  }
  vm_block_set_eno(b, false);
}
