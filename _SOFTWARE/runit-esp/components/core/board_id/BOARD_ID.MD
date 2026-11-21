# board_id — the board's identity

Component `components/core/board_id/` (in `core`: the id is the header's `src`, CMD.MD "The header"). The board's id, its boot id and its firmware's build, read once at boot. The logic is `board_id.c`; what is this chip's (the random number, the firmware's description) is `esp/`, behind `internal/board_id_port.h`.

## Files

| File | Role |
|---|---|
| `board_id.h` / `board_id.c` | `board_id_init`, `board_id`, `board_id_save`, `board_boot_id`, `board_build_id` |
| `internal/board_id_port.h` | What it takes from the chip: `board_id_port_random`, `board_id_port_build` |
| `esp/board_id_esp.c` | ESP-IDF's `esp_random` and the app's description (the first 4 bytes of its ELF SHA-256) |
| `Kconfig` | `CONFIG_BOARD_ID_DEFAULT` |

## The identity

- **The id** (`board_id()`): the board's place in the project, `CMD_SRC_NONE` (0, none given) or `0x01..0xEF`. `board_id_init` reads it from the store (partition `nvs`, namespace `board_id`, key `id`; none kept, or a value no board may have: `CONFIG_BOARD_ID_DEFAULT`, 0). `board_id_save(id)` keeps another in the store: it is the board's from the next start, so nothing that has the id changes it under way. The app sets it with the session's ID (SYS_SESSION.MD "Remote").
- **The boot id** (`board_boot_id()`): random, drawn once at `board_id_init`: a peer that sees a new one knows the board restarted (the session's hello).
- **The build** (`board_build_id()`): the first 4 bytes of the firmware's ELF SHA-256: whether two boards run the same build (the session's hello).

`board_id_init` runs at boot after `nvs_store_init("nvs")` and before anything reads them (`app/runit`).

## Tests

Host tests in `host/` (`python test_host/run.py board_id` from `runit-esp/`, `HOST_TESTS.MD`), on `nvs_store`'s RAM implementation: the default id and the boot and build ids; a saved id reloaded, an id out of range refused; a corrupt stored id falling back to the default.
