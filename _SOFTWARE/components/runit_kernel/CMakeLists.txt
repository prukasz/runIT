idf_component_register(
    SRCS "rik_configs.c" "rik_devices.c" "rik_interrupts.c" "rik_modules.c" "rik_main.c"
        "rik_interrupts.c" "rik_logs.c" "rik_scheduler.c"
        
    INCLUDE_DIRS "include"
    REQUIRES 
    manager_i2c
    manager_ble
    manager_io
    manager_power

    packet_interface
    
    freertos
    esp_driver_gpio

    mock_ads
    ina3221
    tps55289

    provider_gpio_expander
    provider_adc
    mock_tps
 

    utils
)
