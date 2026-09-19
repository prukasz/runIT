#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include "esp_compiler.h"
#include "sys_io.h"
#include "vm_block_helpers.h"

/*
 *            -------------
 *   ->EN     |           | ->ENO
 *  (->IO_NUM)| IO_TOGGLE |
 *            -------------
 *
 *  VM_BLK_IO_TOGGLE -- Hardware Digital Output Toggle Block.
 *  Toggles the state of a designated hardware pin on a target device
 *  via sys_io_toggle() whenever enabled / triggered this pass.
 *
 *  Features:
 *  - Dynamic pin validation against a 64-bit safety bitmask.
 *  - ENO reflects successful toggle assertion this pass.
 */

#define VM_IO_TOGGLE_F_PREV_EN (1u << 0)

typedef struct __attribute__((aligned(8))) {
  uint64_t allowed_mask;       // Permitted pins bitmask (0..63)
  uint8_t  device_id;          // Target device ID
  uint8_t  default_io_num;     // Static pin number when IN0 (IO_NUM) is unwired
  uint8_t  flags;              // VM_IO_TOGGLE_F_*
  uint8_t  _pad[5];            // Align to 16 bytes
} vm_block_io_toggle_data_t;

_Static_assert(sizeof(vm_block_io_toggle_data_t) == 16, "vm_block_io_toggle_data_t must be 16 bytes");
#define VM_IO_TOGGLE_CUSTOM_LEN sizeof(vm_block_io_toggle_data_t)

static inline void vm_block_io_toggle_init_data(void* buffer, uint8_t device_id, uint8_t default_pin, uint64_t allowed_mask) {
  const vm_block_io_toggle_data_t data = {
      .allowed_mask   = allowed_mask,
      .device_id      = device_id,
      .default_io_num = default_pin,
      .flags          = 0,
  };
  memcpy(buffer, &data, sizeof(data));
}

#define VM_IO_TOGGLE_IN_PIN 0u // optional dynamic pin index

static inline bool vm_verify_io_toggle(vm_block_h b) {
  // 0 required inputs, 0 required outputs; optional IN0
  if (!vm_block_shape_valid(b, 0, 0, 0x0u)) return false;
  if (b->cfg.custom_len < sizeof(vm_block_io_toggle_data_t)) return false;

  const vm_block_io_toggle_data_t* d = (const vm_block_io_toggle_data_t*)vm_block_get_custom_data(b);
  if (!d) return false;
  if (d->allowed_mask == 0) return false;
  if (d->default_io_num >= 64 || !((1ULL << d->default_io_num) & d->allowed_mask)) return false;

  return true;
}

static inline void vm_blk_io_toggle(vm_block_h b) {
  if (unlikely(!vm_block_require(b, 0, 0, 0x0u))) {
    vm_block_set_eno(b, false);
    return;
  }

  vm_block_io_toggle_data_t* d = (vm_block_io_toggle_data_t*)vm_block_get_custom_data(b);
  if (unlikely(!d)) {
    vm_block_cfg_bad(b);
    vm_block_set_eno(b, false);
    return;
  }

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
    err_h err = VM_BLOCK_GET_PARAM(pin_val, b, VM_IO_TOGGLE_IN_PIN, d->default_io_num);
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

    BLOCK_CALL(sys_io_toggle(d->device_id, pin), b);
    if (unlikely(g_vm_block_fault)) {
      vm_block_set_eno(b, false);
      return;
    }

    vm_block_set_eno(b, true);
    return;
  }

  vm_block_set_eno(b, false);
}
