# Header-only: devices.h includes every device's header, so the application names one component instead of one per chip.
# (A device registers itself through its own component, see its CMakeLists.txt: WHOLE_ARCHIVE.)
idf_component_register(
    INCLUDE_DIRS "include"
    REQUIRES device_gpio_esp device_tps55289 device_tca6424a device_ads7128 device_pca9685 device_dac53202 device_drv8962 device_power_mux
             device_ina3221 device_ap33772s device_servo
)
