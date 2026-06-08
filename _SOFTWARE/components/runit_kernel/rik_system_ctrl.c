#include "rik_system_ctrl.h"
#include "manager_io.h"
#include "manager_power.h"
#include "rik_shared.h"
#include "rtos_utils.h"
#include "rik_devices_link.h"
#include "vm_demo.h"
#include "sdkconfig.h"
#include <inttypes.h>

#define TAG __FILE_NAME__

extern TaskHandle_t vm_demo_task;

static rik_sys_ctrl_power_cfg_t system_ctrl_config = {0};

#define OFF_GPIO_EXPANDER_MASK \
    SYS_IO_GET_MASK(RIK_IO_PIN_DRV1_SLEEP) | SYS_IO_GET_MASK(RIK_IO_PIN_DRV2_SLEEP) | \
    SYS_IO_GET_MASK(RIK_IO_PIN_REGA_EN) | SYS_IO_GET_MASK(RIK_IO_PIN_REGB_EN) | \
    SYS_IO_GET_MASK(RIK_IO_PIN_DRV2_VMA_REGA) | SYS_IO_GET_MASK(RIK_IO_PIN_DRV2_VMB_VSUP) | \
    SYS_IO_GET_MASK(RIK_IO_PIN_DRV1_VMA_VSUP) | SYS_IO_GET_MASK(RIK_IO_PIN_DRV1_VMB_REGB) | \
    SYS_IO_GET_MASK(RIK_IO_PIN_PWM_EXPANDER_nOE)

#define OFF_GPIO_ESP_MASK \
    SYS_IO_GET_MASK(RIK_IO_PIN_DRV_1_IN1) | SYS_IO_GET_MASK(RIK_IO_PIN_DRV_1_IN2) | \
    SYS_IO_GET_MASK(RIK_IO_PIN_DRV_1_IN3) | SYS_IO_GET_MASK(RIK_IO_PIN_DRV_1_IN4) | \
    SYS_IO_GET_MASK(RIK_IO_PIN_DRV_1_EN1) | SYS_IO_GET_MASK(RIK_IO_PIN_DRV_1_EN2) | \
    SYS_IO_GET_MASK(RIK_IO_PIN_DRV_1_EN3) | SYS_IO_GET_MASK(RIK_IO_PIN_DRV_1_EN4)

/* Registered VM callbacks (set via rik_register_vm_sys_*) */
static void (*s_vm_sys_power_cb)(void *arg) = NULL;
static void (*s_vm_sys_adc_cb)(void *arg) = NULL;
static void (*s_vm_sys_gpio_cb)(void *arg) = NULL;

void rik_register_vm_sys_power(void (*cb)(void *arg)){
    s_vm_sys_power_cb = cb;
}

void rik_register_vm_sys_adc(void (*cb)(void *arg)){
    s_vm_sys_adc_cb = cb;
}

void rik_register_vm_sys_gpio(void (*cb)(void *arg)){
    s_vm_sys_gpio_cb = cb;
}

void sys_freeze(void){
    manager_io_freeze(true);
    manager_pwr_freeze(true);
}

void sys_unfreeze(void){
    manager_io_freeze(false);
    manager_pwr_freeze(false);
    R_EVENT_CLEAR(rik_events_wired, EVENT_BIT_I2C_DONE_0 | EVENT_BIT_I2C_DONE_1);
    R_EVENT_SET(rik_events_wired, EVENT_BIT_I2C_PROCESS_0|EVENT_BIT_I2C_PROCESS_1);
    R_EVENT_AWAIT_ALL(rik_events_wired,EVENT_BIT_I2C_DONE_0 | EVENT_BIT_I2C_DONE_1, MSEC(200));
}

void sys_stop_or_resume(bool stop, bool reset_prev_state){
    static uint64_t prev_state;
    if (reset_prev_state) prev_state = 0;
    STATUS_SUSPEND();
    sys_unfreeze();
    if(stop){
        sys_gpio_read_level(rik_gpio_expander_port_id, OFF_GPIO_EXPANDER_MASK, &prev_state);
        sys_gpio_set_level(rik_gpio_expander_port_id, OFF_GPIO_EXPANDER_MASK, 0);
        ESP_LOGI(TAG, "Stopped devices, saved level %016" PRIx64, prev_state);
    }else{
        sys_gpio_set_level(rik_gpio_expander_port_id, OFF_GPIO_EXPANDER_MASK, prev_state);
        ESP_LOGI(TAG, "Resumed devices, level %016" PRIx64, prev_state);
    }
    STATUS_RESUME();
}

void sys_devices_default_config(void){
    STATUS_SUSPEND();
    sys_unfreeze();
    #if CONNECT_TPS55289_0
    sys_pwr_set_verg_current_limit(0, 200);
    sys_pwr_set_verg_voltage(0, 5000);
    sys_pwr_enable_verg(0,0);
    #endif
    #if CONNECT_TPS55289_1
    sys_pwr_enable_verg(1,0);
    #endif

    #if CONFIG_CONNECT_INA3221
    sys_pwr_current_monitor_reset();
    #endif

    #if CONFIG_CONNECT_TCA6424A
    SYS_GPIO_RESET_PIN(RIK_IO_PIN_GPIO_EXPANDER_nRESET);
    #endif

    sys_io_reset_all();
    rik_link_pins();
    rik_link_interrupts();
    STATUS_RESUME();
    ESP_LOGI(TAG, "Devices default configuration applied");
}

status_rep_t handle_vm_stop(void){
    R_EVENT_SET(rik_events_vm, EVENT_BIT_VM_STOP);
    ESP_LOGW(TAG, "VM stopped");
    return STA_OK;
}

