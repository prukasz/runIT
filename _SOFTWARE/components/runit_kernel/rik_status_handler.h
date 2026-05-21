#pragma once 
#include "status.h"
#include "freertos/Task.h"


void rik_status_handler_start(RingbufHandle_t status_buffer, TaskHandle_t supervisor_task_handle);
