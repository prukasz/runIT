#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "soc/gpio_struct.h"
#include "driver/gpio.h"

#include "shared_io_types.h"
#include "esp_adc_config.h" 
#include "provider_gpio_esp.h"
#include "rtos_utils.h"
#include "esp_adc/adc_continuous.h"
#include "esp_timer.h"

#define TAG __FILE_NAME__
#undef OWNER
#define OWNER OWNER_PROVIDER_GPIO_ESP

// ESP32 specific masks
#define AVIABLE_GPIO_MASK (SOC_GPIO_VALID_GPIO_MASK & ~(0ULL | BIT19 | BIT20)) 
#define ADC_GPIO_MASK     (0ULL | BIT0 | BIT1 | BIT2 | BIT3 | BIT4 | BIT5 | BIT6 | BIT7 | BIT8 | BIT9)

R_QUEUE_DEFINE(gpio_evt_queue, CONFIG_MAX_PENDING_GPIO_ESP_INTR, sizeof(sys_pin_obj_t*));
R_TASK_DEFINE(gpio_dispatcher_task, 4096);

// --- State Variables ---
static bool _freeze = false;
static uint64_t pending_pin_level_updates = 0;
static uint64_t cached_pin_levels = 0;
static uint8_t my_port_id = 0xFF;  // Port ID assigned by IO manager

// THE UNIFIED REGISTRY: Instant O(1) lookup. NULL means the pin is completely free.
sys_pin_obj_t* pin_registry[64] = {NULL};

// --- FreeRTOS ISR Dispatcher ---

// Background task that executes the user's callbacks safely
static void gpio_dispatcher_task_function(void* arg) {
    sys_pin_obj_t* pin_obj;
    
    while (1) {
        // Wait forever until the ISR pushes a pin object into the queue
        if (xQueueReceive(gpio_evt_queue, &pin_obj, portMAX_DELAY)) {
            // Safely in task context! 
            // Blocking operations, printf, Mutexes, I2C/SPI are fully safe here.
            if (pin_obj && pin_obj->callback) {
                pin_obj->callback(pin_obj->callback_arg);
            }
        }
    }
}



// Ultra-fast ISR Trampoline
static void IRAM_ATTR _gpio_pin_isr_trampoline(void* arg) {
    sys_pin_obj_t* pin = (sys_pin_obj_t*)arg;
    
    // --- DEBOUNCE LOGIC ---
    uint64_t current_time = esp_timer_get_time(); // Time in microseconds
    if ((current_time - pin->last_isr_time) < CONFIG_ESP_GPIO_DEBOUNCE_TIME_US) {
        // It hasn't been 50ms since the last interrupt. This is a bounce.
        return; // Exit immediately, do not push to queue
    }
    // Update the timestamp for the next valid press
    pin->last_isr_time = current_time; 
    // ----------------------
    
    BaseType_t high_task_wakeup = pdFALSE;
    // Push the pin object pointer to the background task
    xQueueSendFromISR(gpio_evt_queue, &pin, &high_task_wakeup);

    // Force immediate context switch to the dispatcher task if it was waiting
    if (high_task_wakeup == pdTRUE) {
        portYIELD_FROM_ISR();
    }
}
// Must be called during system startup to initialize the driver
status_rep_t p_gpio_esp_init(void) {
    R_TASK_START_ON_CORE(gpio_dispatcher_task, gpio_dispatcher_task_function, NULL, CONFIG_MAX_PENDING_GPIO_ESP_INTR, 0);
    CHECK_ESP_CALL_R(esp_adc_start());
    CHECK_ESP_CALL_R(gpio_install_isr_service(0));
    return STA_OK;
}

// --- GPIO Configuration and Hardware API ---

bool verify_pin_free(uint64_t pin_mask) {
    for(int i = 0; i < 64; i++){
        if((pin_mask & (1ULL << i)) != 0){
            if(pin_registry[i] != NULL){
                return false;
            }
        }
    }
    return true;
}

