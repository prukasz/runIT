#pragma once

#include "sys_io.h"
#include "vm_block_helpers.h"

/*
 *            -------------------
 *   ->EN     |                 | ->ENO
 *  (->LEVEL) |   IO_SET_LEVEL  |
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
 *  - Static level (default_level, HIGH by default) driven while the block is active; a wired Level overrides it.
 *  - Configurable state when not active: FORCE_LOW (0, default), FORCE_HIGH (1) or HOLD (do nothing).
 *  - ENO reflects successful hardware assertion this pass.
 */

//#ref-enum @alias IO Disabled Action
typedef enum {
  VM_IO_DISABLED_FORCE_LOW  = 0, // Assert LOW (0) when not active (the default: 0 is what a fresh block holds)
  VM_IO_DISABLED_FORCE_HIGH = 1, // Assert HIGH (1) when not active
  VM_IO_DISABLED_HOLD       = 2, // Keep the last state when not active
} vm_io_disabled_state_e;

//#ref-enum @alias IO Static Level
typedef enum {
  VM_IO_LEVEL_HIGH = 0, // Drive HIGH (1) while active (the default: 0 is what a fresh block holds)
  VM_IO_LEVEL_LOW  = 1, // Drive LOW (0) while active
} vm_io_level_e;

#define VM_IO_SET_F_INITIALIZED (1u << 0) // Pin has been driven at least once
#define VM_IO_SET_F_LAST_LEVEL  (1u << 1) // Cached last level written (0 or 1)

typedef struct __attribute__((aligned(8))) {
  uint64_t allowed_mask;       // Permitted pins for dynamic selection @hidden-by-default @let-user-select-available default_io_num @dynamic-input 1
  uint8_t  device_id;          // Target device @id device @contract packet_sys_io_set_level_t
  uint8_t  default_io_num;     // Static pin when the Pin input is unwired @id pin @device-field device_id
  uint8_t  when_not_active;    // What the pin does when the block is not active @enum-ref vm_io_disabled_state_e
  uint8_t  flags;              // VM_IO_SET_F_* cache bits @runtime
  uint8_t  last_pin;           // Cached last pin written @runtime
  uint8_t  default_level;      // Select level when block active @enum-ref vm_io_level_e
  uint8_t  _pad[2];            // Align to 16 bytes
} vm_block_io_set_level_data_t;

_Static_assert(sizeof(vm_block_io_set_level_data_t) == 16, "vm_block_io_set_level_data_t must be 16 bytes");
#define VM_IO_SET_LEVEL_CUSTOM_LEN sizeof(vm_block_io_set_level_data_t)

/**
 * @brief Initialize IO set level configuration in block custom data.
 */
static inline void vm_block_io_set_level_init_data(void* buffer, uint8_t device_id, uint8_t default_pin,
                                                   uint64_t allowed_mask, vm_io_disabled_state_e when_not_active,
                                                   vm_io_level_e default_level) {
  const vm_block_io_set_level_data_t data = {
      .allowed_mask    = allowed_mask,
      .device_id       = device_id,
      .default_io_num  = default_pin,
      .when_not_active = (uint8_t)when_not_active,
      .last_pin        = default_pin,
      .default_level   = (uint8_t)default_level,
  };
  memcpy(buffer, &data, sizeof(data));
}

#define VM_IO_SET_IN_LEVEL 0u // optional: boolean / scalar level (default_level while unwired)
#define VM_IO_SET_IN_PIN   1u // optional: dynamic pin number (0..63)

/* Load-time check of the block's state; the pin shape is checked from the //@in / //@out directives below. */
bool vm_verify_io_set_level(vm_block_h b);
/* The body, called every pass. */
void vm_blk_io_set_level(vm_block_h b);

//#vm-block VM_BLK_IO_SET_LEVEL @id 10
//@title Set Pin Level
//@category io
//@activation enabled Runs every pass while enabled.
//@data vm_block_io_set_level_data_t
//@block-description Drives a pin on an IO device (ESP GPIO, expander) to the input level; writes only on change.
//@header {title} | {pin}
//@rule allowed_mask is not 0, and default_io_num is below 64 and in allowed_mask. @error ERR_VM_BLK_BAD_SHAPE
//@rule when_not_active is a vm_io_disabled_state_e value. @error ERR_VM_BLK_BAD_SHAPE
//@rule default_level is a vm_io_level_e value. @error ERR_VM_BLK_BAD_SHAPE
//@always-detailed
//@in 0 level @title Level @description Overrides default_level; the level the pin is driven to while active. @value bool @overrides default_level @macro VM_IO_SET_IN_LEVEL
//@in 1 pin @title Pin @description Overrides default_io_num; must be in allowed_mask. @value u32 @id pin @device-field device_id @overrides default_io_num @macro VM_IO_SET_IN_PIN
