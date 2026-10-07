#pragma once

#include "sys_io.h"
#include "vm_block_helpers.h"

/*
 *            ----------------
 *   ->EN     |              | ->ENO
 *            | IO_GET_LEVEL |
 *            |              | ->LEVEL
 *            ----------------
 *
 *  VM_BLK_IO_GET_LEVEL -- Hardware Digital Input Block.
 *  Reads the logic level (HIGH / LOW) of a pin on a target device (ESP GPIO, expander ...) via sys_io_get_level()
 *  and writes it to the Level output as a bool.
 *
 *  Features:
 *  - Reads every pass while enabled; put a PERIODIC's ENO on EN to sample at a fixed rate.
 *  - The pin must already be an input: set its mode in the device's default settings.
 *  - ENO is true when the level was read and written this pass.
 */

//@data vm_block_io_get_level_data_t
typedef struct __attribute__((aligned(4))) {
  uint8_t device_id;  // @description Device to read @id device @contract packet_sys_io_get_level_t
  uint8_t pin;        // @description Pin of the device to read @id pin @device-field device_id
  uint8_t _pad[2];    // @description Align to 4 bytes
} vm_block_io_get_level_data_t;

_Static_assert(sizeof(vm_block_io_get_level_data_t) == 4, "vm_block_io_get_level_data_t must be 4 bytes");
#define VM_IO_GET_LEVEL_CUSTOM_LEN sizeof(vm_block_io_get_level_data_t)

/* Pins, by index: VM_OUT_<BLOCK>_<PIN>. Titles and descriptions are in io_get_level.display.json. */
//#block-enum @alias IO Get Level Outputs
typedef enum vm_out_io_get_level_e {
  VM_OUT_IO_GET_LEVEL_LEVEL = 0,  //@out @value bool @required
} vm_out_io_get_level_e;

/* The state has nothing the pin shape does not cover: no load-time check. */
/* The body, called every pass.
   Face, titles and descriptions: io_get_level.display.json; the app's io_get_level.content.json is generated from this header. */
//#vm-block VM_BLK_IO_GET_LEVEL @id 18
//@activation enabled
void vm_blk_io_get_level(vm_block_h b);