esp_err_t normal_io_configure(uint8_t pin, uint32_t mode) {
    if (!verify_pin_free(1ULL << pin)) {
        return ESP_ERR_INVALID_STATE; 
    }

    sys_pin_obj_t* new_pin = calloc(1, sizeof(sys_pin_obj_t));
    if (new_pin == NULL) {
        return ESP_ERR_NO_MEM; 
    }

        // Set standard generic fields
    new_pin->io_num =  pin;
    new_pin->pin_mode = mode;
    new_pin->hw.gpio_cfg.pin_bit_mask = (1ULL << pin);
   
    if (mode == SYS_GPIO_MODE_INPUT || 
        mode == SYS_GPIO_MODE_INPUT_PULLUP || 
        mode == SYS_GPIO_MODE_INPUT_PULLDOWN) {
        new_pin->hw.gpio_cfg.mode = GPIO_MODE_INPUT;
    } else if (mode == SYS_GPIO_MODE_OUTPUT_OPEN_DRAIN) {
        new_pin->hw.gpio_cfg.mode = GPIO_MODE_OUTPUT_OD;
    } else if (mode == SYS_GPIO_MODE_OUTPUT_PUSH_PULL) {
        new_pin->hw.gpio_cfg.mode = GPIO_MODE_OUTPUT; // Push-Pull
    } else {
        ESP_LOGE(TAG, "Invalid GPIO mode requested: %lu", (unsigned long)mode);
         free(new_pin);
        return ESP_ERR_INVALID_ARG;
    }

    // Safely map pull resistors based on exact mode matches
    new_pin->hw.gpio_cfg.pull_up_en = (mode == SYS_GPIO_MODE_INPUT_PULLUP) ? GPIO_PULLUP_ENABLE : GPIO_PULLUP_DISABLE;
    new_pin->hw.gpio_cfg.pull_down_en = (mode == SYS_GPIO_MODE_INPUT_PULLDOWN) ? GPIO_PULLDOWN_ENABLE : GPIO_PULLDOWN_DISABLE;
    new_pin->hw.gpio_cfg.intr_type = GPIO_INTR_DISABLE;


        // Apply config to hardware
    esp_err_t err = gpio_config(&new_pin->hw.gpio_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "gpio_config failed for pin %d with error %d", pin, err);
        return err;
    }
        
    ESP_LOGI(TAG, "Configured GPIO pin %d successfully", pin);

    pin_registry[pin] = new_pin;

    return ESP_OK;
}

status_rep_t p_gpio_esp_set_pin_mode(uint8_t pin, uint32_t mode) {
    uint64_t pin_mask = 1ULL << pin;
    if ((pin_mask & ~AVIABLE_GPIO_MASK) != 0) {
        return STA_C(IO_ERR_PIN_UNSUPPORTED, OWNER_PROVIDER_GPIO_ESP, SYS_IO_MAKE_INFO(my_port_id, pin, mode));
    }
     
    if (mode == SYS_GPIO_MODE_ADC) {
        if ((pin_mask & ~ADC_GPIO_MASK) != 0) {
            return STA_C(IO_ERR_MODE_UNSUPPORTED, OWNER_PROVIDER_GPIO_ESP, SYS_IO_MAKE_INFO(my_port_id, pin, mode));
        } else {
            if (!verify_pin_free(pin_mask)) {
                return STA_C(IO_ERR_PIN_IN_OTHER_USE, OWNER_PROVIDER_GPIO_ESP, SYS_IO_MAKE_INFO(my_port_id, pin, mode));
            }
            sys_pin_obj_t* new_pin = calloc(1, sizeof(sys_pin_obj_t));
            if (new_pin == NULL) {
                return STA_C(IO_ERR_UPDATE_FAILED, OWNER_PROVIDER_GPIO_ESP, SYS_IO_MAKE_INFO(my_port_id, pin, mode));
            }
            adc_channel_t channel = 0;
            adc_unit_t unit = 0;

            adc_continuous_io_to_channel(pin, &unit, &channel);
            new_pin->io_num = pin;
            new_pin->pin_mode = mode;
            new_pin->hw.adc_cfg.adc_channel =  (uint8_t)channel;
            pin_registry[pin] = new_pin;
            esp_adc_bind_pin_obj(channel, new_pin);
            esp_adc_set_active_channels(1U << channel);
            }
            
            return STA_OK;
        }else if (mode == SYS_GPIO_MODE_PWM) {
        // Placeholder: Apply PWM logic via PWM module
            return STA_OK;
        } else {
        CHECK_ESP_CALL_R(normal_io_configure(pin, mode));
    }
    return STA_OK;
}

