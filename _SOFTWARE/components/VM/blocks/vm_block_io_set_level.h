#pragma once

#include "sys_io.h"
#include "vm_block_helpers.h"

/*
 *            -------------------
 *   ->EN     |                 | ->ENO
 *   ->LEVEL  |   IO_SET_LEVEL  |
 *  (->IO_NUM)|                 |
 *            -------------------
 *
 *  VM_BLK_IO_SET_LEVEL -- Hardware Digital Output Block.
 *  Sets the digital level (HIGH / LOW) of a designated hardware pin on a target device
 *  (such as ESP GPIO, TCA6424A expander, etc.) via sys_io_set_level().
 *
 *  Features:
 *  - In-block write-on-change caching: avoids redundant I2C/SPI bus transactions.
 *  - Dynamic pin validation: validates optional IN1 against a 64-bit permitted bitmask.
 *  - Configurable disabled state: HOLD (do nothing), FORCE_LOW (0), or FORCE_HIGH (1).
 *  - ENO reflects successful hardware assertion this pass.
 */

//#ref-enum @alias IO Disabled Action
typedef enum {
  VM_IO_DISABLED_HOLD       = 0, // Keep last state on disable
  VM_IO_DISABLED_FORCE_LOW  = 1, // Assert LOW (0) on disable
  VM_IO_DISABLED_FORCE_HIGH = 2, // Assert HIGH (1) on disable
} vm_io_disabled_state_e;

#define VM_IO_SET_F_INITIALIZED (1u << 0) // Pin has been driven at least once
#define VM_IO_SET_F_LAST_LEVEL  (1u << 1) // Cached last level written (0 or 1)
#define VM_IO_SET_F_ALWAYS      (1u << 2) // Force hardware write on every pass

typedef struct __attribute__((aligned(8))) {
  uint64_t allowed_mask;       // Permitted pins bitmask (0..63) for dynamic selection
  uint8_t  device_id;          // Target device ID
  uint8_t  default_io_num;     // Static pin number when IN1 (IO_NUM) is unwired
  uint8_t  disabled_action;    // What disabled does @enum-ref vm_io_disabled_state_e
  uint8_t  flags;              // VM_IO_SET_F_*: only VM_IO_SET_F_ALWAYS (0x04) is set by the app
  uint8_t  last_pin;           // Cached last pin written; the app writes default_io_num
  uint8_t  _pad[3];            // Align to 16 bytes
} vm_block_io_set_level_data_t;

_Static_assert(sizeof(vm_block_io_set_level_data_t) == 16, "vm_block_io_set_level_data_t must be 16 bytes");
#define VM_IO_SET_LEVEL_CUSTOM_LEN sizeof(vm_block_io_set_level_data_t)

/**
 * @brief Initialize IO set level configuration in block custom data.
 */
static inline void vm_block_io_set_level_init_data(void* buffer, uint8_t device_id, uint8_t default_pin,
                                                   uint64_t allowed_mask, vm_io_disabled_state_e disabled_action,
                                                   uint8_t flags) {
  const vm_block_io_set_level_data_t data = {
      .allowed_mask    = allowed_mask,
      .device_id       = device_id,
      .default_io_num  = default_pin,
      .disabled_action = (uint8_t)disabled_action,
      .flags           = flags,
      .last_pin        = default_pin,
  };
  memcpy(buffer, &data, sizeof(data));
}

#define VM_IO_SET_IN_LEVEL 0u // required: boolean / scalar level
#define VM_IO_SET_IN_PIN   1u // optional: dynamic pin number (0..63)

static inline bool vm_verify_io_set_level(vm_block_h b) {
  vm_block_io_set_level_data_t data;
  memcpy(&data, vm_block_get_custom_data(b), sizeof(data));
  const vm_block_io_set_level_data_t* d = &data;
  if (d->allowed_mask == 0) return false;
  if (d->default_io_num >= 64 || !((1ULL << d->default_io_num) & d->allowed_mask)) return false;
  if (d->disabled_action > VM_IO_DISABLED_FORCE_HIGH) return false;

  return true;
}

static inline void vm_blk_io_set_level(vm_block_h b) {
  vm_block_io_set_level_data_t* d = (vm_block_io_set_level_data_t*)vm_block_get_custom_data(b);

  IF_BLOCK_ENABLED(b) {
    // 1. Read required LEVEL input
    const vm_accessor_t* lvl_acc = vm_block_get_inputs(b)[VM_IO_SET_IN_LEVEL];
    bool level = false;
    err_h err = VM_OBJ_SCALAR_GET(level, lvl_acc);
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

    // 4. Check if write is needed (first pass, level changed, pin changed, or ALWAYS flag)
    bool last_level = (d->flags & VM_IO_SET_F_LAST_LEVEL) != 0;
    bool needs_write = !(d->flags & VM_IO_SET_F_INITIALIZED) ||
                       (level != last_level) ||
                       (pin != d->last_pin) ||
                       (d->flags & VM_IO_SET_F_ALWAYS);

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
    if (d->disabled_action == VM_IO_DISABLED_FORCE_LOW || d->disabled_action == VM_IO_DISABLED_FORCE_HIGH) {
      bool dis_level = (d->disabled_action == VM_IO_DISABLED_FORCE_HIGH);
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


/* Palette entry (vm_blocks_table.c): shape and state size are checked at load
   by vm_block_verify(), so the body never re-checks them. */
//#vm-block VM_BLK_IO_SET_LEVEL @title Set Pin Level @category io @state vm_block_io_set_level_data_t @activation enabled Runs every pass while enabled.
//@block-description Drives a pin on an IO device (ESP GPIO, expander) to the input level; writes only on change unless ALWAYS.
//@rule allowed_mask is not 0, and default_io_num is below 64 and in allowed_mask. @error ERR_VM_BLK_BAD_SHAPE
//@rule disabled_action is a vm_io_disabled_state_e value. @error ERR_VM_BLK_BAD_SHAPE
//@in 0 level @title Level @value bool
//@in 1 pin @title Pin @description Overrides default_io_num; must be in allowed_mask. @value u32
#define VM_BLOCK_TYPE_IO_SET_LEVEL \
  {.run = vm_blk_io_set_level, .check = vm_verify_io_set_level, .min_in = 1, .min_q = 0, .required_in = 0x1u, .state_len = VM_IO_SET_LEVEL_CUSTOM_LEN}
