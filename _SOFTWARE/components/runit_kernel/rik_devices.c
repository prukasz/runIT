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
#include "provider_dac_expander.h"
#include "sdkconfig.h"
#include "tca6424a_mock.h"
#include "ads7128_mock.h"
#include "provider_pwm_expander.h"
#include "provider_power_delivery.h"

#define TAG __FILE_NAME__

uint8_t rik_current_monitor_id;
uint8_t rik_gpio_expander_id;
uint8_t rik_vreg0_id;
uint8_t rik_vreg1_id;
uint8_t rik_adc_expander_id;
uint8_t rik_pwm_expander_id;
uint8_t rik_power_delivery_id;
uint8_t rik_dac_expander_id;

uint8_t rik_gpio_expander_port_id = 0xFF; 
uint8_t rik_adc_expander_port_id = 0xFF; 
uint8_t rik_gpio_esp_port_id = 0xFF;
uint8_t rik_pwm_expander_port_id = 0xFF; 

static void* gpio_expander_handle = NULL;
static void* adc_expander_handle = NULL;
static void* current_monitor_handle = NULL;
static void* vreg0_handle = NULL;
static void* vreg1_handle = NULL;
static void* pwm_expander_handle = NULL;
static void* power_delivery_handle = NULL;
static void* dac_expander_handle = NULL;


status_rep_t rik_p_gpio_esp_start(void){
    STA_R_ON_ERR(p_gpio_esp_init());
    STA_R_ON_ERR(manager_io_register_new_port(
        &(io_port_dispatch_t){
            .mode_func = &p_gpio_esp_set_pin_mode,
            .set_func = &p_gpio_esp_set_level,
            .read_func = &p_gpio_esp_read_level,
            .toggle_func = &p_gpio_esp_pin_toggle,
            .reset_pin_func = &p_gpio_esp_reset_pin,
            .callback_add_func = &p_gpio_esp_register_callback,
            .pwm_set_duty_func = NULL,
            .pwm_set_freq_func = NULL,
            .adc_read_func = &p_gpio_esp_adc_read,
            .adc_callback_add_func = &p_gpio_esp_adc_register_callback,
            .freeze = &p_gpio_esp_freeze_updates,
            .reset = &p_gpio_esp_reset_all,
            .protected_pins = 0,
        },
        &rik_gpio_esp_port_id
    ));

    p_gpio_esp_set_port_id(rik_gpio_esp_port_id);
    ESP_LOGI(TAG, "GPIO ESP provider started with port ID %d", rik_gpio_esp_port_id);
    return STA_OK;
}


#undef OWNER
#define OWNER OWNER_PROVIDER_POWER_DELIVERY
status_rep_t rik_p_power_delivery_start(uint8_t i2c_addr, bool bus_num){

    STA_R_ON_ERR(m_i2c_device_present(bus_num, i2c_addr));
    ESP_LOGI(TAG, "AP33772S detected on bus %d at address 0x%02X", bus_num ? 1 : 0, i2c_addr);

    power_delivery_handle = p_power_delivery_new();
    CHECK_HANDLE_R(power_delivery_handle);

    i2c_device_config_t* dev_config = p_power_delivery_get_i2c_dev_config();
    i2c_master_dev_handle_t* master_dev_handle = p_power_delivery_get_i2c_dev_handle();
    TaskHandle_t task_handle = p_power_delivery_get_task_handle();

    STA_R_ON_ERR(m_i2c_add_driver(
        bus_num,
        *dev_config,
        master_dev_handle,
        power_delivery_handle,
        task_handle,
        true,
        &rik_power_delivery_id
    ));
    status_rep_t status = p_power_delivery_begin();
    if(STA_P_ON_ESP_ERR(status)){
        STA_RP(STA_C(PWR_ERR_UPDATE_FAILED, OWNER_MANAGER_PWR_CONFIG_PD, status.e_code));
    }
    STA_RP_ON_ERR(status); 
    ESP_LOGI(TAG, "Power delivery (AP33772S) started on bus %d with address 0x%02X", bus_num ? 1 : 0, i2c_addr);
    return STA_OK;
}
extern void p_gpio_expander_intr_pin_callback(void* arg);
#undef OWNER
#define OWNER OWNER_RIK_DRIVER_INIT_GPIO_EXPANDER
status_rep_t rik_p_gpio_expander_start(uint8_t i2c_addres, bool bus_num){

#if !CONFIG_USE_MOCK_TCA6424A
        STA_RP_ON_ERR(m_i2c_device_present(bus_num, i2c_addres));
        ESP_LOGI(TAG, "TCA6424A detected on bus %d at address 0x%02X", bus_num ? 1 : 0, i2c_addres);
#endif
    gpio_expander_handle = p_gpio_expander_new(i2c_addres);
    CHECK_HANDLE_R(gpio_expander_handle);

    i2c_device_config_t* dev_config = p_gpio_expander_get_i2c_dev_config();
    i2c_master_dev_handle_t*  master_dev_handle = p_gpio_expander_get_i2c_dev_handle();
    TaskHandle_t task_handle = p_gpio_expander_get_task_handle();

    STA_R_ON_ERR(m_i2c_add_driver(
        bus_num, 
        *dev_config,
        master_dev_handle,
        gpio_expander_handle,
        task_handle,
        true, 
        &rik_gpio_expander_id
    ));

    
    STA_R_ON_ERR(manager_io_register_new_port(
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
            .protected_pins = 0x0,
            .reset_pin_func = &p_gpio_expander_reset_pin,
            .reset = NULL,
            .freeze = p_gpio_expander_freeze
        },
        &rik_gpio_expander_port_id
    ));

    p_gpio_expander_set_port_id(rik_gpio_expander_port_id);