void reset_regulator_faults(void){
    sys_unfreeze();
    sys_gpio_set_level(rik_gpio_expander_port_id, SYS_IO_GET_MASK(RIK_IO_PIN_REGA_EN) | SYS_IO_GET_MASK(RIK_IO_PIN_REGB_EN), 0);
    sys_gpio_set_level(rik_gpio_expander_port_id, SYS_IO_GET_MASK(RIK_IO_PIN_REGA_EN) | SYS_IO_GET_MASK(RIK_IO_PIN_REGB_EN), 1);
    ESP_LOGW(TAG, "Regulator faults reset");
}

void sys_handle_pwr_callbacks(manager_pwr_cb_type_e callback_type) {
    rik_sys_ctrl_flags_e flag = ((uint8_t*)&system_ctrl_config)[callback_type];
    switch (flag) {
        case SYS_CTRL_AUTOMATIC: {
            switch (callback_type) {
                case MANAGER_PWR_CB_CURRENT_REG0_WARNING:
                case MANAGER_PWR_CB_CURRENT_SYS_WARNING:
                case MANAGER_PWR_CB_CURRENT_REG1_WARNING: {
                    if (s_vm_sys_power_cb) s_vm_sys_power_cb((void*)callback_type);
                    vm_callback_power_event((void*)callback_type);
                    break;
                }
                case MANAGER_PWR_CB_REG0_OVP:
                case MANAGER_PWR_CB_REG0_OCP:
                case MANAGER_PWR_CB_REG1_OVP:
                case MANAGER_PWR_CB_REG1_OCP: {
                    reset_regulator_faults();
                    break;
                }
                case MANAGER_PWR_CB_REG0_SCP:
                case MANAGER_PWR_CB_REG1_SCP:
                case MANAGER_PWR_CB_CURRENT_SYS_CRITICAL: {
                    SYS_STOP();
                    break;
                }
                case MANAGER_PWR_CB_CURRENT_REG0_CRITICAL:
                case MANAGER_PWR_CB_CURRENT_REG1_CRITICAL: {
                    handle_vm_stop();
                    break;
                }
                default:
                    break;
            }
            break;
        }

        case SYS_CTRL_SKIP_EVENT: {
            ESP_LOGW(TAG, "Skipping event for callback type %d", callback_type);
            break;
        }

        case SYS_CTRL_VM_CALLBACKS_ONLY: {
            if (s_vm_sys_power_cb) s_vm_sys_power_cb((void*)callback_type);
            vm_callback_power_event((void*)callback_type);
            break;
        }

        case SYS_CTRL_STOP: {
            SYS_STOP();
            break;
        }

        case SYS_CTRL_ENTER_EMERGENCY: {
            handle_vm_stop();
            break;
        }

        case SYS_CTRL_DISABLE_DEVICE: {
            switch (callback_type) {
                case MANAGER_PWR_CB_REG0_OVP:
                case MANAGER_PWR_CB_REG0_OCP:
                case MANAGER_PWR_CB_REG0_SCP:
                case MANAGER_PWR_CB_CURRENT_REG0_WARNING:
                case MANAGER_PWR_CB_CURRENT_REG0_CRITICAL: {
                    SYS_GPIO_SET_LEVEL(RIK_IO_PIN_REGA_EN, 0);
                    ESP_LOGW(TAG, "Regulator 0 disabled");
                    break;
                }
                case MANAGER_PWR_CB_REG1_OVP:
                case MANAGER_PWR_CB_REG1_OCP:
                case MANAGER_PWR_CB_REG1_SCP:
                case MANAGER_PWR_CB_CURRENT_REG1_WARNING:
                case MANAGER_PWR_CB_CURRENT_REG1_CRITICAL: {
                    SYS_GPIO_SET_LEVEL(RIK_IO_PIN_REGB_EN, 0);
                    ESP_LOGW(TAG, "Regulator 1 disabled");
                    break;
                }
                default:
                    ESP_LOGW(TAG, "No specific device to disable for callback type %d", callback_type);
                    break;
            }
            break;
        }

        default:
            break;
    }
}

void rik_callback_manager_pwr(void* param){
    manager_pwr_cb_type_e type = (manager_pwr_cb_type_e)(uintptr_t)param;
    sys_handle_pwr_callbacks(type);
}

void rik_callback_adc(void* param){
    sys_pin_t pin = (sys_pin_t)(uintptr_t)param;
    uint8_t port = SYS_IO_GET_PORT(pin);
    uint8_t pin_num = SYS_IO_GET_PIN(pin);
    ESP_LOGW(TAG, "ADC callback triggered for pin %d on port %d", pin_num, port);
    if (s_vm_sys_adc_cb) s_vm_sys_adc_cb(param);
}

void rik_callback_gpio(void* param){
    sys_pin_t pin = (sys_pin_t)(uintptr_t)param;
    uint8_t port = SYS_IO_GET_PORT(pin);
    uint8_t pin_num = SYS_IO_GET_PIN(pin);
    ESP_LOGW(TAG, "GPIO callback triggered for pin %d on port %d", pin_num, port);
    if (s_vm_sys_gpio_cb) s_vm_sys_gpio_cb(param);
}

void sys_ctrl_set_power_cfg(rik_sys_ctrl_power_cfg_t* cfg){
    memcpy(&system_ctrl_config, cfg, sizeof(rik_sys_ctrl_power_cfg_t));
}

rik_sys_ctrl_power_cfg_t* sys_ctrl_get_power_cfg(void){
    return &system_ctrl_config;
}

