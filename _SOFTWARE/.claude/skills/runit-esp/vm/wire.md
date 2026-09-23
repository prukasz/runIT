# VM wire protocol (class `0x04`)

Decoder: `components/codecs/decoders/dec_vm_loader.h` (framing and bounds only; validation is in `vm_loader.c`). Every command frame starts with the client's `seq` byte, stripped by `sys_interface` (SYS_INTERFACE.MD), so a frame from the app is `[seq][0x04][packet][payload]` and every command gets a response `[0x05][seq][0x04][packet][status][data]`. All multi-byte fields little-endian.

## 1. Packets

| Packet | Payload | Effect |
|---|---|---|
| `0x40` reset | — | `vm_loader_reset()`: stop and wait for the pass, clear exec/subscriptions/dynamic objects/registries/arena; leaves execution stopped |
| `0x41` open | `u16 obj_cnt, u16 acc_cnt, u16 blk_cnt, u32 total_size` | Allocate a new pool of exactly `total_size` (checked against `CONFIG_VM_STORE_MAX_POOL` and the largest free DRAM block) **before** tearing the old program down; build the three registries |
| `0x42` add objects | `u8 n`, n × `{u16 id, vm_obj_head_t (4 B, raw ESP32-GCC bitfield ABI), char name[head.d.name_size]}` | Create + bind; name unterminated, ≤ 15 B |
| `0x43` set data | `u8 n`, n × `{u16 id, u16 start_idx, u16 byte_len, u8 data[byte_len]}` | Stopped: write payload (for `PTR`: `data` = `u16` child ids, linked). **Running: queued as a runtime override**, applied at the next pass boundary; `PTR` targets refused (`ERR_VM_OVERRIDE_PTR_UNSUPPORTED`) |
| `0x44` add accessors | `u8 n`, n × `{u16 acc_id, u16 root_obj_id, u8 idx_count, u8 idx_len, u8 idx_data[idx_len]}`; index = `u8 kind` + `LITERAL u32` / `REF u16 acc_id` / `NAME u8 len, bytes` | Create + bind + pre-resolve (cache) |
| `0x45` add block | one block: `u16 blk_id, u16 block_idx, u8 type, u8 in_cnt, u8 q_cnt, u8 en_cnt, u8 en_mode, u8 on_error, u16 custom_len, u16 eno_obj_id` (14 B), then `in_cnt × u16` accessor ids (`0xFFFF` = unwired), `q_cnt × u16` object ids, `en_cnt × u16` accessor ids, `custom_len` bytes | Type must be in the palette; every reference must be bound; the type's verify runs, and a rejected block is removed again |
| `0x47` subscribe | `u8 n` (0 = clear all), n × `u16` object id | Telemetry for those objects (below) |
| `0x48` exec control | `u8 command` (`vm_exec_command_e`) | 0 scan mode, 1 once, 2 block mode, 3 next, 4 rewind to start, 5 normal, 6 pause, 7 resume, 8 reset (= `0x40`), 9 acknowledge latched device fault, 10 forget retained values |

**Upload order** (client-side, one-way):
`0x40` → `0x41` → objects (`0x42`) → data and `PTR` links (`0x43`, children before parents) → accessors (`0x44`, `REF` targets first, roots existing) → blocks (`0x45`, one per frame, **in execution order**) → `0x48 05` (normal mode).
`0x42`/`0x43`/`0x44` are batched and repeatable; batch size is bounded by the link (frame limit = BLE MTU − 3, see SYS_DATA_CONNECTOR.MD). Nothing rolls back on failure: the next `0x40`/`0x41` clears a half-built program.

Pin and list limits: `in_cnt`, `q_cnt`, `en_cnt` ≤ 16 (`CONFIG_VM_BLOCK_MAX_*`). Pool ceiling 128 kB (`CONFIG_VM_STORE_MAX_POOL`). Memory estimate for a program: VM.MD "Program RAM Budget".

