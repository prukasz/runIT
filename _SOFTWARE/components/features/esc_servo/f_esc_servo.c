#include "f_esc_servo.h"

static f_servo_handle_t servo_list = NULL;
static uint32_t servo_count = 0;
static uint32_t next_servo_id = 0;


f_servo_handle_t f_servo_find_by_pin(sys_pin_t pin) {
    for (f_servo_handle_t current = servo_list; current != NULL; current = current->next) {
        if (current->pin == pin) {
            return current;
        }
    }
    return NULL;
}

uint32_t f_servo_get_count(void) {
    return servo_count;
}

status_rep_t f_servo_remove(f_servo_handle_t servo) {
    if (servo == NULL) {
        return STA_C(ESP_ERR_INVALID_ARG, OWNER_IO_MANAGER, 0);
    }

    f_servo_handle_t* current_link = &servo_list;
    while (*current_link != NULL && *current_link != servo) {
        current_link = &(*current_link)->next;
    }

    if (*current_link == NULL) {
        return STA_C(ESP_ERR_NOT_FOUND, OWNER_IO_MANAGER, servo->pin);
    }

    *current_link = servo->next;
    if (servo_count > 0) {
        servo_count--;
    }

    status_rep_t result = SYS_IO_SET_PWM_DUTY(servo->pin, 0);
    free(servo);
    return result;
}

status_rep_t f_servo_remove_by_pin(sys_pin_t pin) {
    f_servo_handle_t servo = f_servo_find_by_pin(pin);
    if (servo == NULL) {
        return STA_C(ESP_ERR_NOT_FOUND, OWNER_IO_MANAGER, pin);
    }
    return f_servo_remove(servo);
}


f_servo_handle_t f_servo_new(sys_pin_t pin,
    uint16_t min_us,
    uint16_t max_us, 
    uint16_t def_us, 
    float range_min_deg, 
    float range_max_deg, 
    float soft_limit_min_deg, 
    float soft_limit_max_deg, 
    float default_position_deg)
{
    if (f_servo_find_by_pin(pin) != NULL) {
        return NULL;
    }

    f_servo_handle_t servo = malloc(sizeof(f_servo_obj_t));
    if (servo == NULL) {
        return NULL; 
    }

    servo->pin = pin;
    servo->id = next_servo_id++;
    servo->next = servo_list;
    servo->min_us = min_us == 0 ? F_SERVO_US_MIN : min_us; 
    servo->max_us = max_us == 0 ? F_SERVO_US_MAX : max_us;
    servo->zero_us = def_us == 0 ? F_SERVO_US_NEUTRAL : def_us;
    servo->range_min_deg = range_min_deg == 0 ? -90.0f : range_min_deg;
    servo->range_max_deg = range_max_deg == 0 ? 90.0f : range_max_deg;
    servo->soft_limit_min_deg = soft_limit_min_deg == 0 ? servo->range_min_deg : soft_limit_min_deg;
    servo->soft_limit_max_deg = soft_limit_max_deg == 0 ? servo->range_max_deg : soft_limit_max_deg;
    servo->default_position_deg = default_position_deg;
    servo->current_angle = default_position_deg; // Initialize to default position

    servo_list = servo;
    servo_count++;
    return servo;
}