# time

Spec: `TIME.MD` (this folder).

- Every change in this folder is followed by an edit of this file, if it is needed.
- The header stays `board_time.h`: a `time.h` here would shadow libc's on every dependent's include path.
- The clock is the system clock (`settimeofday` / `gettimeofday`); no second copy of the time is kept here.
- `BOARD_TIME_SET_AFTER_MS` is the one definition of "never set". Nobody else compares against 2020.
- Anything that needs the time takes it from here, not from `gettimeofday` or `esp_timer_get_time` directly. The VM's program clock (`vm_clock.h`) is the exception on purpose.
- Decoupling invariant: `time` depends on ESP-IDF only (`esp_timer`), so every layer can use it.
