#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "soc/gpio_struct.h"
#include "driver/gpio.h"

// Your custom headers
#include "shared_io_types.h"
#include "esp_adc_config.h" 
#include "provider_gpio_esp.h"
#include "rtos_utils.h"
#include "esp_adc/adc_continuous.h"

#define TAG __FILE_NAME__

// ESP32 specific masks
#define AVIABLE_GPIO_MASK (SOC_GPIO_VALID_GPIO_MASK & ~(0ULL | BIT19 | BIT20)) 
#define ADC_GPIO_MASK     (0ULL | BIT0 | BIT1 | BIT2 | BIT3 | BIT4 | BIT5 | BIT6 | BIT7 | BIT8 | BIT9)

R_QUEUE_DEFINE(gpio_evt_queue, 20, sizeof(sys_pin_obj_t*));
R_TASK_DEFINE(gpio_dispatcher_task, 4096);

// --- State Variables ---
static bool suspend_updates = false;
static uint64_t pending_pin_level_updates = 0;
static uint64_t cached_pin_levels = 0;

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
    
    BaseType_t high_task_wakeup = pdFALSE;
    // Push the pin object pointer to the background task
    
    xQueueSendFromISR(gpio_evt_queue, &pin, &high_task_wakeup);

    // Force immediate context switch to the dispatcher task if it was waiting
    if (high_task_wakeup == pdTRUE) {
        portYIELD_FROM_ISR();
    }
}

// Must be called during system startup to initialize the driver
esp_err_t p_gpio_esp_init(void) {

    
    // 2. Start the background dispatcher task (Priority 10)
    R_TASK_START(gpio_dispatcher_task, gpio_dispatcher_task_function, NULL, 5);
    esp_adc_start();

    esp_err_t err = gpio_install_isr_service(0);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) { 
        return err;
    }

    return ESP_OK;
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

esp_err_t normal_io_configure(uint64_t pin_mask, uint32_t mode) {
    if (!verify_pin_free(pin_mask)) {
        return ESP_ERR_INVALID_STATE; 
    }

    uint64_t pin = 0;
    for (int i = 0; i < 64; i++) {
        pin = (1ULL << i) & pin_mask;
        if (pin == 0) continue;

        // Allocate the master struct
        sys_pin_obj_t* new_pin = calloc(1, sizeof(sys_pin_obj_t));
        if (new_pin == NULL) {
            return ESP_ERR_NO_MEM; 
        }

        // Set standard generic fields
        new_pin->io_num = i;
        new_pin->pin_mode = mode;

        // Set hardware-specific fields inside the union
        new_pin->hw.gpio_cfg.pin_bit_mask = pin;
        new_pin->hw.gpio_cfg.mode = (mode & (SYS_GPIO_MODE_INPUT | SYS_GPIO_MODE_INPUT_PULLUP | SYS_GPIO_MODE_INPUT_PULLDOWN)) ? GPIO_MODE_INPUT : GPIO_MODE_OUTPUT;
        new_pin->hw.gpio_cfg.pull_up_en = (mode == SYS_GPIO_MODE_INPUT_PULLUP) ? GPIO_PULLUP_ENABLE : GPIO_PULLUP_DISABLE;
        new_pin->hw.gpio_cfg.pull_down_en = (mode == SYS_GPIO_MODE_INPUT_PULLDOWN) ? GPIO_PULLDOWN_ENABLE : GPIO_PULLDOWN_DISABLE;
        new_pin->hw.gpio_cfg.intr_type = GPIO_INTR_DISABLE;

        // Apply config to hardware
        esp_err_t err = gpio_config(&new_pin->hw.gpio_cfg);
        if (err != ESP_OK) {
            free(new_pin);
            // Note: If configuring multiple pins at once, you may want to 
            // loop backwards here to free previously allocated pins in this mask.
            return err;
        }
        
        ESP_LOGI(TAG, "Configured GPIO pin %d with mode %lu", i, (unsigned long)mode);

        // Save safely in our unified array
        pin_registry[i] = new_pin;
    }
    
    return ESP_OK;
}

