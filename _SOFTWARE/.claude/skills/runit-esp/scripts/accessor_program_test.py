"""Accessors under real blocks (dynamic positions driven by blocks, nested dynamic, folder pick, SET through a dynamic destination, CLONE live cell, run-time out-of-range), run on a board.

Usage: python accessor_program_test.py [COM4] [log.txt]
Reads app/src/domain/compiler/fixtures/program-samples-accessors.json (made by the
app's programSamplesAccessors.test.ts). Resets the board (boot log kept), uploads,
subscribes to every object, then runs four phases; a live write (fixture `writes`)
starts each next one: 0 = 3 s, 1 = 1.5 s (sel = 2), 2 = 1.5 s (oob = 9, out of
range: errors expected), 3 = 1.5 s (oob = 3, back inside). Prints and saves a
report: boot log, per-phase stats of every object element, checks, error packets
per phase. Stops the VM at the end.
"""
from __future__ import annotations

import json
import struct
import sys
import time
from pathlib import Path

from runit_link import Link
from vm_tests import VM, st

FIXTURE = Path(__file__).resolve().parents[4] / "app" / "src" / "domain" / "compiler" / "fixtures" / "program-samples-accessors.json"
FORMATS = {"F": "<f", "U32": "<I", "I32": "<i", "U8": "<B", "B": "<B", "U16": "<H", "I16": "<h", "I8": "<b"}
DURATIONS = [3.0, 1.5, 1.5, 1.5]
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


def main() -> int:
    port = sys.argv[1] if len(sys.argv) > 1 else "COM4"
    out = Path(sys.argv[2]) if len(sys.argv) > 2 else None
    fx = json.loads(FIXTURE.read_text())
    objs = {o["wire"]: o for o in fx["objects"]}
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
            say("errors: " + "; ".join(str(e) for e in link.errors_since(0)[:5]))
            return 1
        start = link.mark()
        check(f"subscribe all {len(fx['objects'])} objects", (r := send(link, fx["subscribe"])) is not None and r.ok, st(r))
        bounds = [start]
        check("run", (r := send(link, fx["run"])) is not None and r.ok, st(r))
        time.sleep(DURATIONS[0])
        for phase, write in enumerate(fx["writes"], start=1):
            bounds.append(link.mark())
            for frame in write["frames"]:
                check(f"phase {phase} starts: {write['what']}", (r := send(link, frame)) is not None and r.ok, st(r))
            time.sleep(DURATIONS[phase])
        end = link.mark()
        bounds.append(end)

        # history[phase][(wire, element)] = values in order
        history: list[dict[tuple[int, int], list]] = [{} for _ in DURATIONS]
        for fr in link.telemetry_since(start):
            b = fr.body
            if len(b) < 3 or b[0] != VM or b[1] != 0x43:
                continue
            ms = fr.t * 1000
            phase = max(i for i, t in enumerate(bounds[:-1]) if ms >= t)
            off = 3
            for _ in range(b[2]):
                oid, first, ln = struct.unpack_from("<HHH", b, off)
                off += 6
                o = objs.get(oid)
                fmt = FORMATS.get(o["type"]) if o else None
                if fmt:
                    width = struct.calcsize(fmt)
                    for j in range(ln // width):
                        history[phase].setdefault((oid, first // width + j), []).append(struct.unpack_from(fmt, b, off + j * width)[0])
                off += ln

        for phase, label in enumerate(("running", "sel = 2", "oob = 9 (out of range)", "oob = 3")):
            say(f"\n== phase {phase}: {label} ({DURATIONS[phase]} s) ==")
            for wire, o in sorted(objs.items()):
                for elem in range(o["length"]):
                    vals = history[phase].get((wire, elem))
                    tag = f"{o['id']}[{elem}]" if o["length"] > 1 else o["id"]
                    if not vals:
                        say(f"  {tag:14s} wire {wire:2d} {o['type']:3s} no samples")
                    else:
                        distinct = sorted(set(vals))
                        shown = distinct if len(distinct) <= 8 else f"{distinct[:4]}..{distinct[-2:]}"
                        say(f"  {tag:14s} wire {wire:2d} {o['type']:3s} updates={len(vals):4d} last={vals[-1]} distinct={shown}")

        say("\n== checks ==")
        for c in fx["checks"]:
            vals = history[c["phase"]].get((c["wire"], c.get("elem", 0)), [])
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
            tag = c["id"] + (f"[{c['elem']}]" if "elem" in c else "")
            check(f"phase {c['phase']}: {c['what']}: {tag} {c['op']} {c['value']}", good, f"last {last}, {len(vals)} samples")

        say("\n== error packets ==")
        by_phase: list[list] = [[] for _ in DURATIONS]
        for t, e in link.errors:
            ms = t * 1000
            if ms < start:
                say(f"boot: {e}")
                continue
            by_phase[max(i for i, b0 in enumerate(bounds[:-1]) if ms >= b0)].append(e)
        for phase, errs in enumerate(by_phase):
            kinds = sorted({str(e).split(" payload")[0] for e in errs})
            say(f"phase {phase}: {len(errs)} error packets {kinds}")
        for e in by_phase[2][:5]:
            say(f"  e.g. {e}")
        check("no errors in phases 0, 1 and 3", not (by_phase[0] or by_phase[1] or by_phase[3]))
        check("out-of-range position raised an error in phase 2", bool(by_phase[2]))
        logs = link.logs_since(start)
        say(f"\n== logs while running ({len(logs)} lines) ==")
        for line in logs:
            say(line)
        link.request(VM, 0x40)
    say(f"\n{'ALL PASS' if fails == 0 else f'{fails} FAILED'}")
    if out:
        out.write_text("\n".join(report), encoding="utf-8")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
