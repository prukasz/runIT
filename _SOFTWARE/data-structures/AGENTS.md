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
- Do not edit `*.generated.json`; change C annotations, schema, or generator, then regenerate.
- `//@...` is a header directive; `@...` is field or enum-member metadata.
- `//#ref-enum` explicitly publishes an enum. `$SYMBOL` must resolve uniquely to a member of one published enum; unresolved or ambiguous references are errors.
- `//@self-property` defines local literal choices; `//@property` defines reusable global-enum choices; `@arg` reuses either in a contract parameter.
- `//@contract` must name an existing packet struct. Include every packet field marked `@required` in `//@param`; `device_id` is generated automatically.
- Keep annotations next to the C declarations they describe. Add a new directive only with documented grammar, generator support, schema changes when needed, and a generation test.
- Descriptions are client-facing. Describe purpose and use, not implementation details.

## Files and commands

- `auto-annotations/device/generate-devices.py`: parser and descriptor generator.
- `schema/device-definition.schema.json`: generated device JSON contract.
- `auto-annotations/enums/generate-enums.py`: shared `//#ref-enum` scanner; device generation runs it live.
- `auto-annotations/vm/generate-vm-model.py`: extracts marked VM C structs and VM enums.
- `schema/vm-model.schema.json`: generated VM model contract.
- `auto-annotations/contracts/generate-contracts.py`: extracts explicitly exposed packet contracts.
- `schema/contracts.schema.json`: generated contract catalog contract.
- `auto-annotations/settings/generate-settings.py`: extracts annotated runtime settings decoders.
- `schema/settings.schema.json`: generated settings catalog contract.

```powershell
python data-structures/auto-annotations/device/generate-devices.py components/codecs/decoders data-structures/devices
python data-structures/auto-annotations/enums/generate-enums.py <output-directory> <output-name>
python data-structures/auto-annotations/vm/generate-vm-model.py
python data-structures/auto-annotations/contracts/generate-contracts.py
python data-structures/auto-annotations/settings/generate-settings.py
```


