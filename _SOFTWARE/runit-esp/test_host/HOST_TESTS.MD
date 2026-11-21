# Host tests

Unit tests of the firmware's components built with `gcc` on the PC, with [Unity](vendor/unity/) (the same Unity ESP-IDF ships, so a test can later also run on the board or in QEMU). The component's code is the real one; only the platform it stands on is replaced.

```
python test_host/run.py                 # every component with host/host_test.json (from runit-esp/)
python test_host/run.py errors          # one
python test_host/run.py errors -k wire  # only the test files whose path contains "wire"
python test_host/run.py --list
```

Needs `gcc` (any, 32- or 64-bit) and Python. Output goes to `runit-esp/build_host/` (not committed). Exit code 1 when any test fails.

The components are found in `runit-esp/components/` (any depth) by their folder name, which is unique (two of one name stop the runner).

## What is in here

| Path | Role |
|---|---|
| `run.py` | Finds the components, builds each test file with its sources into one executable per variant, runs it |
| `vendor/unity/` | Unity 2.6 (MIT), copied from ESP-IDF; not edited |
| `include/` | Stand-ins for what a component gets from ESP-IDF and the lower layers: `esp_err.h` (`ESP_OK`, `ESP_FAIL`, `ESP_ERR_NO_MEM`, `_INVALID_ARG`, `_INVALID_STATE` with ESP-IDF's values), `esp_log.h` (a spy: `host_log_*` read what was logged; `ESP_LOGE` .. `ESP_LOGD`; `host_log_echo` prints every line to stderr too), `utils.h` (`ARRAY_LEN`, `MUST_USE`, `NO_WAIT`, `MSEC`, `u_ringbuf_clear`; the real one pulls in all of FreeRTOS), `freertos_shim.h` (a tick is a millisecond, `xTaskGetTickCount` a monotonic clock; tasks as threads, every thread a task with its own handle; ring buffers (`SendAcquire` / `Complete`, `Send`, `SendFromISR`), task notifications, binary semaphores, mutexes with their holder (`xSemaphoreGetMutexHolder`) and recursive mutexes (`FromISR` is the same call); the critical section `portMUX_TYPE`, `portENTER_CRITICAL`, `taskENTER_CRITICAL` (one process-wide mutex); `xPortInIsrContext`, true on a thread a test made an ISR with `host_isr_set(1)`; the `U_TASK_*` / `U_RINGBUFFER_DEFINE` / `U_BINARY_SEM_DEFINE` / `U_RECURSIVE_MUTEX_DEFINE` macros), `freertos/FreeRTOS.h`, `task.h`, `semphr.h` (all three are `freertos_shim.h`), `board_time.h` (`board_time_now_ms`, `_uptime_ms` and `_at_uptime_ms`: one clock the test sets with `host_time_set`, or the PC's after `host_time_real(true)`), `host_mocks.h`, `host_assert.h` (`TEST_ASSERT_ERR_TAG(tag, e)`: an error chain is there and has this outermost tag; a missing one is a clean FAIL) |
| `src/` | Their code: `host_mocks.c` (the clock, the log spy, the critical section), `host_freertos.c` (threads, locked ring buffers, notifications) |

## Adding tests to a component

Create `components/<group>/<name>/host/`:

1. `host_test.json`:
   ```json
   {
     "sources": ["foo.c", "foo_util.c"],
     "tests": ["host/test_foo.c"],
     "config": "host/foo_host_config.h",
     "deps": ["errors"],
     "variants": {"default": [], "small": ["-DCONFIG_FOO_SIZE=4"]}
   }
   ```
   `sources`, `tests` and `includes` (optional: more include folders, when the component's `INCLUDE_DIRS` has more than `"."`, e.g. `["errors"]`) are relative to the component folder. `deps` are other components with a `host_test.json`: their sources and config are built in and their folder is on the include path (a component needs the dependencies its code includes). `variants` (optional) are extra compiler flags; the tests run once per variant.
2. `foo_host_config.h`: the component's `CONFIG_*` defaults from its Kconfig, each `#ifndef`-guarded so a variant can override it. `run.py` makes an `sdkconfig.h` that includes the config of the component and its deps.
3. `test_foo.c`: Unity, with its own `main()`:
   ```c
   #include "unity.h"
   #include "foo.h"
   void setUp(void) {}
   void tearDown(void) {}
   static void test_something(void) { TEST_ASSERT_EQUAL_INT(3, foo_add(1, 2)); }
   int main(void) { UNITY_BEGIN(); RUN_TEST(test_something); return UNITY_END(); }
   ```

Rules:
- Test state and results (bytes, return values, what the log spy caught), not calls. Use the real code of the component and of its deps; replace only what the PC does not have (FreeRTOS, drivers, the clock).
- A task is a thread and a critical section is a process-wide mutex, so what a component's tasks and a spinlock do is exercised for real; priorities, stack sizes and the scheduler's timing are not modelled. An ISR is a thread that says it is one (`host_isr_set`): the ISR side of a component runs, an interrupt's timing does not. A test that runs tasks waits on a latch (a condition the task posts), not on a sleep; only "nothing happened yet" needs a short wait. Run such a test many times before trusting it (`for i in $(seq 40); do build_host/<component>/default/<test>.exe; done`).
- A stand-in grows only when a component needs it: add it to `include/` and `src/` (shared by all), in the smallest form that compiles and lets a test observe or control the thing (a stub with a setter, a spy with a reader). Hardware-specific behaviour (ISR, spinlock, timing) is not tested here.
- The host has 8-byte pointers: a test never relies on the size of a struct with pointers. An error node's header is larger here than on the board, so a component with an error payload over 12 B builds with a larger pool block (`-DCONFIG_ERRORS_POOL_BLOCK_SIZE=40` in its variant, as `sys_device`).
- A test that fails mid-way is left by Unity's jump: what it set up for the component (a hold of events, a blocked task, a hook that waits) lives where `tearDown` can end it (static, not on the test's stack).
- Examples: `components/core/errors/host/`.
- The screen simulator `lvgl-playground/` (its README.md) builds firmware components on these stand-ins too: a change to them is checked there as well.
