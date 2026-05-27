#include "manager_i2c.h"
#include "rik_devices.h"
#include "manager_io.h"
#include "rtos_utils.h"
#include "rik_shared.h"
#include "provider_gpio_expander.h"
#include "provider_adc_expander.h"
#include "provider_gpio_esp.h"
#include "provider_current_monitor.h"
#include "provider_voltage_regulator.h"
#include "sdkconfig.h"
#include "tca6424a_mock.h"
#include "ads7128_mock.h"
#include "provider_pwm_expander.h"

#define TAG __FILE_NAME__

uint8_t rik_current_monitor_id;
uint8_t rik_gpio_expander_id;
uint8_t rik_vreg0_id;
uint8_t rik_vreg1_id;
uint8_t rik_adc_expander_id;
uint8_t rik_pwm_expander_id;


uint8_t rik_gpio_expander_port_id = 0xFF; //invalid port id as default
uint8_t rik_adc_expander_port_id = 0xFF; //invalid port id as default
uint8_t rik_gpio_esp_port_id = 0xFF; //invalid port id as default
uint8_t rik_pwm_expander_port_id = 0xFF; //invalid port id as default

static void* gpio_expander_handle = NULL;
static void* adc_expander_handle = NULL;
static void* current_monitor_handle = NULL;
static void* vreg0_handle = NULL;
static void* vreg1_handle = NULL;
static void* pwm_expander_handle = NULL;


status_rep_t rik_gpio_esp_start(void){
    esp_err_t err = p_gpio_esp_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize GPIO ESP provider");
        return STA_C(err, OWNER_RIK_DRIVER_INIT, 0);
    }
    STA_RET_ON_ERR(manager_io_register_new_port(
        &(io_port_dispatch_t){
            .mode_func = &p_gpio_esp_set_pin_mode,
            .set_func = &p_gpio_esp_set_level,
            .read_func = &p_gpio_esp_read_level,
            .toggle_func = &p_gpio_esp_pin_toggle,
            .callback_add_func = &p_gpio_esp_register_callback,
            .pwm_set_duty_func = NULL,
            .pwm_set_freq_func = NULL,
            .adc_read_func = &p_gpio_esp_adc_read,
            .adc_callback_add_func = &p_gpio_esp_adc_register_callback,
            .freeze = &p_gpio_esp_freeze_updates,
            .protected_pins = 0,
        },
        &rik_gpio_esp_port_id
    ));
    
    ESP_LOGI(TAG, "GPIO ESP provider started with port ID %d", rik_gpio_esp_port_id);
    return STA_OK;
}

extern void p_gpio_expander_intr_pin_callback(void* arg);

status_rep_t rik_gpio_expander_start(uint8_t i2c_addres, bool bus_num){

#if !CONFIG_USE_MOCK_TCA6424A
        if(!STA_IS_OK(m_i2c_device_present(bus_num, i2c_addres))) {
            return STA_C(ERR_I2C_DEV_NOT_FOUND, OWNER_RIK_DRIVER_INIT_TCA6424A, i2c_addres);
        }
        ESP_LOGI(TAG, "TCA6424A detected on bus %d at address 0x%02X", bus_num ? 1 : 0, i2c_addres);
#endif
    gpio_expander_handle = p_gpio_expander_new(i2c_addres);
    if (!gpio_expander_handle) return STA_C(ESP_ERR_NO_MEM, OWNER_RIK_DRIVER_INIT_TCA6424A, i2c_addres);

    i2c_device_config_t* dev_config = p_gpio_expander_get_i2c_dev_config();
    i2c_master_dev_handle_t*  master_dev_handle = p_gpio_expander_get_i2c_dev_handle();
    TaskHandle_t task_handle = p_gpio_expander_get_task_handle();

    STA_RET_ON_ERR(m_i2c_add_driver(
        bus_num, 
        *dev_config,
        master_dev_handle,
        gpio_expander_handle,
        task_handle,
        true, 
        &rik_gpio_expander_id
    ));

    
    STA_RET_ON_ERR(manager_io_register_new_port(
        &(io_port_dispatch_t){
            .mode_func = &p_gpio_expander_configure_pins,
            .set_func = &p_gpio_expander_set_pins,
            .read_func = &p_gpio_epander_read_pin,
            .toggle_func = &p_gpio_expander_toggle_pin,
            .callback_add_func = &p_gpio_expander_set_pin_callback,
            .pwm_set_duty_func = NULL,
            .pwm_set_freq_func = NULL,
            .adc_read_func = NULL,
            .adc_callback_add_func = NULL,
            .protected_pins = 0,
            .freeze = p_gpio_expander_freeze
        },
        &rik_gpio_expander_port_id
    ));
#if CONFIG_USE_MOCK_TCA6424A
        tca_mock_set_intr_callback(p_gpio_expander_intr_pin_callback, gpio_expander_handle);
#endif
    ESP_LOGI(TAG, "GPIO expander started on bus %d with address 0x%02X", bus_num ? 1 : 0, i2c_addres);
    return STA_OK;
}


