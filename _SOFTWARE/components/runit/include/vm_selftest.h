#pragma once
#include <stddef.h>
#include <stdint.h>
#include "sys_error.h"

/**
 * @file vm_selftest.h
 * @brief On-target smoke test for the VM object/accessor/loader stack.
 *
 * Nothing in `components/VM` had ever executed before this existed -- it was
 * all compile-verified only. This runs the layers bottom-up so a failure
 * points at one layer rather than at "the VM":
 *
 *   A  object layer via the direct API (create, tag, table, rejections)
 *   B  the upload protocol, injected as complete frames through
 *      sys_interface_decode() -- the same path a BLE frame takes, so class
 *      dispatch and the decoder's cursor walk are covered too, not just the
 *      loader behind them
 *   C  malformed frames: truncation, bad ids, out-of-range writes
 *
 * Results go to the serial log as PASS/FAIL lines with a final tally.
 * Expected-failure cases inspect the returned err_h rather than pushing it,
 * so a clean run produces no error telemetry.
 *
 * Enable with RUNIT_ENABLE_VM_SELFTEST in [[runit_board_cfg.h]].
 */

/** @brief Run every stage and log a PASS/FAIL tally. Leaves the VM empty. */
void vm_selftest_run(void);

/**
 * @brief Inject one raw frame as if it had arrived over the wire.
 *
 * Class byte included -- e.g. `{0x04, 0x40}` resets VM storage. Returns the
 * decoder's own err_h without pushing it, so a caller can probe a frame and
 * inspect the outcome without generating telemetry.
 *
 * @code
 * const uint8_t reset[] = {0x04, 0x40};
 * CHECK_AND_LOG(vm_selftest_inject(reset, sizeof(reset)));
 * @endcode
 */
err_h vm_selftest_inject(const uint8_t* frame, size_t len);
