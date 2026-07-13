# Agent Rules & Instructions

## Architecture & Code Guidelines
- **Error Handling**: Use `status_rep_t` for system submodule errors. Check/propagate using `status.h` macros like: `STA_IS_OK`, `STA_RP`, `STA_C`.
- **Device Design**: Create separate `device_adapter` and `device_driver`.
- **Contracts**: Fit new device contracts under `sys_io`, `sys_power`, etc., or create submodules. Provide all `sys_device` contract functions.
- **Simplification**: Expose a single creation function `d_xxx_create(args)` to bind adapter functions to contracts in `device_install`.
- **Macros**: In adapters, use `IF_PIN(int_pin_num)`, `CHECK_HANDLE_R(handle)`, and `CHECK_ESP_CALL_R(esp_err)`.
- **Testing**: Add new user-facing `sys_xxx` functions to `codecs/decoders` for remote testing.
- **Double Buffering**: For I/O devices, implement freeze/resume functions if needed.

## Verified ESP-IDF Build Command
Run this exact command to compile the project under ESP-IDF 6.0:
```powershell
powershell -Command ". C:\esp\v6.0.1\esp-idf\export.ps1; idf.py build"
```
