# Settings auto-annotations

Runtime settings decoders publish their packet layouts to
`data-structures/settings/settings.generated.json`.

Place one directive after the decoder includes:

```c
//@settings ble @title BLE settings @description Create and remove runtime BLE GATT services and characteristics.
//@settings-class SETTINGS_BLE
```

- `<tag>` is the stable settings catalog identifier.
- `@title` and `@description` are required client-facing metadata.
- `//@settings-class <CLASS_NAME>` names the Kconfig class suffix, such as
  `SETTINGS_BLE` for `CONFIG_RX_PACKET_CLASS_SETTINGS_BLE`.
- Every `HEADER_packet_*` packed struct in that annotated `dec_settings_*.h`
  file is exported. Field annotations use the shared packet-field grammar.

## Flexible trailing arrays

A C flexible array member, such as `char name[]`, is emitted as
`"flexible_array": true`. It consumes all remaining packet bytes and must be
the final struct field. Add `@encoding utf-8 @terminator nul` when it is a
NUL-terminated string; omit both for an opaque trailing blob.

Generate from the repository root:

```powershell
python data-structures/auto-annotations/settings/generate-settings.py
```
