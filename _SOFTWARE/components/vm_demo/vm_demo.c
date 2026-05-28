#include "vm_demo.h"
#include "f_esc_servo.h"
#include "rik_shared.h"
#include "rik_system_ctrl.h"
#include "esp_timer.h"
#include "rik_logs.h"
#include "manager_power.h"
R_TASK_DEFINE(vm_demo_task, 4096);

void vm_demo_task_func(void* args){

    EventGroupHandle_t vm_events = (EventGroupHandle_t)(args);
    f_servo_handle_t servo_1 = f_servo_simple_new(RIK_PWM_EXPANDER_USER_CHANNEL_7);
    f_servo_handle_t servo_2 = f_servo_simple_new(RIK_PWM_EXPANDER_USER_CHANNEL_6);
    sys_pwr_set_bus_current_warning(RIK_CHANNEL_VREG0, 600);
    sys_pwr_set_bus_current_critical(RIK_CHANNEL_VREG0, 1100);
    SYS_IO_SET_PWM_FREQ(RIK_PWM_EXPANDER_USER_CHANNEL_7, 50);
    uint32_t adc_value[4];
    rik_enable_log_mirroring(false);
    while (1)
    {
        rik_devices_freeze();
        f_servo_set_angle(servo_1, -90);
        f_servo_set_angle(servo_2, 90);
        rik_devices_unfreeze();
        vTaskDelay(MSEC(1000));
        rik_devices_freeze();
        f_servo_set_angle(servo_1, 90);
        f_servo_set_angle(servo_2, -90);
        rik_devices_unfreeze();
        vTaskDelay(MSEC(1000));
        sys_io_adc_read(rik_gpio_esp_port_id,SYS_IO_GET_MASK(RIK_IO_PIN_DRV_1_IPROPI_1)|SYS_IO_GET_MASK(RIK_IO_PIN_DRV_1_IPROPI_2)|
        SYS_IO_GET_MASK(RIK_IO_PIN_DRV_1_IPROPI_3)|SYS_IO_GET_MASK(RIK_IO_PIN_DRV_1_IPROPI_4), adc_value, 4);
        ESP_LOGI("VM", "ADC readings - IPROPI 1: %d mV, IPROPI 2: %d mV, IPROPI 3: %d mV, IPROPI 4: %d mV", adc_value[0], adc_value[1], adc_value[2], adc_value[3]);
        int32_t adc_value_1;
        sys_pwr_get_bus_current(RIK_CHANNEL_VREG0, &adc_value_1);
        ESP_LOGI("VM", "Current reading from power manager for VREG0: %d mA", adc_value_1);
    }
}

void vm_callback_power_event(void* param){
    ESP_LOGW("VM", "VM received power event callback with param %d", (int)(uintptr_t)param);
}

TaskHandle_t vm_demo_start(void){
    R_TASK_START(vm_demo_task, vm_demo_task_func, NULL, 5);
    return vm_demo_task;
}
