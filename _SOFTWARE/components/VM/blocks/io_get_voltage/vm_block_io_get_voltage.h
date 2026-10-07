#pragma once

#include "sys_io.h"
#include "vm_block_helpers.h"

/*
 *            ------------------
 *   ->EN     |                | ->ENO
 *            | IO_GET_VOLTAGE |
 *            |                | ->VOLTAGE
 *            ------------------
 *
 *  VM_BLK_IO_GET_VOLTAGE -- Hardware Analog Input Block (ADC).
 *  Reads the voltage on a pin of a device that measures it (ADS7128, the ESP ADC) via sys_io_get_voltage() and
 *  writes it to the Voltage output in volts.
 *
 *  Features:
 *  - Reads every pass while enabled; put a PERIODIC's ENO on EN to sample at a fixed rate.
 *  - The pin must be set to the device's analog mode (device default settings).
 *  - ENO is true when the voltage was read and written this pass.
 */

//@data vm_block_io_get_voltage_data_t
typedef struct __attribute__((aligned(4))) {
  uint8_t device_id;  // @description Device to read @id device @contract packet_sys_io_get_voltage_t
  uint8_t pin;        // @description Analog input of the device to read @id pin @device-field device_id
  uint8_t _pad[2];    // @description Align to 4 bytes
} vm_block_io_get_voltage_data_t;

_Static_assert(sizeof(vm_block_io_get_voltage_data_t) == 4, "vm_block_io_get_voltage_data_t must be 4 bytes");
#define VM_IO_GET_VOLTAGE_CUSTOM_LEN sizeof(vm_block_io_get_voltage_data_t)

/* Pins, by index: VM_OUT_<BLOCK>_<PIN>. Titles and descriptions are in io_get_voltage.display.json. */
//#block-enum @alias IO Get Voltage Outputs
typedef enum vm_out_io_get_voltage_e {
  VM_OUT_IO_GET_VOLTAGE_VOLTAGE = 0,  //@out @value f32 @required
} vm_out_io_get_voltage_e;

/* The state has nothing the pin shape does not cover: no load-time check. */
/* The body, called every pass.
   Face, titles and descriptions: io_get_voltage.display.json; the app's io_get_voltage.content.json is generated from this header. */
//#vm-block VM_BLK_IO_GET_VOLTAGE @id 19
//@activation enabled
void vm_blk_io_get_voltage(vm_block_h b);
