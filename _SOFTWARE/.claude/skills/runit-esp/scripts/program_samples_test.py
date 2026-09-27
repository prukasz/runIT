"""A whole VM program compiled by the app (objects, accessors, blocks), run on a board.

Usage: python program_samples_test.py [COM4]
Reads app/src/domain/compiler/fixtures/program-samples.json (made by the app's
programSamples.test.ts): upload frames, subscribe / run / live-write frames and
the values to expect. Uploads, runs 1.5 s and checks phase 0; writes sel = 0,
runs 1 s and checks phase 1 (a value not resent keeps its last one). Resets the
VM at the end.
"""
from __future__ import annotations

import json
import struct
import sys
import time
from pathlib import Path

from runit_link import Link
from vm_tests import VM, st, values

FIXTURE = Path(__file__).resolve().parents[4] / "app" / "src" / "domain" / "compiler" / "fixtures" / "program-samples.json"
FORMATS = {"F": "<f", "U32": "<I", "I32": "<i", "U8": "<B", "B": "<B"}
fails = 0


def check(name: str, cond: bool, detail: str = "") -> None:
    global fails
    print(f"{'PASS' if cond else 'FAIL'}  {name}" + (f"   ({detail})" if detail else ""))
    if not cond:
        fails += 1


def send(link: Link, frame_hex: str):
    frame = bytes.fromhex(frame_hex)
    return link.request(frame[0], frame[1], frame[2:], timeout=3.0)


def main() -> int:
    port = sys.argv[1] if len(sys.argv) > 1 else "COM4"
    fx = json.loads(FIXTURE.read_text())
    print("blocks:", ", ".join(fx["blocks"]))
    latest: dict[int, float] = {}

    def verify(phase: int, since: int) -> None:
        for wire, raws in values(link, since).items():
            latest[wire] = raws[-1]
        for c in (c for c in fx["checks"] if c["phase"] == phase):
            raw = latest.get(c["wire"])
            v = struct.unpack(FORMATS[c["type"]], raw[: struct.calcsize(FORMATS[c["type"]])])[0] if raw is not None else None
            if v is None:
                good = False
            elif c["op"] == "eq":
                good = abs(v - c["value"]) < 1e-4
            elif c["op"] == "gt":
                good = v > c["value"]
            else:
                good = v > 0 and v % c["value"] == 0
            check(f"{c['what']}: {c['id']} {c['op']} {c['value']}", good, f"got {v}")

    with Link(port) as link:
        for i, frame in enumerate([bytes.fromhex(f) for f in fx["frames"]]):
            r = link.request(frame[0], frame[1], frame[2:], timeout=3.0)
            if r is None or not r.ok:
                check(f"upload frame {i} (0x{frame[1]:02x})", False, st(r))
        check(f"upload {len(fx['frames'])} frames ({fx['counts']}, arena {fx['arenaBytes']} B)", fails == 0)
        if fails:
            print("errors:", "; ".join(str(e) for e in link.errors_since(0)[:5]))
            return 1
        since = link.mark()
        check("subscribe", (r := send(link, fx["subscribe"])) is not None and r.ok, st(r))
        check("run", (r := send(link, fx["run"])) is not None and r.ok, st(r))
        time.sleep(1.5)
        verify(0, since)
        since = link.mark()
        for w in fx["write"]:
            check("write sel = 0 while running", (r := send(link, w)) is not None and r.ok, st(r))
        time.sleep(1.0)
        verify(1, since)
        errors = link.errors_since(0)
        check("no error packets", not errors, "; ".join(str(e) for e in errors[:3]))
        link.request(VM, 0x40)

    print(f"\n{'ALL PASS' if fails == 0 else f'{fails} FAILED'}")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
