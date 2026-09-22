# runIT Firmware Reality & Architecture Summary

## 1. Actual Physical Devices & Owners
Defined in `components/devices/devices/include/devices_owners.h`:
- `OWNER_DEVICE_BASE` (0xD000)
- `OWNER_DEVICE_GPIO_ESP` (0xD001): ESP32-S3 native GPIO and internal ADC
- `OWNER_DEVICE_TPS55289` (0xD002): I2C Buck-Boost DC-DC Regulator
- `OWNER_DEVICE_TCA6424A` (0xD003): 24-bit I/O Expander
- `OWNER_DEVICE_ADS7128` (0xD004): 8-Channel 12-bit ADC with I2C
- `OWNER_DEVICE_PCA9685` (0xD005): 16-Channel 12-bit PWM Expander
- `OWNER_DEVICE_AP33772S` (0xD006): USB-PD Sink Controller
- `OWNER_DEVICE_DAC53202` (0xD007): 2-Channel 10-bit DAC
- `OWNER_DEVICE_INA3221` (0xD008): 3-Channel Current & Voltage Monitor
- `OWNER_DEVICE_DRV8962` (0xD009): Motor / Solenoid Driver

## 2. Real VM Palette & Blocks
Defined in `components/VM/blocks/vm_blocks.h`:
- `VM_BLK_NONE` (0): Reserved
- `VM_BLK_EXPR` (1): RPN float mathematical expression (`vm_block_expr.h`)
- `VM_BLK_EXPR_BIT` (2): RPN uint32 bitwise logic (`vm_block_expr.h`)
- `VM_BLK_IF` (3): 2-way conditional flow branch router (`vm_block_branch.h`)
- `VM_BLK_SWITCH` (4): N-way flow router (`vm_block_branch.h`)
- `VM_BLK_FOR` (5): Repetitive loop span owner (`vm_block_for.h`)
- `VM_BLK_SET` (6): Direct payload copy: source -> target (`vm_block_set.h`)
- `VM_BLK_CLONE` (7): Allocating cloner (`vm_block_clone.h`)
- `VM_BLK_EDGE` (8): Edge detector: rising, falling, or both (`vm_block_edge.h`)
- `VM_BLK_TIMER` (9): TON, TOF, TP pulse timer (`vm_block_timer.h`)
- `VM_BLK_IO_SET_LEVEL` (10): Hardware digital output driver (`vm_block_io_set_level.h`)
- `VM_BLK_IO_TOGGLE` (11): Hardware digital pin toggle (`vm_block_io_toggle.h`)

## 3. Real VM Object System
Defined in `components/VM/core/obj/vm_obj.h`:
- Types: `VM_OBJ_PTR` (1), `VM_OBJ_U8` (2), `VM_OBJ_U32` (3), `VM_OBJ_I32` (4), `VM_OBJ_F` (5), `VM_OBJ_B` (6), `VM_OBJ_STR` (7).
- Tags: Up to 15 characters (`VM_OBJ_NAME_MAX 15`).
- Flags: `mutable`, `upd`, `upd_resetable`, `retentive`, `dynamic`, `usr_protected`.
- Block Accessors: Resolved via `vm_obj_access.h`.

## 4. Binary Codec & Protocol Classes
Defined in `components/codecs/CODECS.MD`:
- Class `0x01` (`dec_sys_contracts.h`):
  - `0x10`-`0x19`: Device lifecycle (`uninstall`, `reset`, `suspend`, `resume`, `freeze`, `sync`)
  - `0x1A`: `sys_device_set_error_handling` (device_id, importance, actions[5])
  - `0x20`-`0x2F`: IO contracts (`set_mode`, `set_level`, `get_level`, `toggle`, `set_pwm_frequency`, `set_pwm_duty`)
  - `0x30`-`0x3F`: Power contracts (`budget_update`, `vreg_set_voltage/current`, `usb_pd_set`)
  - `0x40`-`0x47` (`dec_sys_device_install.h`): Dynamic device creation (`d_pca9685_create`, `d_tps55289_create`, etc.)
- Class `0x04` (`dec_vm_loader.h`):
  - `0x40`: `packet_vm_reset`
  - `0x41`: `packet_vm_open`
  - `0x42`: `packet_vm_add_objs`
  - `0x43`: `packet_vm_set_data`
  - `0x44`: `packet_vm_add_acc` (add accessor)
  - `0x45`: `packet_vm_add_block` (upload block in execution order)
  - `0x47`: `packet_vm_subscribe` (runtime variable subscription)
  - `0x48`: `packet_vm_exec` (run / halt VM)

## 5. System Errors & Dispatcher
Defined in `components/sys_errors/SYS_ERRORS.MD` and `sys_error_codes.h`:
- 10 Error domains: `BASE`, `DEV`, `IO`, `I2C`, `POWER`, `BLE`, `INTERFACE`, `BUFFERS`, `ACTIONS`, `VM`.
- Error chaining with root cause, device attribution, and per-instance policy hooks.

