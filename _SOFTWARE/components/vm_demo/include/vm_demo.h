#include <stdint.h>
#include <stdlib.h>
#include "status.h"
#include "manager_io.h"
#include "rtos_utils.h"


TaskHandle_t vm_demo_start(void);

void vm_callback_power_event(void* param);