status_rep_t p_gpio_esp_set_pin_mode(uint64_t pin_mask, uint32_t mode) {
    if ((pin_mask & ~AVIABLE_GPIO_MASK) != 0) {
        return STA_C(IO_ERR_PIN_UNSUPPORTED, OWNER_PROVIDER_GPIO_ESP, pin_mask);
    }
    
    if (mode == SYS_GPIO_MODE_ADC) {
        if ((pin_mask & ~ADC_GPIO_MASK) != 0) {
            return STA_C(IO_ERR_PIN_UNSUPPORTED, OWNER_PROVIDER_GPIO_ESP, pin_mask);
        } else {
            if (!verify_pin_free(pin_mask)) {
                return STA_C(IO_ERR_PIN_IN_USE, OWNER_PROVIDER_GPIO_ESP, pin_mask);
            }


            for (int i = 0; i < 64; i++) {
                if ((pin_mask & (1ULL << i)) == 0) continue;
                

                sys_pin_obj_t* new_pin = calloc(1, sizeof(sys_pin_obj_t));
                if (new_pin == NULL) {
                    return STA_C(IO_ERR_UPDATE_FAILED, OWNER_PROVIDER_GPIO_ESP, ESP_ERR_NO_MEM);
                }
                adc_channel_t channel = 0;
                adc_unit_t unit = 0;

                adc_continuous_io_to_channel(i, &unit, &channel);
                new_pin->io_num = i;
                new_pin->pin_mode = mode;
                new_pin->hw.adc_cfg.adc_channel =  (uint8_t)channel;
                pin_registry[i] = new_pin;
                esp_adc_bind_pin_obj(channel, new_pin);
                esp_adc_set_active_channels(1U << channel);
            }
            
            return STA_OK;
        }
    }else if (mode == SYS_GPIO_MODE_PWM) {
        // Placeholder: Apply PWM logic via PWM module
        return STA_OK;
    } else{
        esp_err_t err = normal_io_configure(pin_mask, mode);
        if (err != ESP_OK) {
        return STA_FROM_ESP(err, OWNER_PROVIDER_GPIO_ESP, pin_mask);
        }
    }

    return STA_OK;
}

status_rep_t p_gpio_esp_set_level(uint64_t pin_mask, bool level) {
    for (int i = 0; i < 64; i++) {
        if ((pin_mask & (1ULL << i)) == 0) continue;
        
        sys_pin_obj_t* pin_obj = pin_registry[i];

        if (pin_obj == NULL || (pin_obj->pin_mode != SYS_GPIO_MODE_OUTPUT_PUSH_PULL && pin_obj->pin_mode != SYS_GPIO_MODE_OUTPUT_OPEN_DRAIN)) {
            return STA_C(IO_ERR_PIN_UNSUPPORTED, OWNER_PROVIDER_GPIO_ESP, (uint64_t)(1ULL << i));
        }

        if (suspend_updates) {
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
            return STA_C(IO_ERR_UPDATE_FAILED, OWNER_PROVIDER_GPIO_ESP, err);
        }

        if (level)
            cached_pin_levels |= (1ULL << i);
        else
            cached_pin_levels &= ~(1ULL << i);
    }
    return STA_OK;
}

status_rep_t p_gpio_esp_read_level(uint64_t pin_mask, bool* level) {
    for (int i = 0; i < 64; i++) {
        if ((pin_mask & (1ULL << i)) == 0) continue;

        sys_pin_obj_t* pin_obj = pin_registry[i];
        if (pin_obj == NULL || (pin_obj->pin_mode != SYS_GPIO_MODE_ADC && pin_obj->pin_mode != SYS_GPIO_MODE_PWM))  {
            return STA_C(IO_ERR_PIN_UNSUPPORTED, OWNER_PROVIDER_GPIO_ESP, (uint64_t)(1ULL << i));
        }

        if (suspend_updates) {
            *level = ((cached_pin_levels >> i) & 1ULL) != 0;
            continue;
        }

        int gpio_level = gpio_get_level(i);
        if (gpio_level < 0) {
            return STA_C(IO_ERR_UPDATE_FAILED, OWNER_PROVIDER_GPIO_ESP, gpio_level);
        }

        *level = (gpio_level != 0);

        if (*level)
            cached_pin_levels |= (1ULL << i);
        else
            cached_pin_levels &= ~(1ULL << i);
    }
    return STA_OK;
}

