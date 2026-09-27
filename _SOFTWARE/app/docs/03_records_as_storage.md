# Stored code: the app builds the project into frames, the board keeps them

Status: **built** (2026-09-26), replaces the "records + metadata" draft of the
same day. F1–F4 and A1–A6 done (section 7); the app side (A4–A6) is not yet
tried on the board. Firmware work is marked **F**, app work **A**.

## 1. Idea

Two layers, each doing one job:

- **The app's project** is what the user edits: every selected option (object
  tree, BLE services and characteristics, connectors, devices, later canvas and
  remote), saved as the app's own project JSON. This is the source.
- **The stored code on the board** is the build output: when the user decides
  the code is ready, the app generates the complete list of command frames that
  configures a board from its defaults and uploads it into the board's NVS. The
  board replays it at every boot (the **startup config**).

Nothing changes for live work: Apply, upload, run, write and subscribe still
send their packets right away (the **running config**). Storing is a separate,
explicit "code is ready" step, like building and flashing a release.

Reading the stored frames back is **recovery**: it restores the data (names,
types, values, flags, bindings, UUIDs) into a new project when the source file
is missing. It is not the normal way to open a project, so it doesn't need to
be 1:1; editor-only information (IDs, descriptions, layouts) comes later as a
metadata template / settings (section 8).

## 2. Separate from actions

`sys_actions` stays what it is: runtime recordings (e.g. a device state loaded
when an error occurs), replayed on demand, 2 KB each, recorded from live
traffic by the interface tap. The project store is its own component
(`sys_project`), class and storage. It shares only the frame list format
(`[u16 len][frame]`, no seq byte) and the replay path
(`sys_interface_decode()` with RX suspended).

A stored project may invoke actions (a `0x03` frame), but it doesn't record,
contain or replace them.

## 3. Why the app writes the script instead of the board capturing it

Capturing live frames (like action recording) was considered and rejected:

| Capture live frames | Write the script as data |
|---|---|
| On a board already configured, creates fail (`ERR_DEV_ALREADY_EXIST`), so the stored project silently misses them | The script is built from board defaults by the app; the board state doesn't matter |
| The tap copies frames before decode: refused frames are stored | Nothing runs while storing; the script is checked by the app's compilers, and the boot replay reports every refused frame |
| Shares the one interface tap with action recording | No tap involved |
| The running config changes while storing | Storing doesn't touch the running config; loading is a restart |
| Size bound by RAM while recording | Streamed to flash in chunks |

## 4. Board side (`sys_project`)

**Storage.** Its own NVS partition `project` (custom `partitions.csv`, e.g.
512 KB, `CONFIG_SYS_PROJECT_PARTITION_SIZE`; the default `nvs` partition stays for settings, actions, BLE). NVS can't
append to a blob and a whole program doesn't fit RAM, so the frames are stored
as chunk keys written as they arrive, in two generations for an atomic replace:

```
p<g>_000, p<g>_001, …   chunks of the frame list (generation g = A or B, ~4 KB each)
p_active                header of the committed generation:
                        magic "RPRJ", format version, generation, frame count,
                        byte length, CRC-32, errors schema ID
frames (concatenated):  [u16 len][class][packet][payload] …
```

A store writes the inactive generation; commit checks it and rewrites
`p_active` (one NVS commit), so a failed or interrupted store keeps the old
code. The flash size is the board's 16 MB, which leaves room for the partition.

**Packets** (class `0x0A`, `CONFIG_RX_PACKET_CLASS_SETTINGS_PROJECT` in `dec_settings_project.h`, annotated so
the settings generator publishes them to the app):

