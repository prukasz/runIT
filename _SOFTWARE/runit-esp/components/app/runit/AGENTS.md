# runit

Read `RUNIT.MD` in this folder before changing anything here.

- Nothing depends on this component; it may use every module. Keep `main` to `runit_start()`.
- New wiring between two modules that can't know each other goes here, in `connectivity/`: the board's data are the tables of `connectivity/data_matrix_setup.c` (`k_providers`, `k_sessions`, `k_modules`, `k_streams`) applied in one pass, not code in `runit.c`. A module registers its own ports (named by its module number); where they go is links, here.
- Keep the order of `runit_start()`: the console before the GPIO device, the board's profile last (its frames name the GPIO's pins).
- The board's other ways (a UART to a slave, MQTT, more than one of each) are not made here: they are frames of the board's record (`SYS_BOARD.MD`): the provider made, its session attached, its links connected, like any provider. Nothing here assumes a link.
- The system ids (`runit_ids.h`) are the board's: the app skips their `CREATE` (`boardProfile.ts`). Keep the two in step.
- A new `device_*` or `provider_*` goes in `PRIV_REQUIRES` of `CMakeLists.txt` (their own CMake links them whole).
- Docs: `RUNIT.MD` and the skill `runit-esp`.
