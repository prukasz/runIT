# Auto-annotations

## Purpose

Generate web-app JSON descriptors from annotated C headers. C remains the source of truth; generated JSON is output only.

- `devices/`: generated device descriptors.
- `schema/`: JSON schemas for generated structures.
- `auto-annotations/`: generators and annotation grammar.
- `vm/`: generated VM model descriptors.

## Rules

- Read `auto-annotations/device/device-annotations.md` before changing annotation syntax, device headers, or the device generator.
- Read `auto-annotations/vm/vm-annotations.md` before changing VM annotations or the VM generator.
- Read `auto-annotations/settings/settings-annotations.md` before changing runtime settings annotations or the settings generator.
- Do not edit `*.generated.json`, nor a VM block's `<name>.content.json` (generated from its header); change C annotations, schema, or generator, then regenerate. A block's `<name>.display.json` is hand-written (what the editor shows).
- `//@...` is a header directive; `@...` is field or enum-member metadata. Device headers use records instead: `//#device`, `//#self-property`, `//#property`, `//#contract`, each continued by `//  @tag` lines (two or more spaces).
- `//#ref-enum` explicitly publishes an enum. `$SYMBOL` must resolve uniquely to a member of one published enum; unresolved or ambiguous references are errors.
- A published enum member may take its value from Kconfig (`MEMBER = CONFIG_X,`); the enum generator resolves it from `sdkconfig` (an unresolved `CONFIG_*` is an error). Used for IDs the app must match, e.g. `sys_data_connector_id_e`, `runit_data_provider_e`. Regenerate after changing such an option.
- `//#self-property` defines local literal choices; `//#property` defines reusable global-enum choices; `@arg` reuses either (with its alias and note) in a contract parameter.
- `//#contract` must name an existing packet struct. Give every packet field marked `@required` a `@param` line; `device_id` is generated automatically.
- Write only tags the app reads; the device generator rejects any other tag. A pin on another device is a `sys_io_pin_ref_t` install field. New device headers start from `auto-annotations/device/device-template.txt` (every tag, with what the app does with it).
- Keep annotations next to the C declarations they describe. Add a new directive only with documented grammar, generator support, schema changes when needed, and a generation test.
- Descriptions are client-facing. Describe purpose and use, not implementation details.

## Files and commands

- `auto-annotations/generate-all.py`: runs every generator below in dependency order and writes LF files. Use it after any annotated header, Kconfig, error map or ESP-IDF change. `--check` changes nothing and exits 1 naming the out-of-date files (CI / before a commit); `--test` also runs the generators' unit tests.
- `auto-annotations/device/generate-devices.py`: parser and descriptor generator; tests `auto-annotations/device/test_generate_devices.py` (`python -m unittest data-structures/auto-annotations/device/test_generate_devices.py`).
- `schema/device-definition.schema.json`: generated device JSON contract.
- `auto-annotations/enums/generate-enums.py`: shared `//#ref-enum` scanner; device generation runs it live.
- `auto-annotations/vm/generate-vm-model.py`: extracts marked VM C structs and VM enums.
- `auto-annotations/vm/generate-vm-blocks.py`: per VM block `<name>.content.json` next to its header (`//#vm-block` and the pin enums: palette shape, pins and their constants (`@overrides`), activation, load rules, state layouts, the block's own `//#block-enum` enums, bytecode encoding) plus `vm/blocks/index.generated.json`, the firmware registry and the user guides; the hand-written `<name>.display.json` holds the face (title, category, description, pin titles, header line) and the app renders a block from the two files alone; schemas `schema/vm-block.schema.json` (checked on the merged block), `schema/vm-blocks-index.schema.json`.
- `auto-annotations/vm/generate-vm-program.py`: the VM program wire format (`vm_wire.h` records, telemetry, widths, limits, arena formulas); schema `schema/vm-program.schema.json`.
- `schema/vm-model.schema.json`: generated VM model contract.
- `auto-annotations/contracts/generate-contracts.py`: extracts explicitly exposed packet contracts.
- `schema/contracts.schema.json`: generated contract catalog contract.
- `auto-annotations/settings/generate-settings.py`: extracts annotated runtime settings decoders.
- `schema/settings.schema.json`: generated settings catalog contract.
- `auto-annotations/errors/generate-errors.py`: error tags, owners, severities, payload layouts (native C offsets), message templates from the `LOG_BODY_*` macros, value → text tables of the helpers they call, and the `SE_schema_id()` value. Reads the sys_errors X-macro maps in `components/sys_errors/codes/sys_error_codes.h` order. Payload fields that hold a nameable ID carry `/*@enum-ref <enum>*/` or `/*@id <kind>*/` after their `;` (grammar: `SYS_ERRORS.MD`, rules for a map header; kinds listed in the output's `id_kinds`). Also publishes the esp_err_t names of the ESP-IDF in `$IDF_PATH` or `build/project_description.json`, and per-contract feature names from the `//@contract-features $SYS_DEVICE_CONTRACT_<X>` tables (one per `sys_device_contract_type_e` member, or generation fails). Schema `schema/errors.schema.json`. Regenerate after changing any error map or `LOG_BODY`.
- `auto-annotations/board/generate-board.py`: onboard devices and their fixed IDs (`//@STATIC_DEVICE` in `runit_board_defs.h`), their drivers (`SYS_DEVICE_CREATE_ONBOARD(&g_<driver>_class, &(d_<driver>_cfg_t){.device_id = DEVICE_ID_<NAME>, ...})` in `runit_board_cfg.c`) and matching device descriptors. Schema `schema/board.schema.json`. Regenerate after changing a static device.
- `auto-annotations/streams/generate-streams.py`: board → app streams (connector name, stream byte, connector ID) from the `sys_data_connector_init()` table, plus the BLE service / characteristics each stream uses (`//@STATIC_SERVICE` / `//@STATIC_CHARACTERISTIC` in `runit_board_defs.h`, bindings in `runit_board_connector_bindings_init()`). Schema `schema/streams.schema.json`. Regenerate after changing a `CONFIG_TX_PACKET_CLASS_*`, a GATT UUID or a connector binding.

```powershell
python data-structures/auto-annotations/generate-all.py   # all of the below, in order (add --check / --test)
python data-structures/auto-annotations/device/generate-devices.py components/codecs/decoders data-structures/devices
python data-structures/auto-annotations/enums/generate-enums.py <output-directory> <output-name>
python data-structures/auto-annotations/vm/generate-vm-model.py
python data-structures/auto-annotations/vm/generate-vm-blocks.py
python data-structures/auto-annotations/vm/generate-vm-program.py
python data-structures/auto-annotations/contracts/generate-contracts.py
python data-structures/auto-annotations/settings/generate-settings.py
python data-structures/auto-annotations/board/generate-board.py
python data-structures/auto-annotations/errors/generate-errors.py
python data-structures/auto-annotations/streams/generate-streams.py
```