| Packet | Payload | Does |
|---|---|---|
| begin | `u32 length` | Erase the inactive generation, expect `length` bytes |
| write | `u32 offset`, `data[]` | Write a chunk (fits one frame) |
| commit | `u32 crc32`, `u32 schema_id` | Check CRC and that every length prefix fits; refuse frames of class `0x0A`, action recording and VM exec (`0x48`); make the generation active |
| abort | — | Drop the inactive generation |
| erase | — | No project at boot (defaults only) |
| info | — | Response: stored code present, length, frame count, CRC, schema ID, last boot's replay report |
| read | `u32 offset`, `u16 length` | Response data: bytes of the stored frame list (recovery) |
| load | — | Restart the board, which replays the project |
| options | `u8 autostart` | Store the project options in `sys_settings` (`prj_opts`); `info` returns them |

**Boot replay.** After the board's own init (devices, BLE service, connector
bindings), the stored frames go through `sys_interface_decode()` with RX
suspended. A refused frame is reported (error stream) and skipped; the replay
continues. The report (frames, applied, failed, first failing index and its
error tag) stays in RAM for `info`, since no app is connected at boot. A schema
ID that differs from the firmware's is reported and the replay is skipped.

**Autostart** is a board setting, not part of the stored code: `sys_settings`
key `prj_opts` (`{autostart}`), set with the `options` packet. After the replay
the VM starts (`VM_EXEC_NORMAL_MODE`) only when autostart is on and every VM
frame applied. Stored code never contains `0x48` exec frames (commit refuses
them).

## 5. App side

**Script builder** (`domain/storedCode/build.ts`, `buildStoredCode`): model → frames from board
defaults, in replay order:

1. BLE creates: the settings planner with `from = board defaults` already
   produces exactly these;
2. data connector creates, binds, suspends (same planner);
3. other classes kept from the file untouched (devices `0x01`, power `0x08`,
   events `0x09` … until they get editors);
4. VM: `0x41` open, `0x42` objects, `0x43` values and links, `0x47` subscribe
   (`planVmUpload`).

Autostart is sent separately (`options`), from a project setting.

**Store**: build → begin → write chunks → commit (CRC) → info to confirm.
**Load**: `load` packet (restart), reconnect, `info` for the replay report.
**Restore**: `info` → `read` chunks → CRC check → frames → decoders → project.

**Decoders** (restore), one per class, data only:

| Class | Becomes |
|---|---|
| `0x02` BLE | user services and characteristics (UUID, flags, buffers, name) |
| `0x06` connectors | user connectors (ID, header, length, name), bindings, suspended |
| `0x04` VM | objects from `0x42` (name, type, length, flags; header decoded with `vm-model`), values from `0x43`, folders from PTR children (an object linked under a second folder becomes a reference), `subscribed` from `0x47` |
| anything else | kept as raw frames and written back unchanged |

Project IDs are generated on restore (no metadata yet).

**Project file** stays the app's own JSON (`runit-project`): the selected
options, not frames. It grows a `settings` section (BLE services and
characteristics, connectors) so they are saved with the project instead of in
browser storage / memory. The generated frames are an output; the app can
export them (the `runit-upload` bundle becomes this export) but never edits
them.

**Running vs stored.** The app compares the CRC of the script it would generate
with the board's stored one (`info`) and shows "code not stored on board" when
they differ. Live changes are expected to differ from the stored code until the
user stores again. `info` also returns `replay_crc32`, the CRC of the code this
boot replayed: when it differs from the stored CRC, the code was stored
without a restart and the board still runs the older one.

## 6. What it simplifies

- Settings baseline per device and "Board restarted": after a restart the
  running config is defaults + the stored code. The live Apply planner stays,
  with `from` = board defaults + the decoded stored code (read once on
  connect) instead of a guess.
- Block-owned objects and every other generated part are just more frames in
  the output; the `runit-objects` section files were removed (2026-09-26).
- Round-trip test per class: `decode(build(model))` keeps the data.

## 7. Plan

Each step ends green (app `npm test` + build; firmware builds; PCB checks with
the host scripts in `runit-esp/scripts/`).

### Firmware

