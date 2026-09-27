"""Stored code (sys_project, class 0x0A) on a board: store, read back, refusals, load + boot replay, autostart.

Usage: python project_store_tests.py COM4
The stored code: a BLE service + characteristic, a user data connector bound to it,
and a VM program (PERIODIC -> EXPR counter, no IO). Restarts the board (load) twice
and erases the stored code at the end.
"""
from __future__ import annotations

import struct
import sys
import time
import zlib

from runit_link import Link
import vm_tests as v

SVC, CHR = 0xFF40, 0xFF41
CONNECTOR = 4  # SYS_DATA_CONNECTOR_APP_BASE
fails = 0


def check(name: str, cond: bool, detail: str = "") -> None:
    global fails
    print(f"{'PASS' if cond else 'FAIL'}  {name}" + (f"   ({detail})" if detail else ""))
    if not cond:
        fails += 1


def err(r) -> str:
    if r is None:
        return "no response"
    return "OK" if r.ok else r.err[0] if r.err else r.data.hex()


def code_frames() -> list[bytes]:
    frames = [
        bytes([0x02, 0x01]) + struct.pack("<HB", SVC, 1),
        bytes([0x02, 0x03]) + struct.pack("<HHBBBII", SVC, CHR, 0, 0, 1, 256, 0) + b"prj test\0",
        bytes([0x06, 0x01]) + struct.pack("<BBH", CONNECTOR, 0x10, 128) + b"prjc\0",
        bytes([0x06, 0x05]) + struct.pack("<BBI", CONNECTOR, 1, CHR),
    ]
    p = v.Program()
    p.obj(v.OBJ_TICK, v.T_B, "tick")
    p.obj(v.OBJ_COUNT, v.T_F, "count")
    p.obj(v.OBJ_PERIOD, v.T_U32, "period")
    for i in (v.OBJ_TICK, v.OBJ_COUNT, v.OBJ_PERIOD):
        p.acc(i, i)
    p.block(0, 0, v.BLK_PERIODIC, ins=[v.OBJ_PERIOD], qs=[v.OBJ_TICK], custom=struct.pack("<IBBHQ", 0, 0, 0, 0, 0))
    expr = bytes([v.EXPR_IN, 0, v.EXPR_IN, 1, v.EXPR_ADD, v.EXPR_END])
    p.block(1, 1, v.BLK_EXPR, ins=[v.OBJ_COUNT, v.OBJ_TICK], qs=[v.OBJ_COUNT], custom=struct.pack("<BBH", 0, 0, len(expr)) + expr)
    frames.append(bytes([v.VM, 0x41]) + struct.pack("<HHHI", len(p.objs), len(p.accs), len(p.blocks), p.total_size()))
    frames.append(bytes([v.VM, 0x42, len(p.objs)]) + b"".join(struct.pack("<H", oid) + head + name.encode() for oid, head, name in p.objs))
    frames.append(bytes([v.VM, 0x43, 1]) + struct.pack("<HHH", v.OBJ_PERIOD, 0, 4) + struct.pack("<I", 100))
    frames.append(bytes([v.VM, 0x44, len(p.accs)]) + b"".join(struct.pack("<HHBB", a, r, 0, 0) for a, r in p.accs))
    frames += [bytes([v.VM, 0x45]) + b for b in p.blocks]
    return frames


def frame_list(frames: list[bytes]) -> bytes:
    return b"".join(struct.pack("<H", len(f)) + f for f in frames)


def info(link: Link) -> dict:
    r = link.call("packet_settings_project_info_t")
    return r.fields if r is not None and r.ok and r.fields else {}


def store(link: Link, blob: bytes, schema: int, chunk: int = 200, crc: int | None = None) -> str:
    r = link.call("packet_settings_project_begin_t", length=len(blob))
    if r is None or not r.ok:
        return f"begin: {err(r)}"
    for off in range(0, len(blob), chunk):
        r = link.request(0x0A, 0x02, struct.pack("<I", off) + blob[off:off + chunk])
        if r is None or not r.ok:
            return f"write @{off}: {err(r)}"
    r = link.call("packet_settings_project_commit_t", crc32=zlib.crc32(blob) if crc is None else crc, schema_id=schema)
    return "OK" if r is not None and r.ok else f"commit: {err(r)}"


