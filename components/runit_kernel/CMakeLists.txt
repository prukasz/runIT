idf_component_register(
    SRCS "rik_devices.c" "rik_interrupts.c" "rik_modules.c" "rik_main.c"
        "rik_interrupts.c"
        
    INCLUDE_DIRS "include"
    REQUIRES ina3221_wrapper tca6424a_wrapper manager_i2c manager_ble packet_interface mock_tca freertos esp_driver_gpio tps55289 mock_tps 
)
