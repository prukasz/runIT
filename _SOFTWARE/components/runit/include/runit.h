#pragma once

#include <stddef.h>
#include "sys_error.h"

typedef err_h (*runit_boot_step_fn)(void);

typedef struct {
  const char* name;
  runit_boot_step_fn fn;
} runit_boot_step_entry_t;

err_h runit_start(void);
void runit_enter_safe_state(void);
err_h runit_run_boot_steps(const runit_boot_step_entry_t* steps, size_t count);
