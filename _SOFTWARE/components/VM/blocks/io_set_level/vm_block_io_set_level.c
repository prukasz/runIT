#include "vm_block_io_set_level.h"

bool vm_verify_io_set_level(vm_block_h b) {
  vm_block_io_set_level_data_t data;
  memcpy(&data, vm_block_get_custom_data(b), sizeof(data));
  const vm_block_io_set_level_data_t* d = &data;
  if (d->allowed_mask == 0) return false;
  if (d->default_io_num >= 64 || !((1ULL << d->default_io_num) & d->allowed_mask)) return false;
  if (d->when_not_active > VM_IO_DISABLED_HOLD) return false;
  if (d->default_level > VM_IO_LEVEL_LOW) return false;

  return true;
}

void vm_blk_io_set_level(vm_block_h b) {
  vm_block_io_set_level_data_t* d = (vm_block_io_set_level_data_t*)vm_block_get_custom_data(b);

  IF_BLOCK_ENABLED(b) {
    // 1. Read optional LEVEL input (defaults to d->default_level)
    bool level = false;
    err_h err = VM_BLOCK_GET_PARAM(level, b, VM_IO_SET_IN_LEVEL, d->default_level == VM_IO_LEVEL_HIGH);
    if (unlikely(!vm_block_check(b, err))) {
      vm_block_set_eno(b, false);
      return;
    }

    // 2. Read optional IO_NUM input (defaults to d->default_io_num)
    uint32_t pin_val = d->default_io_num;
    err = VM_BLOCK_GET_PARAM(pin_val, b, VM_IO_SET_IN_PIN, d->default_io_num);
    if (unlikely(!vm_block_check(b, err))) {
      vm_block_set_eno(b, false);
      return;
    }

    uint8_t pin = (uint8_t)pin_val;

    // 3. Validate pin against allowed_mask
    if (unlikely(pin >= 64 || !((1ULL << pin) & d->allowed_mask))) {
      BLOCK_CALL(VM_BLK_ERR_NEW(ERR_IO_PIN_UNAVAILABLE, .dev_id = d->device_id, .pin_num = pin), b);
      vm_block_set_eno(b, false);
      return;
    }

    // 4. Check if write is needed (first pass, level changed, or pin changed)
    bool last_level = (d->flags & VM_IO_SET_F_LAST_LEVEL) != 0;
    bool needs_write = !(d->flags & VM_IO_SET_F_INITIALIZED) ||
                       (level != last_level) ||
                       (pin != d->last_pin);

    if (needs_write) {
      BLOCK_CALL(sys_io_set_level(SYS_IO_REF(d->device_id, pin), level), b);
      if (unlikely(vm_block_failed(b))) {
        vm_block_set_eno(b, false);
        return;
      }
      d->flags |= VM_IO_SET_F_INITIALIZED;
      if (level) d->flags |= VM_IO_SET_F_LAST_LEVEL;
      else d->flags &= ~VM_IO_SET_F_LAST_LEVEL;
      d->last_pin = pin;
    }

    vm_block_set_eno(b, true);
    return;
  }

  // Handle transition when block is disabled
  if (d->flags & VM_IO_SET_F_INITIALIZED) {
    if (d->when_not_active != VM_IO_DISABLED_HOLD) {
      bool dis_level = (d->when_not_active == VM_IO_DISABLED_FORCE_HIGH);
      bool last_level = (d->flags & VM_IO_SET_F_LAST_LEVEL) != 0;
      if (dis_level != last_level) {
        BLOCK_CALL(sys_io_set_level(SYS_IO_REF(d->device_id, d->last_pin), dis_level), b);
        if (unlikely(vm_block_failed(b))) {
          vm_block_set_eno(b, false);
          return;
        }
        if (dis_level) d->flags |= VM_IO_SET_F_LAST_LEVEL;
        else d->flags &= ~VM_IO_SET_F_LAST_LEVEL;
      }
    }
  }

  vm_block_set_eno(b, false);
}
