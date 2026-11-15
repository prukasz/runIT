#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Starts the firmware (RUNIT.MD): the modules in their order, the board's own devices, providers and streams,
 * the error sink, then the board's profile. Returns when everything is started; the tasks run on.
 */
void runit_start(void);

#ifdef __cplusplus
}
#endif