status_rep_t rik_current_monitor_start(uint8_t i2c_addres, bool bus_num){

    if (!STA_IS_OK(m_i2c_device_present(bus_num, i2c_addres))) {
        STA_RP(STA_C(ERR_I2C_DEV_NOT_FOUND, OWNER_RIK_DRIVER_INIT_CURRENT_MONITOR, i2c_addres));
    }
    ESP_LOGI(TAG, "INA3221 detected on bus %d at address 0x%02X", bus_num ? 1 : 0, i2c_addres);

    current_monitor_handle = p_current_monitor_new(i2c_addres);

    i2c_device_config_t* dev_config = p_current_monitor_get_i2c_dev_config();
    i2c_master_dev_handle_t*  master_dev_handle = p_current_monitor_get_i2c_dev_handle();
    TaskHandle_t task_handle = p_current_monitor_get_task_handle();

    

    if (!current_monitor_handle) return STA_C(ESP_ERR_NO_MEM, OWNER_RIK_DRIVER_INIT_CURRENT_MONITOR, i2c_addres);

    STA_RET_ON_ERR(m_i2c_add_driver(
        bus_num,
        *dev_config,
        master_dev_handle,
        current_monitor_handle,
        task_handle,
        true,
        &rik_current_monitor_id
    ));
    ESP_LOGI(TAG, "INA3221 started on bus %d with address 0x%02X", bus_num ? 1 : 0, i2c_addres);
    return STA_OK;
}

status_rep_t rik_regs_start(uint8_t i2c_adders_0, uint8_t i2c_adders_1, bool bus_num){
#if !CONFIG_USE_MOCK_TPS55289
        STA_RET_ON_ERR(m_i2c_device_present(bus_num, i2c_adders_0));
        STA_RET_ON_ERR(m_i2c_device_present(bus_num, i2c_adders_1));
#endif


    i2c_device_config_t* dev_config;
    i2c_master_dev_handle_t*  master_dev_handle;
    TaskHandle_t task_handle;

#if CONFIG_CONNECT_TPS55289_0
    vreg0_handle = p_vreg_0_new();
    if (!vreg0_handle) return STA_C(ESP_ERR_NO_MEM, OWNER_RIK_DRIVER_INIT_TPS55289, i2c_adders_0);
    
    dev_config = p_vreg_get_i2c_dev_config(0);
    master_dev_handle = p_vreg_get_i2c_dev_handle(0);
    task_handle = p_vreg_get_task_handle(0);

    STA_RET_ON_ERR(m_i2c_add_driver(
        bus_num, 
        *dev_config,
        master_dev_handle,
        vreg0_handle,
        task_handle, 
        true,
        &rik_vreg0_id
    ));
    ESP_LOGI(TAG, "TPS55289 regulator 0 started on bus %d with address 0x%02X", bus_num ? 1 : 0, i2c_adders_0);
#endif

#if CONFIG_CONNECT_TPS55289_1
    vreg1_handle = p_vreg_1_new();
    if (!vreg1_handle) return STA_C(ESP_ERR_NO_MEM, OWNER_RIK_DRIVER_INIT_TPS55289, i2c_adders_1);
    
    dev_config = p_vreg_get_i2c_dev_config(1);
    master_dev_handle = p_vreg_get_i2c_dev_handle(1);
    task_handle = p_vreg_get_task_handle(1);

    STA_RET_ON_ERR(m_i2c_add_driver(
        bus_num, 
        *dev_config,
        master_dev_handle,
        vreg0_handle,
        task_handle, 
        true,
        &rik_vreg1_id
    ));

    ESP_LOGI(TAG, "TPS55289 regulator 1 started on bus %d with address 0x%02X", bus_num ? 1 : 0, i2c_adders_1);
#endif
    return STA_OK;
}

