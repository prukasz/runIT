idf_component_register(SRCS "runit.c"
                    INCLUDE_DIRS "include"
                    REQUIRES freertos log device_gpio_esp device_pca9685 sys_i2c sys_device sys_io status sys_power device_tca6424a device_tps55289 device_ina3221 device_dac53202 device_ap33772s device_ads7128 ble sys_interface sys_h_bridge device_drv8962)
