#include "manager_io.h"
#include "tca6424a_mock.h"
#include "tca6424a_wrapper.h"

static manager_io_config_t* config;

status_rep_t manager_io_start(void* config_ptr){
    config = (manager_io_config_t*)config_ptr;

    
    tca_mock_set_intr_callback(&tca_isr_callback, config->gpio_expander_dev_handle);

    return STA_OK;
}