extern void p_adc_expander_intr_pin_callback(void* arg);

status_rep_t rik_adc_expander_start(uint8_t i2c_addres, bool bus_num){
    adc_expander_handle = p_adc_expander_new_handle(i2c_addres);
    if (!adc_expander_handle) return STA_C(ESP_ERR_NO_MEM, 0, i2c_addres);

    i2c_device_config_t* dev_config = p_adc_expander_get_i2c_dev_config();
    i2c_master_dev_handle_t*  master_dev_handle = p_adc_expander_get_i2c_dev_handle();
    TaskHandle_t task_handle = p_adc_expander_get_task_handle();

    STA_RET_ON_ERR(m_i2c_add_driver(
        bus_num, 
        *dev_config,
        master_dev_handle,
        adc_expander_handle,
        task_handle,
        true, 
        &rik_adc_expander_id
    ));

    STA_RET_ON_ERR(manager_io_register_new_port(
        &(io_port_dispatch_t){
            .mode_func = NULL,
            .set_func = NULL,
            .read_func = NULL,
            .toggle_func = NULL,
            .callback_add_func = NULL,
            .pwm_set_duty_func = NULL,
            .pwm_set_freq_func = NULL,
            .adc_read_func = &p_adc_expander_read_voltage,
            .adc_callback_add_func = &p_adc_expander_register_callback,
            .freeze = &p_adc_expander_freeze,
            .protected_pins = 0,
        },
        &rik_adc_expander_port_id
    ));

    ads_mock_add_alert_callback(p_adc_expander_intr_pin_callback, adc_expander_handle);
    ESP_LOGI(TAG, "ADC expander started on bus %d with address 0x%02X", bus_num ? 1 : 0, i2c_addres);
    return STA_OK;
}

status_rep_t p_pwm_expadner_start(uint8_t i2c_addres, bool bus_num){
    pwm_expander_handle = p_pca9685_new(i2c_addres);
    if (!STA_IS_OK(m_i2c_device_present(bus_num, i2c_addres))) {
        STA_RP(STA_C(ERR_I2C_DEV_NOT_FOUND, OWNER_RIK_DRIVER_INIT, i2c_addres));
    }
    ESP_LOGI(TAG, "PCA9685 detected on bus %d at address 0x%02X", bus_num ? 1 : 0, i2c_addres);
    i2c_device_config_t* dev_config = p_pca9685_get_i2c_dev_config();
    i2c_master_dev_handle_t*  master_dev_handle = p_pca9685_get_i2c_dev_handle();
    TaskHandle_t task_handle = p_pca9685_get_task_handle();

    STA_RET_ON_ERR(m_i2c_add_driver(
        bus_num, 
        *dev_config,
        master_dev_handle,
        adc_expander_handle,
        task_handle,
        true, 
        &rik_pwm_expander_id
    ));

    STA_RET_ON_ERR(manager_io_register_new_port(
        &(io_port_dispatch_t){
            .mode_func = NULL,
            .set_func = p_pca9685_set_pins,
            .read_func = NULL,
            .toggle_func = p_pca9685_toggle_pins,
            .callback_add_func = NULL,
            .pwm_set_duty_func = p_pca9685_pwm_set_duty,
            .pwm_set_freq_func = p_pca9685_pwm_set_freq,
            .adc_read_func = NULL,
            .adc_callback_add_func = NULL,
            .freeze = p_pca9685_freeze,
            .protected_pins = 0,
        },
        &rik_pwm_expander_port_id

    ));
    //void p_pca9685_notify_to_update(void);   
    ESP_LOGI(TAG, "PWM expander started on bus %d with address 0x%02X", bus_num ? 1 : 0, i2c_addres);
    return STA_OK;
}


