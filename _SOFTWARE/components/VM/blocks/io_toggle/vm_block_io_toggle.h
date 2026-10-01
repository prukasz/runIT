#pragma once

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

//@data vm_block_io_toggle_data_t
typedef struct __attribute__((aligned(8))) {
  uint64_t allowed_mask;       // @description Permitted pins for dynamic selection @hidden-by-default @let-user-select-available default_io_num @dynamic-input 0
  uint8_t  device_id;          // @description Target device @id device @contract packet_sys_io_toggle_t
  uint8_t  default_io_num;     // @description Static pin when the Pin input is unwired @id pin @device-field device_id
  uint8_t  flags;              // @description VM_IO_TOGGLE_F_* @runtime
  uint8_t  _pad[5];            // @description Align to 16 bytes
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

/* Pins, by index: VM_IN_<BLOCK>_<PIN>. The member's //@in says what the pin carries and which state field it replaces while
   unwired; titles and descriptions are in io_toggle.display.json. */
//#block-enum @alias Toggle Pin Inputs
typedef enum vm_in_io_toggle_e {
  VM_IN_IO_TOGGLE_PIN = 0,  //@in @value u32 @id pin @device-field device_id @overrides default_io_num
} vm_in_io_toggle_e;

/* Load-time check of the block's state; the pin shape comes from the pin enum above. */
//@rule allowed_mask is not 0, and default_io_num is below 64 and in allowed_mask. @error ERR_VM_BLK_BAD_SHAPE
bool vm_verify_io_toggle(vm_block_h b);
/* The body, called every pass.
   Face, titles and descriptions: io_toggle.display.json; the app's io_toggle.content.json is generated from this header. */
//#vm-block VM_BLK_IO_TOGGLE @id 11
//@activation enable-rising
void vm_blk_io_toggle(vm_block_h b);
