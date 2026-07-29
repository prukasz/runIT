idf_component_register(
    INCLUDE_DIRS "decoders" "encoders"
    REQUIRES sys_errors ble sys_device sys_io sys_power
             device_gpio_esp device_pca9685 device_tca6424a device_tps55289
             device_ina3221 device_ap33772s device_dac53202 device_ads7128
)
