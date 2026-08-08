#pragma once

/**
 * @file vm_bench.h
 * @brief On-target timing for the object access layer.
 *
 * Answers one question: what does an accessor resolve actually cost, and how
 * much of that is the chain rather than the data. The reference shape is an
 * 8x8 float grid -- a VM_OBJ_PTR[8] of VM_OBJ_F[8] rows, the jagged 2D form
 * described in [[VM.MD]] -- read cell by cell through progressively more
 * expensive accessor shapes, against a raw C pointer walk over the same
 * bytes as the floor.
 *
 * Results are ESP_LOG lines: cycles and nanoseconds per access, plus the
 * multiple over raw C. Enable with RUNIT_ENABLE_VM_BENCH in
 * [[runit_board_cfg.h]]; it runs once at the end of boot.
 *
 * Numbers are indicative, not a contract -- this runs with interrupts live
 * (BLE, timers), so each scenario is repeated and the best trial is kept to
 * keep ISR time out of the figure.
 */
void vm_bench_run(void);
