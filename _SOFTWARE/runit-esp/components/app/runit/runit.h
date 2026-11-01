#pragma once

#include "errors.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief The runIT application (RUNIT.MD): the boot sequence, the safe state, the error policy wiring and the
 * board's stream routing. main.c only calls runit_start().
 */

typedef err_h (*runit_boot_step_fn)(void);

typedef struct {
  const char* name;
  runit_boot_step_fn fn;
} runit_boot_step_entry_t;

/**
 * @brief Runs the boot sequence. A failing step is reported and the boot goes on, so one missing piece (no BLE
 * controller under QEMU, no I2C pull-ups) doesn't take the clients' sessions down with it.
 * @return The first failure (owned, not reported yet: the caller reports it), or NULL. The others are reported.
 */
MUST_USE err_h runit_start(void);

/** @brief Safe state: every device suspended (outputs off). */
void runit_enter_safe_state(void);

/**
 * @brief The power manager's safe state: entered through the router, which notes the reason (Power fault) for SYS SAFE_GET.
 */
void runit_power_safe_state(void);

/**
 * @brief Runs `steps` in order, all of them; failures are counted in `failed` (may be NULL).
 * @return The first failure (owned, not reported), or NULL. The others are reported.
 */
MUST_USE err_h runit_run_boot_steps(const runit_boot_step_entry_t* steps, size_t count, uint32_t* failed);

#ifdef __cplusplus
}
#endif