| Step | Work | Done when |
|---|---|---|
| F1 ✅ | Custom `partitions.csv` with a `project` NVS partition (512 KB at 0x210000), flash size 16 MB, factory app 2 MB | Board boots; `nvs` kept its offset (data survived) |
| F2 ✅ | `components/system/sys_project`: begin / write / commit / abort / erase / info / read, chunk keys in two generations, CRC-32 read back from flash, frame check, error tags `0xB2xx`, `SYS_PROJECT.MD` | PCB: 151 KB stored twice (~57 s each over USB), head / tail byte-identical; refusals keep the old code |
| F3 ✅ | `dec_settings_project.h` (`//@settings project`), class `0x0A` (`CONFIG_RX_PACKET_CLASS_SETTINGS_PROJECT`); the settings generator now publishes `_response_t` layouts; app catalog supports trailing `uint8_t data[]` (kind `bytes`) | App catalog test packs `write`, reads `info` |
| F4 ✅ | `runit_project_boot` after `vm_exec_start`: replay with RX suspended, report in RAM (`info`), schema / CRC check, autostart from `prj_opts`; `load` restarts after 300 ms | PCB (`project_store_tests.py` 21/21): service present and VM running after load; erase + restart back to defaults |

### App

| Step | Work | Done when |
|---|---|---|
| A1 ✅ | Frame list codec: stored bytes ⇄ frames ⇄ export JSON (`runit-code` v1), CRC-32 (`domain/storedCode/frameList.ts`) | Round-trip tests; zlib CRC-32 = the board's `esp_rom_crc32_le(0, …)` (confirmed on the PCB) |
| A2 ✅ | Builder `buildStoredCode` (section 5): settings from `boardDefaultSettings()`, extra frames passed through, VM program; frames up to frame max − 1; exec frames refused | `storedCode.test.ts` |
| A3 ✅ | `decodeStoredCode`: settings replayed onto the board defaults, VM objects / values / folders / references / subscriptions / block section (`upd_resetable`), unknown classes kept; VM accessors and blocks not decoded yet | Recovered settings need no command; recovered program compiles to the same frames |
| A4 ✅ | Project JSON: `settings` (BLE profile, connectors; editor types in `domain/project/document.ts`), `autostart`, `extra_frames`; BLE and connector editors auto-save to browser storage and load from Open; `domain/settings` refreshes system entries from the descriptors and turns decoded settings back into editor settings; `runit-upload` bundle removed, the Run tab exports `runit-code` | `settings.test.ts`: file round trip, refresh, recovered settings need no command |
| A5 ✅ | Run tab "Stored code" (`StoredCodeSection.tsx`, `boardCode.ts`): store (begin / write chunks / commit / info check, autostart option), load (restart), recover (read, CRC, decode → project), erase, export; board info, replay report with the first refusal named, "differs / not stored" state, schema mismatch | tsc + build; **PCB check pending** (board unplugged) |
| A6 ✅ | `useBoardCode` reads info and the stored frames up to the first VM frame once per connection; its settings are the Apply baseline (`useSettingsSync`, then what was applied on that connection); per-device baseline and "Board restarted" removed | tsc + tests; **PCB check pending** |

Order: F1–F2 and A1–A3 in parallel (no dependency), then F3–F4, then A4–A6.

## 8. Later: metadata

Editor-only information (stable IDs, descriptions, auto-type, long names,
canvas and remote layouts, project templates, app settings) as a separate
layer: either a metadata frame class the board stores and ignores, or a
template / settings file next to the project. Restore works without it; with
it, restore becomes 1:1.

## 9. Risks

- **Positional wire IDs**: a recovered VM program has the board's object order;
  without metadata, project IDs are generated on recovery (the source JSON keeps its own).
- **Descriptor drift**: a stored project built for another firmware; the schema
  ID check skips the replay instead of misapplying it.
- **Replay vs. the link**: the replay runs before the app can connect, so it
  can't cut an active link; a user BLE change that breaks the command link is
  still refused by the app's planner before storing.
- **Store time**: a 200 KB write in 128-byte chunks at ~110 ms per command is
  ~3 min over BLE; chunk size grows with the link (UART, larger MTU), and
  typical projects are a few KB.
