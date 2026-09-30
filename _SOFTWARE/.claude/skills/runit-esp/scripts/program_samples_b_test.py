"""Second whole-program sample (sawtooth, EXPR_BIT, TOF / TP, falling EDGE, RS latch, folder paths), run on a board.

Usage: python program_samples_b_test.py [COM4] [report.txt]
Reads app/src/domain/compiler/fixtures/program-samples-b.json (made by the app's
programSamplesB.test.ts). Resets the board (boot log kept), uploads, subscribes
to EVERY value object, runs 6 s (phase 0), writes k = 3, runs 2 s (phase 1).
Prints and saves a report: boot log, per-object stats, checks, logs, errors.
"""
from __future__ import annotations

import json
import struct
import sys
import time
from pathlib import Path

from runit_link import Link
from vm_tests import VM, st

FIXTURE = Path(__file__).resolve().parents[4] / "app" / "src" / "domain" / "compiler" / "fixtures" / "program-samples-b.json"
FORMATS = {"F": "<f", "U32": "<I", "I32": "<i", "U8": "<B", "B": "<B", "U16": "<H", "I16": "<h", "I8": "<b"}
fails = 0
report: list[str] = []


def say(text: str = "") -> None:
    print(text)
    report.append(text)


def check(name: str, cond: bool, detail: str = "") -> None:
    global fails
    say(f"{'PASS' if cond else 'FAIL'}  {name}" + (f"   ({detail})" if detail else ""))
    if not cond:
        fails += 1


def send(link: Link, frame_hex: str):
    frame = bytes.fromhex(frame_hex)
    return link.request(frame[0], frame[1], frame[2:], timeout=3.0)


def samples(link: Link, since: int, until: int | None = None) -> dict[int, list[bytes]]:
    out: dict[int, list[bytes]] = {}
    for fr in link.telemetry_since(since):
        b = fr.body
        if len(b) < 3 or b[0] != VM or b[1] != 0x43 or (until is not None and fr.t * 1000 >= until):
            continue
        off = 3
        for _ in range(b[2]):
            oid, _start, ln = struct.unpack_from("<HHH", b, off)
            off += 6
            out.setdefault(oid, []).append(b[off:off + ln])
            off += ln
    return out


def decode(ty: str, raw: bytes):
    fmt = FORMATS.get(ty)
    return struct.unpack(fmt, raw[:struct.calcsize(fmt)])[0] if fmt and len(raw) >= struct.calcsize(fmt) else raw.hex()


def main() -> int:
    port = sys.argv[1] if len(sys.argv) > 1 else "COM4"
    out = Path(sys.argv[2]) if len(sys.argv) > 2 else None
    fx = json.loads(FIXTURE.read_text())
    types = {o["wire"]: (o["id"], o["type"]) for o in fx["objects"]}
    say("blocks: " + ", ".join(fx["blocks"]))
    with Link(port, reset=True) as link:
        boot = link.logs_since(0)
        say(f"\n== boot log ({len(boot)} lines) ==")
        for line in boot:
            say(line)
        say("\n== upload ==")
        for i, frame in enumerate(bytes.fromhex(f) for f in fx["frames"]):
            r = link.request(frame[0], frame[1], frame[2:], timeout=3.0)
            if r is None or not r.ok:
                check(f"upload frame {i} (0x{frame[1]:02x})", False, st(r))
        check(f"upload {len(fx['frames'])} frames ({fx['counts']}, arena {fx['arenaBytes']} B)", fails == 0)
        if fails:
            return 1
        start = link.mark()
        check(f"subscribe all {len(fx['objects'])} objects", (r := send(link, fx["subscribe"])) is not None and r.ok, st(r))
        check("run", (r := send(link, fx["run"])) is not None and r.ok, st(r))
        time.sleep(6.0)
        switched = link.mark()
        for w in fx["write"]:
            check("write k = 3 while running", (r := send(link, w)) is not None and r.ok, st(r))
        time.sleep(2.0)

        def series(phase: int) -> dict[int, list]:
            raw = samples(link, start, switched) if phase == 0 else samples(link, switched)
            return {oid: [decode(types[oid][1], v) for v in vs] for oid, vs in raw.items() if oid in types}

        phases = {0: series(0), 1: series(1)}
        for ph, label in ((0, "running (6 s)"), (1, "after k = 3 (2 s)")):
            say(f"\n== every subscribed object, {label} ==")
            for wire, (name, ty) in sorted(types.items()):
                vals = phases[ph].get(wire)
                if not vals:
                    say(f"  {name:12s} wire {wire:2d} {ty:3s} no samples")
                else:
                    distinct = sorted(set(vals))
                    shown = distinct if len(distinct) <= 10 else f"{distinct[:5]}..{distinct[-2:]}"
                    say(f"  {name:12s} wire {wire:2d} {ty:3s} updates={len(vals):4d} last={vals[-1]} distinct={shown}")
        say("\n== checks ==")
        for c in fx["checks"]:
            vals = phases[c["phase"]].get(c["wire"], [])
            last = vals[-1] if vals else None
            if not vals:
                good = False
            elif c["op"] == "eq":
                good = abs(last - c["value"]) < 1e-4
            elif c["op"] == "gt":
                good = last > c["value"]
            elif c["op"] == "within":
                good = all(c["value"] <= v <= c["max"] for v in vals)
            else:
                good = any(abs(v - c["value"]) < 1e-4 for v in vals)
            check(f"{c['what']}: {c['id']} {c['op']} {c['value']}", good, f"last {last}, {len(vals)} samples")
        boot_errors = [e for e in link.errors_since(0) if e not in link.errors_since(start)]
        errors = link.errors_since(start)
        logs = link.logs_since(start)
        say(f"\n== logs while running ({len(logs)} lines) ==")
        for line in logs:
            say(line)
        say(f"\n== error packets ({len(errors)}) ==")
        for e in errors:
            say(str(e))
        check("no error packets while running", not errors)
        link.request(VM, 0x40)
    say(f"\n{'ALL PASS' if fails == 0 else f'{fails} FAILED'}")
    if out:
        out.write_text("\n".join(report), encoding="utf-8")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
