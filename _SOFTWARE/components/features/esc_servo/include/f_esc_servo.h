#include <stdint.h>
#include <stdlib.h>
#include "status.h"
#include "manager_io.h"
#include <math.h> 

// --- Macros ---
#define F_SERVO_FREQ_HZ          50
#define F_SERVO_US_MIN           1000
#define F_SERVO_US_MAX           2000
#define F_SERVO_US_MIN_EXTENDED  500
#define F_SERVO_US_MAX_EXTENDED  2500
#define F_SERVO_US_NEUTRAL       1500

#define F_SERVO_US_TO_DUTY(us) (uint16_t)(((uint32_t)(us) * 4096) / 20000)

// --- Types & Enums ---
typedef struct f_servo_obj_t {
    sys_pin_t pin;
    uint32_t id;
    struct f_servo_obj_t* next;
    
    uint16_t min_us;
    uint16_t max_us;
    uint16_t zero_us;
    
    float range_min_deg;
    float range_max_deg;
    
    float soft_limit_min_deg;
    float soft_limit_max_deg;
    float default_position_deg;

    float current_angle;
} f_servo_obj_t;

typedef f_servo_obj_t* f_servo_handle_t;
typedef f_servo_handle_t f_esc_handle_t;

// (Assuming f_servo_new is implemented in your .c file)
f_servo_handle_t f_servo_new(sys_pin_t pin, uint16_t min_us, uint16_t max_us, uint16_t def_us, float range_min_deg, float range_max_deg, float soft_limit_min_deg, float soft_limit_max_deg, float default_position_deg);
status_rep_t f_servo_remove(f_servo_handle_t servo);
status_rep_t f_servo_remove_by_pin(sys_pin_t pin);
f_servo_handle_t f_servo_find_by_pin(sys_pin_t pin);
uint32_t f_servo_get_count(void);

// Standard defaults to prevent division by zero
static inline f_servo_handle_t f_servo_simple_new(sys_pin_t pin) {
    return f_servo_new(pin,
                       F_SERVO_US_MIN,
                       F_SERVO_US_MAX,
                       F_SERVO_US_NEUTRAL,
                       -90.0f,
                       90.0f,
                       -90.0f,
                       90.0f,
                       0.0f);
}

// Drone ESCs idle at 1000us, so zero_us is F_SERVO_US_MIN. Default pos is -100%.
static inline f_esc_handle_t f_esc_simple_new(sys_pin_t pin) {
    return (f_esc_handle_t)f_servo_new(pin,
                                       F_SERVO_US_MIN,
                                       F_SERVO_US_MAX,
                                       F_SERVO_US_MIN,
                                       -100.0f,
                                       100.0f,
                                       -100.0f,
                                       100.0f,
                                       -100.0f);
}

// Backwards-compatible macros
#define F_SERVO_SIMPLE_NEW(pin) f_servo_simple_new(pin)
#define F_ESC_SIMPLE_NEW(pin)    f_esc_simple_new(pin)

static inline uint16_t f_servo_deg_to_duty(f_servo_handle_t servo, float angle_deg) {
    // Return neutral duty instantly if perfectly zero
    if (angle_deg == 0.0f) {
        return F_SERVO_US_TO_DUTY(servo->zero_us);
    }
    
    if (angle_deg > 0.0f) {
        // Cast to float to prevent unsigned underflow/truncation
        float max_travel = (float)servo->max_us - (float)servo->zero_us;
        return F_SERVO_US_TO_DUTY(servo->zero_us + max_travel * (angle_deg / servo->range_max_deg));
    } else {
        // Cast to float to allow negative travel calculations
        float min_travel = (float)servo->min_us - (float)servo->zero_us; 
        return F_SERVO_US_TO_DUTY(servo->zero_us + min_travel * (angle_deg / servo->range_min_deg));
    }
}

static inline status_rep_t f_servo_set_angle(f_servo_handle_t servo, float angle_deg) {
    // 1. Clamp to hard limits first
    if (angle_deg < servo->range_min_deg) angle_deg = servo->range_min_deg;
    if (angle_deg > servo->range_max_deg) angle_deg = servo->range_max_deg;
    
    // 2. Clamp to soft limits (tighter security)
    if (angle_deg < servo->soft_limit_min_deg) angle_deg = servo->soft_limit_min_deg;
    if (angle_deg > servo->soft_limit_max_deg) angle_deg = servo->soft_limit_max_deg;
    
    // 3. Save the safely clamped angle
    servo->current_angle = angle_deg; 
    
    // 4. Calculate and apply
    uint16_t duty = f_servo_deg_to_duty(servo, angle_deg);
    //ESP_LOGI("F_SERVO", "Setting servo on pin %u to angle %.2f degrees (duty: %u)", servo->pin, angle_deg, duty);
    return SYS_IO_SET_PWM_DUTY(servo->pin, duty);
}

static inline status_rep_t f_esc_set_speed(f_esc_handle_t esc, float speed_percent) {
    return f_servo_set_angle((f_servo_handle_t)esc, speed_percent);
}

static inline status_rep_t f_servo_home(f_servo_handle_t servo) {
    return f_servo_set_angle(servo, servo->default_position_deg);
}

static inline status_rep_t f_esc_stop(f_esc_handle_t esc) {
    return f_esc_set_speed(esc, esc->default_position_deg);
}
