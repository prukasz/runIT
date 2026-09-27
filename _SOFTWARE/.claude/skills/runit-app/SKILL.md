---
name: runit-app
description: App skill for the runIT phone/PC app (Vite + React + TypeScript + Tailwind, Web Bluetooth) — remote, debugger, configurator and block-canvas IDE. Load for any work under app/.
---

# runIT App — Skill

> STUB — grows as the app is built. Legacy app design docs are discarded (2026-09-25); design from `features.md` and `app/docs/`.

## Code map (app/src)

| Path | What |
|---|---|
| `backend/` | BLE transport, packet packer, stream router, `CommandClient` |
| `domain/descriptors/` | Typed catalogs from `data-structures/` (`runitCommandCatalog`, `runitErrorCatalog`, `runitVmCatalog`, …) |
| `domain/decoder/` | Error packets, log lines, stream routing |
| `domain/project/` | Project document (one JSON, `runit-project`): object tree, `settings` (BLE profile + connectors, editor types), `devices`, `actions`, `autostart`, `extra_frames`; load / save, tree edits. `ObjectSection`: a slice of the object space (`key` + `owner` user/block); block sections come from blocks later, today only from a recovery (kept in memory) |
| `domain/settings/` | Settings in editor form: board defaults, `refreshSettings` (system entries from the descriptors), `settingsFromState` (decoded stored code → editor), `settingsState` (→ planner) |
| `domain/storedCode/` | Stored code: frame list + CRC-32, `buildStoredCode`, `decodeStoredCode` (recovery), `runit-code` export |
| `domain/compiler/` | Project (+ extra sections) → VM load frames (`0x41`–`0x43`) with pre-upload checks; `0x47` subscribe (from `subscribed` flags), `0x48` exec, runtime `0x43` writes |
| `domain/upload/` | Everything sent to the board as `UploadStep`s (label + frame): `planVmUpload`, `planSettingsUpload` (BLE `0x02` + connectors `0x06`, a diff from what the board holds), `boardDefaultSettings` |
| `sendSteps.ts`, `useSettingsSync.ts`, `ProgramPanel.tsx` | Send steps over the session (stop at first refusal, error named from the catalog); settings apply against the board baseline; Run tab: upload / run / pause / live values |
| `devices/` | Board view: `useDevicesWorkspace` (user devices + actions, undo, auto-save `runit.devices`), `DevicesPalette` (system / user devices, actions), `DevicesEditor` (device types to add, device page: tags, tile icon / image, install form, datasheet, user guide from `app/docs/devices/<type>.md`; action composer over the dimmed app), `DeviceDetails` (Commands, grouped: Device (install, uninstall, apply defaults, reset …), Pins, Regulator …; sent live, add to action) |
| `domain/devices/` | Device IDs, install and contract frames (device ID from the device), device checks, action build / size / record frames |
| `domain/descriptors/deviceCatalog.ts` | `runitDeviceCatalog()`: device types from `data-structures/devices/*.generated.json` (install command, pin groups, contracts, datasheet), board devices, lifecycle commands |
| `boardCode.ts`, `useBoardCode.ts`, `StoredCodeSection.tsx` | Stored code over class `0x0A` (info, store, read, options, erase, load); per-connection board info + settings baseline; Run tab section: store / load / recover / erase / export, replay report |

**ID rule:** the project file stores stable string IDs only. The device has three dense wire ID spaces (objects, accessors, blocks, each 0..count-1); the compiler assigns them on every compile and returns the map back. The object space is a list of sections, numbered in order: the user's tree first, then block-owned objects (outputs, ENO; `upd_resetable` 1). References may cross sections; IDs must be unique across all of them.

**Firmware values come from `data-structures/`, never literals.** Board defaults (GATT profile with flags and buffer bytes, connector bindings per provider incl. `#if CONFIG_` ones, connector registry limits, per-connector frame cap) are published by `generate-streams.py` into `streams.generated.json` → `runitStreamCatalog().board`; VM per-packet record sizes and batch limits (`vmCatalog.wire`), `payload_size` range (`payloadMax`, `maxElements`) from vm-program / vm-model. Only Bluetooth SIG constants (ATT 512, standard services 0x1800/0x1801/0x180A) and C type ranges stay in code. After a firmware change: rerun the generators, then `npm test`.

**Stored formats:** the project JSON is the source of truth; upload bundles are derived (inspect, diff, replay) and never edited by hand.

**Settings baseline:** the board can't report BLE / connector state, but after boot it runs its defaults + its stored code. `useBoardCode` reads the stored code's settings frames once per connection; `useSettingsSync` compares against them, then against what was applied on that connection (a restart drops the link, the next connection reads again). System services / characteristics / connectors are never removed, and the interface connector must stay on the link's characteristics.

**Project file:** Open / Save (Settings and Code views) write objects + settings together (`App.tsx`); each workspace also auto-saves to browser storage (`runit.project`, `runit.ble.profile`, `runit.connectors`). Terminal panel: Commands (`CommandConsole.tsx`) and Errors & logs (`DiagnosticsConsole.tsx`).

## Lookups

| File | Use it for |
|---|---|
| [features.md](features.md) | Target features with stable IDs (P-, SH-, SET-, DEV-, ACT-, OBJ-, CAN-, CMP-, DBG-, REM-, CON-, LNK-, ERR-, IO-), firmware links and gaps, decisions (survey 2026-09-24), accepted tooling stack. Read before designing or building any view |
| [target-features.md](target-features.md) | The user's raw feature list (source of features.md, kept verbatim) |

Session context and progress: see the `runit-root` skill ([../runit-root/SKILL.md](../runit-root/SKILL.md), [../runit-root/PROGRESS.md](../runit-root/PROGRESS.md)).
