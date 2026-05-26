idf_component_register(
    SRCS "rik_status_handler.c" "rik_devices.c" "rik_interrupts.c" "rik_modules.c" "rik_main.c"
        "rik_interrupts.c" "rik_logs.c" "rik_scheduler.c" "rik_status_handler.c" "rik_devices_link.c"
        
    INCLUDE_DIRS "include"
    REQUIRES 
    manager_i2c
    manager_ble
    manager_io
    manager_power

    
    
    freertos
    esp_driver_gpio

    mock_ads
    ina3221
    tps55289

    provider_gpio_expander
    provider_adc_expander
    provider_gpio_esp
    provider_current_monitor
    provider_voltage_regulator
    mock_tps
 

    utils
    PRIV_REQUIRES packet_interface config_io config_power
)