#if CONFIG_USE_MOCK_TCA6424A
        tca_mock_set_intr_callback(p_gpio_expander_intr_pin_callback, gpio_expander_handle);
#endif
    ESP_LOGI(TAG, "GPIO expander started on bus %d with address 0x%02X", bus_num ? 1 : 0, i2c_addres);
    return STA_OK;
}

#undef OWNER
#define OWNER OWNER_RIK_DRIVER_INIT_CURRENT_MONITOR
status_rep_t rik_current_monitor_start(uint8_t i2c_addres, bool bus_num){
    STA_R_ON_ERR(m_i2c_device_present(bus_num, i2c_addres));
    ESP_LOGI(TAG, "INA3221 detected on bus %d at address 0x%02X", bus_num ? 1 : 0, i2c_addres);

    current_monitor_handle = p_current_monitor_new(i2c_addres);
    CHECK_HANDLE_R(current_monitor_handle);

    i2c_device_config_t* dev_config = p_current_monitor_get_i2c_dev_config();
    i2c_master_dev_handle_t*  master_dev_handle = p_current_monitor_get_i2c_dev_handle();
    TaskHandle_t task_handle = p_current_monitor_get_task_handle();

    STA_R_ON_ERR(m_i2c_add_driver(
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


#undef OWNER
#define OWNER OWNER_RIK_DRIVER_INIT_VREG
status_rep_t rik_p_vreg_start(uint8_t i2c_adders_0, uint8_t i2c_adders_1, bool bus_num){

    i2c_device_config_t* dev_config;
    i2c_master_dev_handle_t*  master_dev_handle;
    TaskHandle_t task_handle;

#if CONFIG_CONNECT_TPS55289_0
    vreg0_handle = p_vreg_0_new();
    CHECK_HANDLE_R(vreg0_handle);
    dev_config = p_vreg_get_i2c_dev_config(0);
    master_dev_handle = p_vreg_get_i2c_dev_handle(0);
    task_handle = p_vreg_get_task_handle(0);

    STA_R_ON_ERR(m_i2c_add_driver(
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
    CHECK_HANDLE_R(vreg1_handle);

    dev_config = p_vreg_get_i2c_dev_config(1);
    master_dev_handle = p_vreg_get_i2c_dev_handle(1);
    task_handle = p_vreg_get_task_handle(1);

    STA_R_ON_ERR(m_i2c_add_driver(
        bus_num, 
        *dev_config,
        master_dev_handle,
        vreg1_handle,
        task_handle, 
        true,
        &rik_vreg1_id
    ));

    ESP_LOGI(TAG, "TPS55289 regulator 1 started on bus %d with address 0x%02X", bus_num ? 1 : 0, i2c_adders_1);
#endif
#if !CONFIG_USE_MOCK_TPS55289 && CONFIG_CONNECT_TPS55289_0
        STA_R_ON_ERR(m_i2c_device_present(bus_num, i2c_adders_0));
#endif
#if !CONFIG_USE_MOCK_TPS55289 && CONFIG_CONNECT_TPS55289_1
        STA_R_ON_ERR(m_i2c_device_present(bus_num, i2c_adders_1));
#endif
    return STA_OK;
}


extern void p_adc_expander_intr_pin_callback(void* arg);

#undef OWNER
#define OWNER OWNER_RIK_DRIVER_INIT_ADC_EXPANDER
status_rep_t rik_adc_expander_start(uint8_t i2c_addres, bool bus_num){

    #if !CONFIG_USE_MOCK_ADS7128
        STA_R_ON_ERR(m_i2c_device_present(bus_num, i2c_addres));
        ESP_LOGI(TAG, "ADS7128 detected on bus %d at address 0x%02X", bus_num ? 1 : 0, i2c_addres);
    #endif

    adc_expander_handle = p_adc_expander_new_handle(i2c_addres);
    CHECK_HANDLE_R(adc_expander_handle);

    i2c_device_config_t* dev_config = p_adc_expander_get_i2c_dev_config();
    i2c_master_dev_handle_t*  master_dev_handle = p_adc_expander_get_i2c_dev_handle();
    TaskHandle_t task_handle = p_adc_expander_get_task_handle();

    STA_R_ON_ERR(m_i2c_add_driver(
        bus_num, 
        *dev_config,
        master_dev_handle,
        adc_expander_handle,
        task_handle,
        true, 
        &rik_adc_expander_id
    ));

    STA_R_ON_ERR(manager_io_register_new_port(
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
            .reset = &p_adc_expander_reset_all,
            .protected_pins = 0x0,
        },
        &rik_adc_expander_port_id
    ));
    
    p_adc_expander_set_port_id(rik_adc_expander_port_id);
 
#if CONFIG_USE_MOCK_ADS7128
    ads_mock_add_alert_callback(p_adc_expander_intr_pin_callback, adc_expander_handle);
#endif
    ESP_LOGI(TAG, "ADC expander started on bus %d with address 0x%02X", bus_num ? 1 : 0, i2c_addres);
    return STA_OK;
}

status_rep_t p_pwm_expadner_start(uint8_t i2c_addres, bool bus_num){
    
    STA_R_ON_ERR(m_i2c_device_present(bus_num, i2c_addres));
    ESP_LOGI(TAG, "PCA9685 detected on bus %d at address 0x%02X", bus_num ? 1 : 0, i2c_addres);
    pwm_expander_handle = p_pca9685_new(i2c_addres);
    i2c_device_config_t* dev_config = p_pca9685_get_i2c_dev_config();
    i2c_master_dev_handle_t*  master_dev_handle = p_pca9685_get_i2c_dev_handle();
    TaskHandle_t task_handle = p_pca9685_get_task_handle();

    STA_R_ON_ERR(m_i2c_add_driver(
        bus_num, 
        *dev_config,
        master_dev_handle,
        pwm_expander_handle,
        task_handle,
        true, 
        &rik_pwm_expander_id
    ));

    STA_R_ON_ERR(manager_io_register_new_port(
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
            .reset = p_pca9685_reset,
            .protected_pins = 0x0,
        },
        &rik_pwm_expander_port_id

    ));
    p_pca9685_configure();
    ESP_LOGI(TAG, "PWM expander started on bus %d with address 0x%02X", bus_num ? 1 : 0, i2c_addres);
    return STA_OK;
}

status_rep_t rik_p_dac_expander_start(uint8_t i2c_addres, bool bus_num){
    #if CONFIG_CONNECT_DAC53202
    STA_R_ON_ERR(m_i2c_device_present(bus_num, i2c_addres));
    ESP_LOGI(TAG, "DAC53202 detected on bus %d at address 0x%02X", bus_num ? 1 : 0, i2c_addres);
    dac_expander_handle = p_dac_expander_new(i2c_addres);
    i2c_device_config_t* dev_config = p_dac_expander_get_i2c_dev_config();
    i2c_master_dev_handle_t*  master_dev_handle = p_dac_expander_get_i2c_dev_handle();
    TaskHandle_t task_handle = p_dac_expander_get_task_handle();

    STA_R_ON_ERR(m_i2c_add_driver(
        bus_num, 
        *dev_config,
        master_dev_handle,
        dac_expander_handle,
        task_handle,
        true, 
        &rik_dac_expander_id
    ));
    p_dac_expander_configure();
    ESP_LOGI(TAG, "DAC expander started on bus %d with address 0x%02X", bus_num ? 1 : 0, i2c_addres);
    #endif
    return STA_OK;
}
