#pragma once

/**
 * @file sys_device_types.h
 * @brief Device type IDs (the u16 of CREATE and INFO) - the one place they are
 * assigned, like err_modules.h, so they stay unique across the firmware and the
 * app. Add a new type at the end; never renumber. Drivers start at 0x0040. A
 * duplicate ID fails to compile (sys_device.c). Types not ported yet keep
 * their legacy numbers reserved.
 */

// clang-format off
#define SYS_DEVICE_TYPE_LIST(X)                                          \
  X(GPIO_ESP, 0x0040)  /* components/devices/device_gpio_esp */          \
  X(PCA9685,  0x0041)  /* components/devices/device_pca9685 */           \
  X(TCA6424A, 0x0042)  /* not ported */                                  \
  X(TPS55289, 0x0043)  /* components/devices/device_tps55289 */          \
  X(INA3221,  0x0044)  /* not ported */                                  \
  X(AP33772S, 0x0045)  /* not ported */                                  \
  X(DAC53202, 0x0046)  /* not ported */                                  \
  X(ADS7128,  0x0047)  /* not ported */                                  \
  X(SERVO,    0x0048)  /* not ported */                                  \
  X(DRV8962,  0x0049)  /* not ported */
// clang-format on

#define SYS_DEVICE__TYPE_ENUM(name, id) SYS_DEVICE_TYPE_##name = (id),

enum { SYS_DEVICE_TYPE_LIST(SYS_DEVICE__TYPE_ENUM) };
