"""Frames the app UI builds, sent to a board: do they work?

Usage: python ui_samples_test.py [COM4]
Reads app/src/domain/devices/fixtures/ui-samples.json (made by the app's
uiSamples.test.ts), sends every sample and checks the answer against its
`expect` ("ok", an error tag, or "ok|<tag>") and `fields`. Then stores the
sample stored code, restarts, and checks the replay. Erases the stored code
and restarts at the end. The board's answers go to ui-samples.board.json next
to the fixture.
"""
from __future__ import annotations

import json
import sys
import time
import zlib
from pathlib import Path

from runit_link import PACKETS, Link, unpack_fields
from project_store_tests import err, info, reboot_via_load, store

FIXTURES = Path(__file__).resolve().parents[4] / "app" / "src" / "domain" / "devices" / "fixtures"
fails = 0


def response_layout(frame: bytes) -> dict | None:
    for desc in PACKETS.values():
        if desc["class"] == frame[0] and int(desc["packet_header"], 16) == frame[1]:
            return desc.get("response")
    return None


def send(link: Link, sample: dict) -> dict:
    global fails
    frame = bytes.fromhex(sample["frame"])
    seq = link.next_seq()
    link.send_raw(bytes([seq]) + frame)
    r = link.wait_response(seq, 3.0)
    answer = "ok" if err(r) == "OK" else err(r)
    fields = None
    if r is not None and r.ok and r.data and (layout := response_layout(frame)):
        fields = unpack_fields(layout, r.data)
    passed = answer in sample["expect"].split("|") and all(fields is not None and fields.get(k) == v for k, v in sample.get("fields", {}).items())
    if not passed:
        fails += 1
    got = f"{answer} {fields}" if fields else answer
    want = f"{sample['expect']} {sample['fields']}" if "fields" in sample else sample["expect"]
    print(f"{'PASS' if passed else 'FAIL'}  {sample['label']}   ({got}; expected {want})")
    return {**sample, "answer": answer, **({"answer_fields": fields} if fields else {}), **({"answer_data": r.data.hex()} if r is not None and r.data and not fields else {}), "pass": passed}


def check(name: str, cond: bool, detail: str = "") -> dict:
    global fails
    print(f"{'PASS' if cond else 'FAIL'}  {name}" + (f"   ({detail})" if detail else ""))
    if not cond:
        fails += 1
    return {"label": name, "pass": cond, "detail": detail}


def main(port: str) -> int:
    samples = json.loads((FIXTURES / "ui-samples.json").read_text(encoding="utf-8"))
    link = Link(port)
    results: dict = {"run": time.strftime("%Y-%m-%d %H:%M:%S"), "port": port, "groups": []}

    for group in samples["groups"]:
        print(f"\n== {group['name']}")
        results["groups"].append({"name": group["name"], "samples": [send(link, sample) for sample in group["samples"]]})

    print("\n== Stored code")
    code = samples["storedCode"]
    blob = bytes.fromhex(code["blob"])
    stored = [check("app CRC = zlib CRC", code["crc32"] == zlib.crc32(blob), f"0x{code['crc32']:08X}")]
    schema = info(link).get("firmware_schema_id", 0)
    stored.append(check("store", (answer := store(link, blob, schema)) == "OK", answer))
    link = reboot_via_load(port, link)
    after = info(link)
    stored.append(check("every frame replayed", after.get("replay_state") == 1 and after.get("replay_applied") == len(code["frames"]) and after.get("replay_failed") == 0 and after.get("replay_crc32") == code["crc32"], str(after)))
    pin = samples["pins"]["a"]
    level = link.call("packet_sys_io_get_level_t", device_id=0, pin=pin)
    stored.append(check(f"pin {pin} high after boot (default settings replayed)", level is not None and level.ok and (level.fields or {}).get("level") == 1, str(level)))
    stored.append(check("erase", err(link.call("packet_settings_project_erase_t")) == "OK"))
    link = reboot_via_load(port, link)
    stored.append(check("nothing stored after erase + restart", info(link).get("stored") == 0))
    results["storedCode"] = stored
    link.close()

    results["fails"] = fails
    (FIXTURES / "ui-samples.board.json").write_text(json.dumps(results, indent=2) + "\n", encoding="utf-8")
    print(f"\n{'ALL PASS' if fails == 0 else f'{fails} FAILED'}  (answers saved to {FIXTURES / 'ui-samples.board.json'})")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1] if len(sys.argv) > 1 else "COM4"))
