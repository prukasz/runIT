#pragma once

#include <stdbool.h>

/* Shared between the connector's own files. */

/** True when the caller runs on the connector task. */
bool sys_connector_is_task(void);
