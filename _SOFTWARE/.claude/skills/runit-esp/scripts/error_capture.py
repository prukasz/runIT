"""Provoke errors whose payloads carry named IDs, and save every raw frame the board sends.

The saved file is a replay fixture for the app's decoder
(app/src/domain/decoder/fixtures/, test boardCapture.test.ts): it checks the live
error schema ID against errors.generated.json and that every node, field name and
message resolves. Each command is a read or one the board refuses, so the board's
state doesn't change.

Usage (BLE needs a Python with bleak):
    python error_capture.py BLE|COM3 <out.json>
"""
from __future__ import annotations

import json
import sys
import time
from datetime import datetime, timezone

from runit_link import PACKETS, open_link, pack_fields

INA3221 = 12  # a power monitor: no IO contract
GPIO_ESP = 0
MISSING_DEVICE = 250


def cases():
    dev_class = PACKETS["packet_sys_device_reset_t"]["class"]
    io = lambda name, **v: (PACKETS[name]["class"], int(PACKETS[name]["packet_header"], 16), pack_fields(PACKETS[name], v))
    return [
        ("IO set_level on a device without an IO contract", io("packet_sys_io_set_level_t", device_id=INA3221, pin=0, level=1)),
        ("IO get_level on a device that doesn't exist", io("packet_sys_io_get_level_t", device_id=MISSING_DEVICE, pin=0)),
        ("IO get_level on a pin out of range", io("packet_sys_io_get_level_t", device_id=GPIO_ESP, pin=99)),
        ("IO get_voltage on a pin without ADC", io("packet_sys_io_get_voltage_t", device_id=GPIO_ESP, pin=45)),
        ("unknown command class", (0x7E, 0x01, b"")),
        ("unknown packet in the device class", (dev_class, 0x7F, b"")),
        ("frame with no packet byte", (dev_class, None, b"")),
    ]


def main() -> int:
    port, out = sys.argv[1], sys.argv[2]
    raw: list[dict] = []
    with open_link(port) as link:
        inner = link._frame

        def record(now: float, data: bytes) -> None:
            raw.append({"t": round(now, 3), "hex": data.hex()})
            inner(now, data)

        link._frame = record
        time.sleep(0.5)
        for title, (cls, pkt, payload) in cases():
            before = len(link.errors)
            r = link.request(cls, pkt, payload, timeout=2.0)
            time.sleep(0.4)  # the error packet follows the response
            print(f"{title}\n   reply : {r if r else 'no response'}")
            for _, e in link.errors[before:]:
                print(f"   error : schema 0x{e.schema_id:08X} {e}")
        time.sleep(0.5)
    with open(out, "w", encoding="utf-8") as f:
        json.dump({"captured": datetime.now(timezone.utc).isoformat(timespec="seconds"), "link": port, "frames": raw}, f, indent=1)
        f.write("\n")
    print(f"{len(raw)} frames -> {out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