status_rep_t p_gpio_esp_set_level(uint64_t pin_mask, bool level) {
    // [FIX APPLIED] Pass 1: Pre-validate all pins atomically
    for (int i = 0; i < 64; i++) {
        if ((pin_mask & (1ULL << i)) == 0) continue;
        
        sys_pin_obj_t* pin_obj = pin_registry[i];

        if (pin_obj == NULL) {
            return STA_C(IO_ERR_PIN_NOT_CONFIGURED, OWNER_PROVIDER_GPIO_ESP, SYS_IO_MAKE_INFO(my_port_id, i, 0));
        }

        if (pin_obj->pin_mode != SYS_GPIO_MODE_OUTPUT_PUSH_PULL && pin_obj->pin_mode != SYS_GPIO_MODE_OUTPUT_OPEN_DRAIN) {
            return STA_C(IO_ERR_PIN_UNSUPPORTED, OWNER_PROVIDER_GPIO_ESP, SYS_IO_MAKE_INFO(my_port_id, i, 0));
        }
    }

    // Pass 2: Apply states safely
    for (int i = 0; i < 64; i++) {
        if ((pin_mask & (1ULL << i)) == 0) continue;

        if (_freeze) {
            if (level)
                cached_pin_levels |= (1ULL << i);
            else
                cached_pin_levels &= ~(1ULL << i);

            pending_pin_level_updates |= (1ULL << i);
            continue;
        }
        esp_err_t err = gpio_set_level(i, level);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to set level on pin %d: %d", i, err);
            return STA_C(IO_ERR_UPDATE_FAILED, OWNER_PROVIDER_GPIO_ESP, SYS_IO_MAKE_INFO(my_port_id, i, 0));
        }
        if (level)
            cached_pin_levels |= (1ULL << i);
        else
            cached_pin_levels &= ~(1ULL << i);
    }
    return STA_OK;
}

status_rep_t p_gpio_esp_read_level(uint64_t pin_mask, uint64_t* out_level) {
    for (int i = 0; i < 64; i++) {
        if ((pin_mask & (1ULL << i)) == 0) continue;

        sys_pin_obj_t* pin_obj = pin_registry[i];

        if (pin_obj == NULL) {
            return STA_C(IO_ERR_PIN_NOT_CONFIGURED, OWNER_PROVIDER_GPIO_ESP, SYS_IO_MAKE_INFO(my_port_id, i, 0));
        }

        if (pin_obj->pin_mode == SYS_GPIO_MODE_ADC || pin_obj->pin_mode == SYS_GPIO_MODE_PWM) {
            return STA_C(IO_ERR_PIN_UNSUPPORTED, OWNER_PROVIDER_GPIO_ESP, SYS_IO_MAKE_INFO(my_port_id, i, 0));
        }

        if (_freeze) {
            *out_level |= (uint64_t)((cached_pin_levels >> i) & 1ULL) << i;
            continue;
        }

        int gpio_level = gpio_get_level(i);
        if (gpio_level < 0) {
            return STA_C(IO_ERR_UPDATE_FAILED, OWNER_PROVIDER_GPIO_ESP, gpio_level);
        }

        *out_level |= ((uint64_t)gpio_level << i);

        if (gpio_level)
            cached_pin_levels |= (1ULL << i);
        else
            cached_pin_levels &= ~(1ULL << i);
    }
    return STA_OK;
}

void p_gpio_esp_freeze_updates(bool freeze) {
    esp_adc_freeze_results(freeze);
    if (freeze) {
        _freeze = true;
        return;
    }

    // Resuming: push pending updates
    for (int i = 0; i < 64; i++) {
        uint64_t bit = (1ULL << i);
        if ((pending_pin_level_updates & bit) == 0) continue;

        sys_pin_obj_t* pin_obj = pin_registry[i];
        if (pin_obj == NULL || (pin_obj->pin_mode != SYS_GPIO_MODE_OUTPUT_PUSH_PULL && pin_obj->pin_mode != SYS_GPIO_MODE_OUTPUT_OPEN_DRAIN)) {
            pending_pin_level_updates &= ~bit;
            continue;
        }

        bool level = ((cached_pin_levels >> i) & 1ULL) != 0;
        esp_err_t err = gpio_set_level(i, level);
        pending_pin_level_updates &= ~bit;
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "Failed to push pending pin level on pin %d: %d", i, err);
        }
    }

    _freeze = false;
}

