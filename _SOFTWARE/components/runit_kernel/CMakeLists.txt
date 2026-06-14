idf_component_register(
    SRCS "rik_status_handler.c" "rik_devices.c" "rik_modules.c" "rik_main.c"
     "rik_logs.c" "rik_scheduler.c" "rik_status_handler.c" "rik_devices_link.c" "rik_system_ctrl.c"
        
    INCLUDE_DIRS "include"
    REQUIRES 
    manager_i2c
    manager_ble
    manager_io
    manager_power
    freertos
    esp_driver_gpio

    PRIV_REQUIRES 
    interface
    utils 
    provider_gpio_expander
    provider_adc_expander
    provider_gpio_esp
    provider_current_monitor
    provider_voltage_regulator
    provider_pwm_expander
    provider_power_delivery
    provider_dac_expander
    vm_new
)
