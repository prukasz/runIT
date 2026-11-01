# Implementation plan: from `_wire_draft/` to the firmware

For the agent that implements the wire design. `_wire_draft/` is the specification: its headers and `*.MD` say what each module is when done. This file says in which order to move it into `runit-esp/` and the app, how to check each step, and when to stop and ask.

## Before any step

- Load the skills `runit-root` and `runit-esp`; build, flash and test only with the commands of `.claude/skills/runit-esp/build.md` (the generic ESP-IDF command fails on this machine). For the app read `runit-app/AGENTS.md` and `runit-app/src/backend/BACKEND.md`.
- Repo rules (CLAUDE.md) hold for every step: the docs follow every change in the same commit (the component's `*.MD` and `AGENTS.md`, the skills, `templates/AGENTS.md`, `.claude/memory.md`); docs describe the current state only, no history; C formatted by hand to the surrounding style (120 columns, aligned `#define` values and X-table arguments).
- A module moved into the firmware leaves the draft: its `*.MD` and headers go into the component, its folder in `_wire_draft/` is deleted, and references to it in the other draft files point to the component.
- Never run plain `graphify update` or `graphify hook install`.
- Do not commit, push or merge unless the user asks. Ask before flashing if the port is not certain (build.md "Choosing the port").

## Where each draft part goes

| Draft | Target | Step |
|---|---|---|
| `cmd/` | `components/core/cmd` (moved: the draft folder is gone) | 1a |
| `router/` | `components/core/router` (moved: the draft folder can go) | 1b (b1, b4) |
| `payloads.h`: errors (`err_record_cmd_t`, `err_record_parse`) | `components/core/errors` (moved) | b2 |
| `payloads.h`: root keys, error modules | `router_ids.h`, `err_modules.h` (provider, session moved; the watch's left) | with the module that takes them |
| `sys_data_provider/` | `components/sys/sys_data_provider` (moved: the draft folder can go) | b3 |
| `sys_session/` | new `components/sys/sys_session` | b4 (client only), 3 (the rest) |
| `sys_data_connector/` | `components/sys/sys_data_connector` | b5 (subscribers), 4 (mirrors) |
| `sys_event/sys_event_filter.h`, `payloads.h`: sys_event | `components/sys/sys_event` | 2, 5 |
| `sys_watch/` | new `components/sys/sys_watch` | 2 (watches), 4 (client, cached reads) |
| `sys_device/` | `components/sys/sys_device` | 4 |
| `sys_remote/` | new `components/sys/sys_remote` | 4 |
| `payloads.h`: vm_blocks, runit | `components/vm/vm_blocks`, `components/app/runit` | 4, 5 |
| `sciezka-danych.html` | stays: the explainer artifact, updated when a step changes what it shows | – |

## Step 0: preconditions

- The direction byte (`[key][dir][...]`, read / write sides) is in place: firmware, descriptors, app, the Python suites and the docs. It is in `main`.
- The base is the branch `proxy`: `main` plus the connector as its byte (connector, provider, endpoint), the router's links as the connectors' providers, `sys_connector` renamed `sys_data_connector`, the connector and link config checked at build time, and the app's canvas work. The draft is written against it (`sys_data_connector`, the links). The work is done on `proxy` itself.

## Step 1a: inside the firmware, no wire change

One commit. The firmware behaves as today.

| # | Change | Files | Check |
|---|---|---|---|
| 1 | One frame size: `CMD_FRAME_MAX` 512, `CMD_HEAD` 5, `CMD_DATA_MAX` 507 in `cmd.h`. Remove `CONFIG_ROUTER_FRAME_MAX`, `CONFIG_SYS_DATA_CONNECTOR_FRAME_MAX`, `CONFIG_SYS_BLE_TX_ITEM_MAX` and the `_Static_assert` between them; `sys_actions.c` and `sys_project.c` take `CMD_DATA_MAX` | `core/cmd`, the three Kconfig, `sys_actions.c`, `sys_project.c` | build (Kconfig removed: reconfigure, build.md §3) |
| 2 | The handler signature `cmd_f(ctx, cmd_in_t in, cmd_out_t* out)`; `cmd_reply`, `cmd_append` on `cmd_out_t` | `cmd.h`, `cmd.c`, the `ROUTER_OP*` macros (about 70 handlers), the contracts' handler macros, the handlers with a hand-written `CMD_F_PARAMS` (`grep -rl CMD_F_PARAMS`: `sys_power_manager_router.c`, `vm_router.c`) | build; `router_tests.py`, `vm_suite.py` on the PCB |
| 3 | `cmd_name_arg` moves into `sys_ble_router.c`, its one user | `core/cmd`, `sys_ble/router` | build |
| 4 | actions READ by offset instead of `router_answer_part`: `u32 id, u16 offset` → the image's bytes from there (the mirror of WRITE); the app's `readProject` reads up to INFO's `bytes` and checks `crc32` | `sys_actions_router.c`, the app's project code, the scripts that read actions | `board_tests.py`; the app reads a stored project |

Done when: build clean, the three suites pass on the PCB, the app (Electron, `npm run app`) works as before.

Status: done on `proxy` (commit `594afce9`). Checked on the PCB with b1 on top (below).

## Step 1b: the wire change

On the branch. b1 to b5 each build, but the protocol is whole only after b4: the app and the scripts change with them, and they reach `main` together.

| # | Change | Spec | Files | Check |
|---|---|---|---|---|
| b1 | The router with a callback: `router_submit(cmd, done, ctx)`, `router_inject(req, flags, done, ctx)`, `router_is_task`, one answer per command. The old links stay for now as users of `submit` with their own `done` | `router/ROUTER.MD`, `router.h` | `core/router` | the suites as today: the wire is unchanged |
| b2 | `err_cmd_t` = {req, req_len, seq u16, src, flags}; the record's command header `err_record_cmd_t`; `err_record_parse` | `payloads.h` "errors" | `core/errors` (`err_wire.c`), `runit_error_policy.c`, `sys_actions` | the app's `errors.ts` and the scripts decode the new record |
| b3 | The provider registry (`sys_data_provider`: id, one user, `NOWAIT`, `URGENT`, `FRAMED`, `WIRED`); `sys_ble` and `sys_uart` as its drivers. UART always binary: COBS + CRC16, 0x00 ends a frame; log lines only as LOGS frames | `sys_data_provider/` (with "UART") | new `sys/sys_data_provider`, `sys_ble/link` → its driver, `sys_uart.c` | the scripts over UART: the new framing in `router_link.py`; `serial_capture.py` shows the bytes that are no frame as text |
| b4 | The session, client only: header `[frame][dst][src][seq u16]`, HELLO with `your_id`, PING, NO_SESSION, its timeout as the dead-man (out of `sys_system`), answers by `session_answer`. A root without children: no forwarding, calls or GONE yet (step 3). The router's links go | `sys_session/` (the client parts) | new `sys/sys_session`, `router`, `sys_system`, `runit.c` (boot, wiring) | the app: `frames.ts`, `services.ts` (header, HELLO, PING, client id); the scripts; the app in Electron over BLE on the board (verify there, through CDP, not in a separate Chrome) |
| b5 | Connectors on the session: a subscriber is the `src` of SUBSCRIBE, STREAM frames go through the session; the board config binds LOGS to the LOGS characteristic | `sys_data_connector/` "Subscribers" | `sys_data_connector`, `runit_board_cfg.c` | the app subscribes LOGS, ERRORS, EVENTS, TELEMETRY; `vm_suite.py` telemetry |

Status b1: done on `proxy` (commit `5890ca04`). `router_tests.py` 207/207; `board_tests.py` 331/334, `vm_suite.py` 959/960 and `npm run test:hw` 96/99 with the same failures on `c0e76ef4` (before 1a): ADS7128 VOLTAGE read while suspended answers OK, the DC motor's SPEED through stop (timing) and its records of device 0x15, the VM's "execution order is upload order" and the app's SUBSCRIBE "a block runs when its input changes" (both still expect the freshness rule `4d28581c` removed); the BLE reconnect tests of `a-link` fail now and then (5 s window). Not checked: the app in Electron. b1 also takes requests of at most `CMD_DATA_MAX` (the app sizes them by `CMD_HEAD`).

Status b2–b5: done on `proxy` (not committed yet).
- The firmware builds clean. On the PCB (COM9): `router_tests.py` 231/231; `board_tests.py` 331/334 and `vm_suite.py` 959/960, with only the failures older than 1a (ADS7128 VOLTAGE while suspended, the DC motor's SPEED through stop (timing) and its records of device 0x15, the VM's "execution order is upload order"). `vm_suite`'s 1 ms PERIODIC reports more episodes while a second client (the app over BLE) is subscribed to telemetry: run the suites with no other client. `npm run test:hw` passes but for the old SUBSCRIBE "a block runs when its input changes"; the app's unit tests 641/641. The app in Electron connects over BLE, opens its session and reads the board.
- Firmware: `err_cmd_t` with `seq u16` and `src`, `err_record_cmd_t`, `err_record_parse` (`ERR_RECORD_CUT`, a core tag: `errors` can't use `ERR_CMD_*`); `sys_data_provider` (registry, router 0x0D); `sys_uart` binary (COBS + CRC16, a 0x00 before and after each frame; a UART TX buffer so USB is never held to 115200 baud; the USB-Serial-JTAG written in pieces); `sys_ble` the driver of two providers (the runIT pair, LOGS); `sys_session` the client parts (HELLO with `your_id`, PING, NO_SESSION, timeouts, `session_answer`, streams, gone and reached listeners, router 0x0E with ID in NVS); the router without links (`ROUTER_SRC_SELF`, `router_report_cmd` public, `ERR_ROUTER_BAD_LINK` and the Kconfig of links and the response byte gone); the dead-man as a gone listener (DEADMAN `{u32 timeout_ms}`, `SYS_SAFE_LINK_LOST` gone); the connectors with provider bindings, subscribers (SUBSCRIBE 0x09, UNSUBSCRIBE 0x0A, LIST 0x07, UNBIND of link 0: every binding), the logs bound to the BLE LOGS characteristic; `mirror_serial` off by default.
- Not built from the drafts yet: a connector's rx, forward, coalescing and mirrors (step 4); the session's tree, forwarding, calls to another board, GONE (step 3). The HELLO's board type, firmware and descriptors' hash are sent as 0.

Done when: build clean; `router_tests.py`, `board_tests.py`, `vm_suite.py` pass over UART on the PCB; `npm run test:hw` passes; the app in Electron connects over BLE, reads devices, runs a program and gets logs and errors.

Risk: b4 changes the protocol at once in the firmware, the app and the scripts. Keep b1 to b3 such that b4 changes only the frame header and the opening of a connection. If a suite fails after b4 and the cause is not clear within the step, stop and report with the output; do not loosen a test to pass.

## Later steps

Each its own branch and commit, in this order; each starts from its spec in the draft.

2. **Watches on one board** (`sys_watch/` without "Client" and "Cached reads"; `sys_event/sys_event_filter.h`): period in 10 ms ticks, ON_CHANGE, ON_EVENT, entries to the owner. The app's views stop polling. Check: the app follows a value with one ADD.
3. **Two boards over UART** (the rest of `sys_session/`): child and parent sessions, the route table, forwarding, broadcast, GONE, `sys_session_call` and `call_wait`, the timeouts by way, the calls in flight per board, the time broadcast. Check: two ESP32 on a cable; the app reaches a device of the child through the root.
4. **Remote devices** (`sys_data_connector/` "Mirrors", `sys_watch/` "Client" and "Cached reads", `sys_device/`, `sys_remote/`, `payloads.h` vm_blocks): a VM program on board 1 drives a servo on board 2. Check: `async` and `blocking` devices, a driver whose `pin_ref` is a `blocking` remote expander.
5. **`EVENTS_ALL`, `SYS_EVENT_LOCAL`, the board as an error source** (`payloads.h` sys_event, errors, runit). Check: ON_EVENT on board 1 fires on a button of board 2.

## Not in this plan

Designed later, do not start them: a VM on several boards at once (program split, remote variables, common start and stop), a history of log lines, the debugger's views (`LIST` fields, `CLIENTS`, a remote device's `INFO`), the app's side of several boards and remote devices, OTA and the flasher, the MQTT and ESP-NOW provider drivers, a command that creates a UART provider.
