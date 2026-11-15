# runit

Read `RUNIT.MD` in this folder before changing anything here.

- Nothing depends on this component; it may use every module. Keep `main` to `runit_start()`.
- New wiring between two modules that can't know each other goes here, as its own file (`connectivity/` for what reaches the app), not in `runit.c`. A module that can make its own stream or listener does it itself (`sys_log` makes `LOGS` and `ERRORS`).
- Keep the order of `runit_start()`: the console before the GPIO device, the board's profile last (its frames name the GPIO's pins).
- The board's other ways (a UART to a slave, MQTT, more than one of each) are not made here: they are frames of the board's record (`SYS_BOARD.MD`), made and attached like any provider. Nothing here assumes a link.
- The system ids (`runit_ids.h`) are the board's: the app skips their `CREATE` (`boardProfile.ts`). Keep the two in step.
- A new `device_*` or `provider_*` goes in `PRIV_REQUIRES` of `CMakeLists.txt` (their own CMake links them whole).
- Docs: `RUNIT.MD`, the row of `REWORK.md` and the skill `runit-esp`.