## 2. Telemetry (subscriptions)

`vm_sub` walks the subscribed objects and their `PTR` children (8 levels) at the end of every **completed** pass and sends each object whose **value changed** since it was last sent (value hash, not `upd`: quiet writes are sent too). A new subscription sends everything once (next pass; at once if stopped). More than `CONFIG_VM_SUB_MAX_SUBSCRIBERS` IDs is refused.

```
[0x02 telemetry stream][0x04][0x43][u8 count] count × {u16 id, u16 start_idx, u16 byte_len, data}   values
[0x02 telemetry stream][0x04][0x42][u8 count] count × {u16 id, vm_obj_head_t (4 B), name}            heap objects
```

- The stream byte `0x02` is the telemetry connector's; the inner `0x04 0x43` / `0x04 0x42` let the app reuse its upload parsers.
- `PTR` data is the child IDs, `u16` each. Heap object IDs have bit `0x8000` set.
- An object bigger than a frame is split: several records, `start_idx` in elements (2 B per `PTR` element, else the type width). Frames follow the telemetry connector's limit (`sys_data_connector_max_payload`, BLE MTU), capped by `CONFIG_VM_SUB_MAX_FRAME_LEN`.
- A describe record (heap objects only: the app didn't upload them) goes out in an earlier frame than any value that may reference it; again when type, size or name change.
- A frame the connector refuses (or a record the link can't carry) is resent in full next pass; after a refusal the rest of the pass is held back, so no value arrives without its describe. The app drops a value for a heap ID it has no describe for.
- Up to `CONFIG_VM_SUB_MAX_TRACKED` (256) reachable objects; beyond that `ERR_VM_SUB_TRACK_FULL` once and the rest isn't sent.

## 3. Runtime writes vs load writes

| Mode | `0x43` goes to | Allowed targets | Checks |
|---|---|---|---|
| Stopped | `vm_loader_set_data()` (under the program lock) | any, incl. `PTR` links | ids, ranges, whole elements |
| Running / paused / stepping | `vm_override_post()` → ring buffer (`CONFIG_VM_OVERRIDE_BUF_SIZE`) → applied at pass start | non-`PTR` | re-validated at drain (type, policy, range); a mismatch is reported and dropped without `upd` |

`0x42`/`0x44`/`0x45` need the VM stopped (`ERR_VM_LOAD_BAD_STATE` otherwise). `0x40` and `0x41` stop a running program themselves (lifecycle barrier) before replacing it.

## 4. What reaches the app (generated JSON)

| Artifact | Status |
|---|---|
| `data-structures/vm/vm-model.generated.json` | Published: `vm_obj_head_t` (wire ABI, and `device_sets` on the flags the device forces), `vm_obj_t`; the VM enums (`//#vm-struct-ref`, grammar `data-structures/auto-annotations/vm/vm-annotations.md`) |
| `data-structures/vm/vm-program.generated.json` | **Published** (`generate-vm-program.py`, from `core/loader/vm_wire.h`, `vm_sub.c`, `//#vm-arena`, Kconfig): every `0x40`–`0x48` packet (fields with offsets, tails, batch, when it's accepted, rules with their error tags), the accessor index-step union, both telemetry frames, type widths (memory and wire), ID constants, all VM Kconfig limits with values from `sdkconfig`, the arena formulas for `total_size` |
| Block palette | **Published**: `data-structures/vm/blocks/block_<name>.generated.json` + `index.generated.json` — per block: id, title, category, description, activation, pins (min / max, required, value kind), load rules with error tags, private-state layout (offsets, sizes, `enum_ref`, source user / derived / runtime / padding), the enums it uses; EXPR / EXPR_BIT add the opcode table (pops, pushes, operand) and golden vectors. Generator: `generate-vm-blocks.py` |
| Error tags | Not published (the app gets `u16 tag, u16 owner` in responses) |

Nothing open for the app contract (G-2 closed).