status_rep_t p_gpio_esp_pin_toggle(uint64_t pin_mask) {
    for (int i = 0; i < 64; i++) {
        if ((pin_mask & (1ULL << i)) == 0) continue;

        sys_pin_obj_t* pin_obj = pin_registry[i];
        if (pin_obj == NULL) {
            return STA_C(IO_ERR_PIN_NOT_CONFIGURED, OWNER_PROVIDER_GPIO_ESP, SYS_IO_MAKE_INFO(my_port_id, i, 0));
        }
        if (pin_obj->pin_mode != SYS_GPIO_MODE_OUTPUT_PUSH_PULL && pin_obj->pin_mode != SYS_GPIO_MODE_OUTPUT_OPEN_DRAIN) {
            return STA_C(IO_ERR_PIN_UNSUPPORTED, OWNER_PROVIDER_GPIO_ESP, SYS_IO_MAKE_INFO(my_port_id, i, 0));
        }

        bool current_level = cached_pin_levels & (1ULL << i);

        if (_freeze) {
            if (current_level)
                cached_pin_levels &= ~(1ULL << i);
            else
                cached_pin_levels |= (1ULL << i);

            pending_pin_level_updates |= (1ULL << i);
            continue;
        }
        STA_R_ON_ERR(p_gpio_esp_set_level(1ULL << i, !current_level));
    }
    return STA_OK;
}

status_rep_t p_gpio_esp_reset_pin(uint8_t pin) {
    if (pin >= 64) {
        return STA_C(IO_ERR_PIN_UNSUPPORTED, OWNER_PROVIDER_GPIO_ESP, SYS_IO_MAKE_INFO(my_port_id, pin, 0));
    }
    uint64_t pin_mask = 1ULL << pin;
    if ((pin_mask & ~AVIABLE_GPIO_MASK) != 0) {
        return STA_C(IO_ERR_PIN_UNSUPPORTED, OWNER_PROVIDER_GPIO_ESP, SYS_IO_MAKE_INFO(my_port_id, pin, 0));
    }

    sys_pin_obj_t* pin_obj = pin_registry[pin];
    gpio_reset_pin((gpio_num_t)pin);

    if (pin_obj != NULL) {
        gpio_isr_handler_remove((gpio_num_t)pin);
        gpio_set_intr_type((gpio_num_t)pin, GPIO_INTR_DISABLE);

        if (pin_obj->pin_mode == SYS_GPIO_MODE_ADC) {
            uint8_t channel = pin_obj->hw.adc_cfg.adc_channel;
            esp_adc_bind_pin_obj(channel, NULL);
        }

        free(pin_obj);
        pin_registry[pin] = NULL;
    }

    ESP_LOGI(TAG, "Reset GPIO pin %d", pin);
    return STA_OK;
}

status_rep_t p_gpio_esp_adc_register_callback(uint8_t pin, void* adc_int_config) {

    if ((ADC_GPIO_MASK & (1ULL << pin)) == 0) {
        return STA_C(IO_ERR_PIN_UNSUPPORTED, OWNER_PROVIDER_GPIO_ESP, SYS_IO_MAKE_INFO(my_port_id, pin, 0));
    }

    if (pin_registry[pin] == NULL) {
        return STA_C(IO_ERR_PIN_NOT_CONFIGURED, OWNER_PROVIDER_GPIO_ESP, SYS_IO_MAKE_INFO(my_port_id, pin, 0));
    }
    adc_channel_t channel;
    adc_unit_t unit;
    adc_continuous_io_to_channel(pin, &unit, &channel);
    esp_err_t err = esp_adc_add_intr_pin((uint8_t)channel, adc_int_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register ADC interrupt for pin %d: %s", pin, esp_err_to_name(err));
        return STA_FROM_ESP(IO_ERR_UPDATE_FAILED);
    }
    return STA_OK;
}