void p_gpio_esp_suppress_updates(bool suppress) {
    if (suppress) {
        suspend_updates = true;
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

    suspend_updates = false;
}

status_rep_t p_gpio_esp_pin_toggle(uint64_t pin_mask) {
    for (int i = 0; i < 64; i++) {
        if ((pin_mask & (1ULL << i)) == 0) continue;

        sys_pin_obj_t* pin_obj = pin_registry[i];
        if (pin_obj == NULL || (pin_obj->pin_mode != SYS_GPIO_MODE_OUTPUT_PUSH_PULL && pin_obj->pin_mode != SYS_GPIO_MODE_OUTPUT_OPEN_DRAIN)) {
            return STA_C(IO_ERR_PIN_UNSUPPORTED, OWNER_PROVIDER_GPIO_ESP, (uint32_t)(1ULL << i));
        }

        bool current_level;
        status_rep_t st = p_gpio_esp_read_level(1ULL << i, &current_level);
        if (!STA_IS_OK(st)) return st;

        STA_RET_ON_ERR(p_gpio_esp_set_level(1ULL << i, !current_level));
    }
    return STA_OK;
}

status_rep_t p_gpio_esp_adc_register_callback(uint64_t pin_mask, void* adc_int_config) {
    for (int i = 0; i < 64; i++) {
        if ((pin_mask & (1ULL << i)) == 0) continue;

        if ((ADC_GPIO_MASK & (1ULL << i)) == 0) {
            return STA_C(IO_ERR_PIN_UNSUPPORTED, OWNER_PROVIDER_GPIO_ESP, (uint64_t)(1ULL << i));
        }

        adc_unit_t unit;
        adc_channel_t channel;
        if (adc_continuous_io_to_channel(i, &unit, &channel) != ESP_OK || unit != ADC_UNIT_1) {
            return STA_C(IO_ERR_PIN_UNSUPPORTED, OWNER_PROVIDER_GPIO_ESP, (uint64_t)(1ULL << i));
        }

        esp_err_t err = esp_adc_add_intr_pin((uint8_t)channel, adc_int_config);
        if (err != ESP_OK) {
            return STA_FROM_ESP(IO_ERR_UPDATE_FAILED, OWNER_PROVIDER_GPIO_ESP, err);
        }
    }
    return STA_OK;
}

status_rep_t p_gpio_esp_adc_read(uint64_t pin_mask, uint32_t* out_mv, uint8_t max_results_num) {
    if (out_mv == NULL || max_results_num == 0) return STA_C(IO_ERR_PORT_INVALID, OWNER_PROVIDER_GPIO_ESP, 0);

    uint8_t written = 0;
    for (int i = 0; i < 64 && written < max_results_num; i++) {
        if ((pin_mask & (1ULL << i)) == 0) continue;

        sys_pin_obj_t* pin_obj = pin_registry[i];
        if (pin_obj == NULL || pin_obj->pin_mode != SYS_GPIO_MODE_ADC) {
            continue;
        }

        uint16_t mv = 0;
        esp_err_t err = esp_adc_get_mv((uint8_t)pin_obj->hw.adc_cfg.adc_channel, &mv);
        if (err != ESP_OK) {
            return STA_FROM_ESP(IO_ERR_UPDATE_FAILED, OWNER_PROVIDER_GPIO_ESP, err);
        }

        out_mv[written++] = mv;
    }

    if (written == 0) {
        return STA_C(IO_ERR_PIN_UNSUPPORTED, OWNER_PROVIDER_GPIO_ESP, pin_mask);
    }

    return STA_OK;
}

status_rep_t p_gpio_esp_register_callback(uint64_t pin_mask, uint32_t mode, void (*callback)(void* arg), void* arg) {
    for (int i = 0; i < 64; i++) {
        if ((pin_mask & (1ULL << i)) == 0) continue;

        sys_pin_obj_t* pin_obj = pin_registry[i];
        if (pin_obj == NULL) {
            return STA_C(IO_ERR_PIN_UNSUPPORTED, OWNER_PROVIDER_GPIO_ESP, (uint64_t)(1ULL << i));
        }

        gpio_int_type_t intr_type = GPIO_INTR_DISABLE;
        switch (mode) {
            case SYS_GPIO_MODE_RISING_EDGE:  intr_type = GPIO_INTR_POSEDGE; break;
            case SYS_GPIO_MODE_FALLING_EDGE: intr_type = GPIO_INTR_NEGEDGE; break;
            case SYS_GPIO_MODE_BOTH_EDGES:   intr_type = GPIO_INTR_ANYEDGE; break;
            case SYS_GPIO_MODE_LEVEL_HIGH:   intr_type = GPIO_INTR_HIGH_LEVEL; break;
            case SYS_GPIO_MODE_LEVEL_LOW:    intr_type = GPIO_INTR_LOW_LEVEL; break;
            default: return STA_C(IO_ERR_MODE_UNSUPPORTED, OWNER_PROVIDER_GPIO_ESP, mode);
        }

        // Store callback details in the unified struct
        pin_obj->callback = callback;
        pin_obj->callback_arg = arg;

        // Apply hardware interrupt settings
        esp_err_t err = gpio_set_intr_type((gpio_num_t)i, intr_type);
        if (err != ESP_OK) {
            return STA_FROM_ESP(IO_ERR_UPDATE_FAILED, OWNER_PROVIDER_GPIO_ESP, err);
        }

        // Add the routing trampoline 
        err = gpio_isr_handler_add((gpio_num_t)i, _gpio_pin_isr_trampoline, pin_obj);
        if (err != ESP_OK) {
            return STA_FROM_ESP(IO_ERR_UPDATE_FAILED, OWNER_PROVIDER_GPIO_ESP, err);
        }
    }

    return STA_OK;
}