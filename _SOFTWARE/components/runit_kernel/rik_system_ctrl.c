#include "rik_system_ctrl.h"
#include "manager_io.h"
#include "manager_power.h"
#include "rik_shared.h"
#include "rtos_utils.h"
#include "rik_devices_link.h"
#include "vm_demo.h"

#define TAG __FILE_NAME__

extern TaskHandle_t vm_demo_task;

static rik_sys_ctrl_cfg_t system_ctrl_config = {0};

#define OFF_GPIO_EXPANDER_MASK SYS_IO_GET_MASK(RIK_IO_PIN_DRV1_SLEEP) | SYS_IO_GET_MASK(RIK_IO_PIN_DRV2_SLEEP) | \
                            SYS_IO_GET_MASK(RIK_IO_PIN_REGA_EN) | SYS_IO_GET_MASK(RIK_IO_PIN_REGB_EN) | \
                            SYS_IO_GET_MASK(RIK_IO_PIN_DRV2_VMA_REGA) | SYS_IO_GET_MASK(RIK_IO_PIN_DRV2_VMB_VSUP) | \
                            SYS_IO_GET_MASK(RIK_IO_PIN_DRV1_VMA_VSUP) | SYS_IO_GET_MASK(RIK_IO_PIN_DRV1_VMB_REGB) | \
                            SYS_IO_GET_MASK(RIK_IO_PIN_PWM_EXPANDER_nOE)
#define OFF_GPIO_ESP_MASK SYS_IO_GET_MASK(RIK_IO_PIN_DRV_1_IN1) | SYS_IO_GET_MASK(RIK_IO_PIN_DRV_1_IN2) | \
                            SYS_IO_GET_MASK(RIK_IO_PIN_DRV_1_IN3) | SYS_IO_GET_MASK(RIK_IO_PIN_DRV_1_IN4) | \
                            SYS_IO_GET_MASK(RIK_IO_PIN_DRV_1_EN1) | SYS_IO_GET_MASK(RIK_IO_PIN_DRV_1_EN2) | \
                            SYS_IO_GET_MASK(RIK_IO_PIN_DRV_1_EN3) | SYS_IO_GET_MASK(RIK_IO_PIN_DRV_1_EN4)

void rik_devices_freeze(){
    manager_io_freeze();
    manager_pwr_freeze_mode(1);
}

void rik_devices_unfreeze(){
    manager_io_unfreeze();
    manager_pwr_freeze_mode(0);
    R_EVENT_CLEAR(rik_events_wired, EVENT_BIT_I2C_DONE_0 | EVENT_BIT_I2C_DONE_1);
    R_EVENT_SET(rik_events_wired, EVENT_BIT_I2C_PROCESS_0|EVENT_BIT_I2C_PROCESS_1);
    R_EVENT_AWAIT_ALL(rik_events_wired,EVENT_BIT_I2C_DONE_0 | EVENT_BIT_I2C_DONE_1, MSEC(200));
}


status_rep_t stop_devices(void){
    //stop reports in case any of devices is still generating erorrs
    STATUS_SUSPEND();
    rik_devices_unfreeze();
    sys_gpio_set_level(rik_gpio_expander_port_id, OFF_GPIO_EXPANDER_MASK, 0);
    sys_gpio_set_level(rik_gpio_esp_port_id, OFF_GPIO_ESP_MASK, 0);
    ESP_LOGW(TAG, "All devices have been stopped");
    //resume reports after disabling devices
    STATUS_RESUME();
    return STA_OK;
}

status_rep_t devices_default_config(void){
    /*******VREGS******************************************** */
    sys_pwr_set_verg_current_limit(0, 200);
    sys_pwr_set_verg_current_limit(1, 200);
    sys_pwr_set_verg_voltage(0, 5000);
    sys_pwr_set_verg_voltage(1, 5000);
    sys_pwr_enable_verg(0,0);
    sys_pwr_enable_verg(1,0);
    /*******VREGS******************************************** */
    sys_pwr_current_monitor_reset();
    /* gpio expander reset */
    SYS_GPIO_RESET_PIN(RIK_IO_PIN_GPIO_EXPANDER_nRESET);
    sys_io_reset_all();
    rik_link_pins();
    rik_link_interrupts();
    return STA_OK;
}

status_rep_t handle_vm_stop(void){
    STATUS_SUSPEND();
    rik_devices_unfreeze();
    R_EVENT_SET(rik_events_vm, EVENT_BIT_VM_STOP);
    R_EVENT_SET(rik_events_vm, EVENT_BIT_VM_EMERGENCY);
    ESP_LOGW(TAG, "VM stopped");
    vTaskSuspend(vm_demo_task);
    STATUS_RESUME();
    return STA_OK;
}

void reset_regulator_faults(void){
    rik_devices_unfreeze();
    sys_gpio_set_level(rik_gpio_expander_port_id, SYS_IO_GET_MASK(RIK_IO_PIN_REGA_EN) | SYS_IO_GET_MASK(RIK_IO_PIN_REGB_EN), 0);
    sys_gpio_set_level(rik_gpio_expander_port_id, SYS_IO_GET_MASK(RIK_IO_PIN_REGA_EN) | SYS_IO_GET_MASK(RIK_IO_PIN_REGB_EN), 1);
    ESP_LOGW(TAG, "Regulator faults reset");
}


void activate_ctrl_mode(manager_pwr_cb_type_e callback_type) {
    rik_sys_ctrl_flags_e flag = ((uint8_t*)&system_ctrl_config)[callback_type];
    
    switch (flag) {
        case SYS_CTRL_AUTOMATIC: {
            switch (callback_type) {
                case MANAGER_PWR_CB_CURRENT_REG0_WARNING:
                case MANAGER_PWR_CB_CURRENT_SYS_WARNING:
                case MANAGER_PWR_CB_CURRENT_REG1_WARNING: {
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
                    stop_devices();
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
            vm_callback_power_event((void*)callback_type);
            break;
        }
        
        case SYS_CTRL_STOP: {
            stop_devices();
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

void rik_callback_vreg(void* param){
    manager_pwr_cb_type_e type = (manager_pwr_cb_type_e)(uintptr_t)param;
    activate_ctrl_mode(type);
}

void rik_callback_current_monitor(void* param){
    manager_pwr_cb_type_e type = (manager_pwr_cb_type_e)(uintptr_t)param;
    rik_devices_freeze();
    int32_t current_mA[3];
    sys_pwr_get_bus_current(RIK_CHANNEL_VREG0, &current_mA[0]);
    sys_pwr_get_bus_current(RIK_CHANNEL_TOTAL, &current_mA[1]);
    sys_pwr_get_bus_current(RIK_CHANNEL_VREG1, &current_mA[2]);
    ESP_LOGW(TAG, "Current monitor callback triggered for type %d. Current readings - VREG0: %d mA, TOTAL: %d mA, VREG1: %d mA", type, current_mA[0], current_mA[1], current_mA[2]);
    rik_devices_unfreeze();
    activate_ctrl_mode(type);
}


void rik_callback_adc(void* param){
    sys_pin_t pin = (sys_pin_t)(uintptr_t)param;
    uint8_t port = SYS_IO_GET_PORT(pin);
    uint8_t pin_num = SYS_IO_GET_PIN(pin);
    ESP_LOGW(TAG, "ADC callback triggered for pin %d on port %d", pin_num, port);
}


void rik_sys_ctrl_set_cfg(rik_sys_ctrl_cfg_t* cfg){
    memcpy(&system_ctrl_config, cfg, sizeof(rik_sys_ctrl_cfg_t));
}