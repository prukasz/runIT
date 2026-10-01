#include "vm_block_io_toggle.h"

bool vm_verify_io_toggle(vm_block_h b) {
  vm_block_io_toggle_data_t data;
  memcpy(&data, vm_block_get_custom_data(b), sizeof(data));
  const vm_block_io_toggle_data_t* d = &data;
  if (d->allowed_mask == 0) return false;
  if (d->default_io_num >= 64 || !((1ULL << d->default_io_num) & d->allowed_mask)) return false;

  return true;
}

void vm_blk_io_toggle(vm_block_h b) {
  vm_block_io_toggle_data_t* d = (vm_block_io_toggle_data_t*)vm_block_get_custom_data(b);

  bool en = vm_block_is_enabled(b);
  bool prev_en = (d->flags & VM_IO_TOGGLE_F_PREV_EN) != 0;
  bool rising = en && !prev_en;

  if (en) {
    d->flags |= VM_IO_TOGGLE_F_PREV_EN;
  } else {
    d->flags &= ~VM_IO_TOGGLE_F_PREV_EN;
  }

  if (rising) {
    uint32_t pin_val = d->default_io_num;
    err_h err = VM_BLOCK_GET_PARAM(pin_val, b, VM_IN_IO_TOGGLE_PIN, d->default_io_num);
    if (unlikely(!vm_block_check(b, err))) {
      vm_block_set_eno(b, false);
      return;
    }

    uint8_t pin = (uint8_t)pin_val;
    if (unlikely(pin >= 64 || !((1ULL << pin) & d->allowed_mask))) {
      BLOCK_CALL(VM_BLK_ERR_NEW(ERR_IO_PIN_UNAVAILABLE, .dev_id = d->device_id, .pin_num = pin), b);
      vm_block_set_eno(b, false);
      return;
    }

    BLOCK_CALL(sys_io_toggle(SYS_IO_REF(d->device_id, pin)), b);
    if (unlikely(vm_block_failed(b))) {
      vm_block_set_eno(b, false);
      return;
    }

    vm_block_set_eno(b, true);
    return;
  }

  vm_block_set_eno(b, false);
}
