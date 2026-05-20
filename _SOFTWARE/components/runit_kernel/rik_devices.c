#include "manager_i2c.h"
#include "rik_devices.h"
#include "rik_shared.h"
#include "ina3221.h"
#include "tps55289.h"
#include "tca6424a_mock.h"
#include "provider_gpio_expander.h"
#include "manager_io.h"
#include "rtos_utils.h"


#define TCA_MOCK
#define TPS_MOCK

uint8_t rik_ina_id;
uint8_t rik_gpio_expander_id;
uint8_t rik_tps_0_id;
uint8_t rik_tps_1_id;

uint8_t rik_gpio_expander_port_id = 0xFF; //invalid port id as default


static void* gpio_expander_handle = NULL;
static ina3221_handle_t ina3221_handle = NULL;
static tps55289_handle_t tps55289_handle_0 = NULL;
static tps55289_handle_t tps55289_handle_1 = NULL;


#define TAG __FILE_NAME__


void callback(void* arg){
    ESP_LOGI(TAG, "Interrupt received with arg: %p", arg);
    bool pin_level;
    manager_io_enter_mode_dereffered(); // Enter dereffered mode to allow immediate pin state reading
    sys_gpio_read_level(0, 1, &pin_level); // Read the level of pin 0 and print it
    ESP_LOGI(TAG, "Pin level is: %d", pin_level);
     // Exit dereffered mode to resume normal operation
    tca6424a_mock_pin_cfg_t pin_cfg = {
            .pin_mask = 0x01, // Pin 0
            .level = 1
        };
    tca_mock_set_pin_level(&pin_cfg);


    sys_gpio_read_level(0, 1, &pin_level); // Read the level of pin 0 and print it
    ESP_LOGI(TAG, "Pin level is: %d", pin_level);

    manager_io_exit_mode_dereffered();

   
}
    
void test_task_func(void* arg){
    while(1){
        tca6424a_mock_pin_cfg_t pin_cfg = {
            .pin_mask = 0x01, // Pin 0
            .level = 1
        };
        tca_mock_set_pin_level(&pin_cfg); // Simulate interrupt on pin 0
        vTaskDelay(pdMS_TO_TICKS(1000));
        tca6424a_mock_pin_cfg_t pin_cfg_clear = {
            .pin_mask = 0x01, // Pin 0
            .level = 0
        };
        tca_mock_set_pin_level(&pin_cfg_clear);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
R_TASK_DEFINE(test_task, 4096);

status_rep_t rik_gpio_expander_start(uint8_t i2c_addres, bool bus_num){

    #ifndef TCA_MOCK
        STA_RET_ON_ESP_ERR(m_i2c_device_present(bus_num, i2c_addres), OWNER_RIK_DRIVER_INIT_TCA6424A, i2c_addres);
        ESP_LOGI(TAG, "TCA6424A detected on bus %d at address 0x%02X", bus_num ? 1 : 0, i2c_addres);
    #endif
    gpio_expander_handle = provider_gpio_expander_new_handle(i2c_addres);
    if (!gpio_expander_handle) return STA_C(ESP_ERR_NO_MEM, OWNER_RIK_DRIVER_INIT_TCA6424A, i2c_addres);

    i2c_device_config_t* dev_config = provider_gpio_expander_get_i2c_dev_config();
    i2c_master_dev_handle_t*  master_dev_handle = provider_gpio_expander_get_i2c_dev_handle();
    TaskHandle_t task_handle = provider_gpio_expander_get_task_handle();

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
            .mode_func = &_sys_expander_configure_pins,
            .set_func = &_sys_io_expander_set_pin,
            .read_func = &_sys_io_expander_read_pin,
            .toggle_func = &_sys_io_expander_toggle_pin,
            .callback_add_func = &_sys_expander_gpio_set_callback,
            .pwm_set_duty_func = NULL,
            .pwm_set_freq_func = NULL,
            .adc_read_func = NULL,
            .adc_callback_add_func = NULL,
            .protected_pins = 0,
            .dereffered_update = _sys_expander_gpio_delay_updates
        },
        &rik_gpio_expander_port_id
    ));
    tca_mock_set_intr_callback(provider_gpio_expander_int_callback, gpio_expander_handle);
    sys_gpio_set_mode(0, 1, SYS_GPIO_MODE_INPUT); // Set pin 0 as input for PD_INT
    sys_gpio_register_callback(0, 1, SYS_GPIO_MODE_FALLING_EDGE, callback, gpio_expander_handle); // Register callback for falling edge on pin 0
    R_TASK_START(test_task, test_task_func, NULL,5);
    ESP_LOGI(TAG, "GPIO expander started on bus %d with address 0x%02X", bus_num ? 1 : 0, i2c_addres);
    return STA_OK;
}


status_rep_t rik_i2c_start_ina3221(uint8_t i2c_addres, bool bus_num){

    STA_RET_ON_ERR(m_i2c_device_present(bus_num, i2c_addres));
    ESP_LOGI(TAG, "INA3221 detected on bus %d at address 0x%02X", bus_num ? 1 : 0, i2c_addres);

    ina3221_handle = ina3221_new(i2c_addres);
    if (!ina3221_handle) return STA_C(ESP_ERR_NO_MEM, OWNER_RIK_DRIVER_INIT_INA3221, i2c_addres);

    STA_RET_ON_ERR(m_i2c_add_driver(bus_num, ina3221_handle->i2c_device_config,
        &ina3221_handle->i2c_master_dev_handle, (void*)ina3221_handle,
        ina3221_handle->driver_task_handle, true, &rik_ina_id));
    ESP_LOGI(TAG, "INA3221 started on bus %d with address 0x%02X", bus_num ? 1 : 0, i2c_addres);
    return STA_OK;
}

status_rep_t rik_i2c_start_tps55289(uint8_t i2c_adders_0, uint8_t i2c_adders_1, bool bus_num){
    #ifndef TPS_MOCK
        STA_RET_ON_ESP_ERR(m_i2c_device_present(bus_num, i2c_adders_0), OWNER_RIK_DRIVER_INIT_TPS55289, i2c_adders_0);
        STA_RET_ON_ESP_ERR(m_i2c_device_present(bus_num, i2c_adders_1), OWNER_RIK_DRIVER_INIT_TPS55289, i2c_adders_1);
    #endif
    tps55289_handle_0 = tps55289_new(i2c_adders_0);
    if (!tps55289_handle_0) return STA_C(ESP_ERR_NO_MEM, OWNER_RIK_DRIVER_INIT_TPS55289, i2c_adders_0);
    tps55289_handle_1 = tps55289_new(i2c_adders_1);
    if (!tps55289_handle_1) return STA_C(ESP_ERR_NO_MEM, OWNER_RIK_DRIVER_INIT_TPS55289, i2c_adders_1);
    
    STA_RET_ON_ERR(m_i2c_add_driver(bus_num, tps55289_handle_0->i2c_device_config,
        &tps55289_handle_0->i2c_master_dev_handle, (void*)tps55289_handle_0,
        tps55289_handle_0->driver_task_handle, true, &rik_tps_0_id));
    
    STA_RET_ON_ERR(m_i2c_add_driver(bus_num, tps55289_handle_1->i2c_device_config,
        &tps55289_handle_1->i2c_master_dev_handle, (void*)tps55289_handle_1,
        tps55289_handle_1->driver_task_handle, true, &rik_tps_1_id));
    ESP_LOGI(TAG, "TPS55289 started on bus %d with addresses 0x%02X and 0x%02X", bus_num ? 1 : 0, i2c_adders_0, i2c_adders_1);
    return STA_OK;
}