def read_all(link: Link, length: int, chunk: int = 100) -> bytes:
    out = b""
    for off in range(0, length, chunk):
        r = link.call("packet_settings_project_read_t", offset=off, length=min(chunk, length - off))
        if r is None or not r.ok:
            raise RuntimeError(f"read @{off}: {err(r)}")
        out += r.data
    return out


def reboot_via_load(port: str, link: Link) -> Link:
    r = link.call("packet_settings_project_load_t")
    check("load answers before the restart", r is not None and r.ok, err(r))
    link.close()
    time.sleep(1.0)
    fresh = Link(port)  # waits for the re-enumerated USB port
    fresh.wait_log("runIT boot sequence complete", 8.0)
    time.sleep(1.5)
    return fresh


def main(port: str) -> int:
    link = Link(port)
    before = info(link)
    check("info answers", bool(before), str(before))
    schema = before.get("firmware_schema_id", 0)
    print(f"      firmware schema 0x{schema:08X}, capacity {before.get('capacity')} B, stored {before.get('stored')}")

    frames = code_frames()
    blob = frame_list(frames)
    check("store the code", store(link, blob, schema) == "OK")
    now = info(link)
    check("info: stored, length, frames, CRC", now.get("stored") == 1 and now.get("length") == len(blob) and now.get("frame_count") == len(frames) and now.get("crc32") == zlib.crc32(blob), str(now))
    back = read_all(link, len(blob))
    check("read back is byte-identical", back == blob, f"{len(back)} B")

    # Refusals keep the committed code.
    check("bad CRC refused", store(link, blob, schema, crc=0x12345678).endswith("ERR_PROJECT_CRC"))
    exec_blob = frame_list(frames + [bytes([v.VM, 0x48, 5])])
    check("VM exec frame refused", store(link, exec_blob, schema).endswith("ERR_PROJECT_FRAME_REFUSED"))
    check("other firmware's schema refused", store(link, blob, schema ^ 1).endswith("ERR_PROJECT_SCHEMA"))
    r = link.call("packet_settings_project_begin_t", length=len(blob))
    r2 = link.request(0x0A, 0x02, struct.pack("<I", 10) + blob[10:20])
    check("chunk out of order refused", r is not None and r.ok and err(r2) == "ERR_PROJECT_OFFSET", err(r2))
    check("abort", err(link.call("packet_settings_project_abort_t")) == "OK")
    check("refusals kept the stored code", info(link).get("crc32") == zlib.crc32(blob))

    # Load: restart, replay, autostart.
    check("autostart on", err(link.call("packet_settings_project_options_t", autostart=1)) == "OK")
    link = reboot_via_load(port, link)
    after = info(link)
    check("replay ran, every frame applied", after.get("replay_state") == 1 and after.get("replay_applied") == len(frames) and after.get("replay_failed") == 0, str(after))
    check("autostart reported", after.get("autostart") == 1)
    check("replayed CRC = stored CRC (the board runs the stored code)", after.get("replay_crc32") == zlib.crc32(blob), str(after.get("replay_crc32")))
    svc = link.call("packet_settings_ble_service_create_t", uuid=SVC, is_primary=1)
    check("BLE service exists after restart", err(svc) == "ERR_DEV_ALREADY_EXIST", err(svc))
    add = link.request(v.VM, 0x42, bytes([1]) + struct.pack("<H", 0) + v.obj_head(v.T_U8, 1, "x") + b"x")
    check("VM program running (autostart)", err(add) == "ERR_VM_LOAD_RUNNING", err(add))

    # Clean up: erase, autostart off, restart to defaults.
    check("erase", err(link.call("packet_settings_project_erase_t")) == "OK")
    check("autostart off", err(link.call("packet_settings_project_options_t", autostart=0)) == "OK")
    link = reboot_via_load(port, link)
    final = info(link)
    check("after erase + restart: nothing stored, nothing replayed", final.get("stored") == 0 and final.get("replay_state") == 0 and final.get("replay_crc32") == 0, str(final))
    svc = link.call("packet_settings_ble_service_create_t", uuid=SVC, is_primary=1)
    check("BLE service gone after restart", err(svc) == "OK", err(svc))
    link.call("packet_settings_ble_service_remove_t", uuid=SVC)
    link.close()

    print(f"\n{'ALL PASS' if fails == 0 else f'{fails} FAILED'}")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1] if len(sys.argv) > 1 else "COM4"))
