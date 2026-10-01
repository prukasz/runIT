#include "devices.h"
#include "sys_device.h"

#define OWNER OWNER_DEVICE

// One line per device. A class registered here is created by its type id from the packet router.
err_h devices_register_classes(void) {
  SE_TRY(sys_device_register_class(&g_gpio_esp_class));
  SE_TRY(sys_device_register_class(&g_tca6424a_class));
  SE_TRY(sys_device_register_class(&g_pca9685_class));
  SE_TRY(sys_device_register_class(&g_ina3221_class));
  SE_TRY(sys_device_register_class(&g_tps55289_class));
  SE_TRY(sys_device_register_class(&g_ap33772s_class));
  SE_TRY(sys_device_register_class(&g_dac53202_class));
  SE_TRY(sys_device_register_class(&g_ads7128_class));
  SE_TRY(sys_device_register_class(&g_drv8962_class));
  SE_TRY(sys_device_register_class(&g_servo_class));
  return NULL;
}
