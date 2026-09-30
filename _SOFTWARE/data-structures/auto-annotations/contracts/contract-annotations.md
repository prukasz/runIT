# Contract auto-annotations

`data-structures/contracts/contracts.generated.json` is the catalog of packet
contracts deliberately exposed by decoder headers. It is generated output; C
headers and their annotations remain the source of truth.

Add these directives in a decoder header, after its includes:

```c
//@contract-catalog system @title System contracts @description Operations exposed by this interface class.
//@contract-list SYS_CONTRACTS_PACKET_LIST
```

- `//@contract-catalog <id>` publishes one catalog. `@title` and
  `@description` are required client-facing metadata.
- `//@contract-list <X-macro>` names the packet registry that defines the
  public boundary. Each `X(HEADER_packet..., packet_..._t, decoder_...)` row
  becomes one contract. A packet struct merely declared in the header is not
  exported unless its row appears in this list.
- Packet fields reuse the existing trailing annotations (`@required`,
  `@optional`, `@alias`, `@unit`, `@enum-ref`, `@one-of`, and so on). No separate field grammar
  exists for contracts.
- `@enum-ref <enum>` identifies an exported enum; pair it with
  `@one-of [$MEMBER, ...]` to declare the values valid in this generic wire
  contract. `$` values are resolved against `//#ref-enum` and fail generation
  if they do not exist. A device can narrow this set through its own property.
- Contract `id` is the exact C packet type, such as
  `packet_sys_io_set_mode_t`. This intentionally matches the `packet` key in
  generated device descriptors, providing the future join key without an
  artificial mapping layer.

## Responses

Every live command is answered on the interface response stream (see the
document-level `response_stream`):

```
[class_header 0x05] [request_class] [request_packet] [status] [data ...]
```

- Responses come in request order; match them first-in first-out.
- `status` is `sys_interface_status_e`: `0` OK, `1` error.
- OK: `data` is the contract's `response` layout, or empty when the contract
  has no `response`.
- Error: `data` is `u16 tag, u16 owner` (little-endian) of the error chain's
  root cause. The full chain arrives on the errors stream.

A decoder declares a command's OK data as a packed struct named after the
packet, `packet_<name>_response_t`, in the same header. It uses the same
field grammar as request packets (`@alias`, `@unit`, `@enum-ref`, …), and
array lengths may be a literal or a `#define` / `CONFIG_*` symbol. The
generator attaches it to the contract as `response`.

Generate from the repository root:

```powershell
python data-structures/auto-annotations/contracts/generate-contracts.py
```