status_rep_t p_gpio_esp_adc_read(uint64_t pin_mask, uint32_t* out_mv, uint8_t max_results_num) {
    CHECK_NOT_NULL_R(out_mv);

    uint8_t written = 0;
    for (int i = 0; i < 64 && written < max_results_num; i++) {
        if ((pin_mask & (1ULL << i)) == 0) continue;

        sys_pin_obj_t* pin_obj = pin_registry[i];
        if (pin_obj == NULL) {
            return STA_C(IO_ERR_PIN_NOT_CONFIGURED, OWNER_PROVIDER_GPIO_ESP, SYS_IO_MAKE_INFO(my_port_id, i, 0));
        }

        if (pin_obj->pin_mode != SYS_GPIO_MODE_ADC) {
            return STA_C(IO_ERR_PIN_UNSUPPORTED, OWNER_PROVIDER_GPIO_ESP, SYS_IO_MAKE_INFO(my_port_id, i, 0));
        }

        uint16_t mv = 0;
        CHECK_ESP_CALL_R(esp_adc_get_mv((uint8_t)pin_obj->hw.adc_cfg.adc_channel, &mv));
        

        out_mv[written++] = mv;
    }

    if (written == 0) {
        return STA_C(IO_ERR_PIN_UNSUPPORTED, OWNER_PROVIDER_GPIO_ESP, SYS_IO_MAKE_INFO(my_port_id, __builtin_ctz(pin_mask), 0));
    }

    return STA_OK;
}

status_rep_t p_gpio_esp_register_callback(uint8_t pin, uint32_t mode, void (*callback)(void* arg), void* arg) {
    sys_pin_obj_t* pin_obj = pin_registry[pin];
    if (pin_obj == NULL) {
        return STA_C(IO_ERR_PIN_NOT_CONFIGURED, OWNER_PROVIDER_GPIO_ESP, SYS_IO_MAKE_INFO(my_port_id, pin, 0));
    }

    gpio_int_type_t intr_type = GPIO_INTR_DISABLE;
    switch (mode) {
        case SYS_GPIO_INTR_MODE_RISING_EDGE:  intr_type = GPIO_INTR_POSEDGE; break;
        case SYS_GPIO_INTR_MODE_FALLING_EDGE: intr_type = GPIO_INTR_NEGEDGE; break;
        case SYS_GPIO_INTR_MODE_BOTH_EDGES:   intr_type = GPIO_INTR_ANYEDGE; break;
        case SYS_GPIO_INTR_MODE_LEVEL_HIGH:   intr_type = GPIO_INTR_HIGH_LEVEL; break;
        case SYS_GPIO_INTR_MODE_LEVEL_LOW:    intr_type = GPIO_INTR_LOW_LEVEL; break;
        default: return STA_C(IO_ERR_MODE_UNSUPPORTED, OWNER_PROVIDER_GPIO_ESP, SYS_IO_MAKE_INFO(my_port_id, pin, mode));
    }

    // store if any new provided
    if(callback)pin_obj->callback = callback;
    if(arg)pin_obj->callback_arg = arg;

        // Apply hardware interrupt settings
    CHECK_ESP_CALL_R(gpio_set_intr_type((gpio_num_t)pin, intr_type));
    CHECK_ESP_CALL_R(gpio_isr_handler_add((gpio_num_t)pin, _gpio_pin_isr_trampoline, pin_obj));
    ESP_LOGI(TAG, "Registered callback for GPIO pin %d with mode %d", pin, mode);
    return STA_OK;
}

status_rep_t p_gpio_esp_reset_all(void) {
    /* Comprehensive GPIO reset: disable interrupts, clear callbacks, reset to default modes and free resources */
    for (int i = 0; i < 64; i++) {
        if (pin_registry[i] != NULL) {
            status_rep_t result = p_gpio_esp_reset_pin((uint8_t)i);
            if (!STA_IS_OK(result)) {
                ESP_LOGW(TAG, "Failed to reset GPIO pin %d during provider reset: e_code=%u, e_owner=%u", i, result.e_code, result.e_owner);
            }
        }
    }
    ESP_LOGI(TAG, "GPIO ESP provider reset: all pins reset and resources freed");
    return STA_OK;
}

void p_gpio_esp_set_port_id(uint8_t port_id) {
    my_port_id = port_id;
    ESP_LOGI(TAG, "GPIO ESP provider port ID set to %d", port_id);
}