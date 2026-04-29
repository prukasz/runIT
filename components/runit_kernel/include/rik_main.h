#pragma once 
#include <stdint.h>
#include <freertos/event_groups.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include "status.h"

static EventGroupHandle_t rik_events = NULL;


