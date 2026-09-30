"""The app's debug build (every block with an ENO object, debug subscriptions), run on a board.

Usage: python debug_build_test.py [COM4] [log.txt]
Reads app/src/domain/upload/fixtures/debug-samples.json (made by the app's
debugSamples.test.ts): the upload steps of `planVmUpload(..., { debug: true })`.
Resets the board (boot log kept), uploads, runs 5 s and reports, per block, the
ENO it reported and what its enable sources did (the debug view colours a block
from exactly these), then checks that every subscribed object reported and no
error packet came while running. Stops the VM at the end.
"""
from __future__ import annotations

import json
import struct
import sys
import time
from pathlib import Path

from runit_link import Link
from vm_tests import VM, st

FIXTURE = Path(__file__).resolve().parents[4] / "app" / "src" / "domain" / "upload" / "fixtures" / "debug-samples.json"
FORMATS = {"F": "<f", "U32": "<I", "I32": "<i", "U8": "<B", "B": "<B", "U16": "<H"}
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


def main() -> int:
    port = sys.argv[1] if len(sys.argv) > 1 else "COM4"
    out = Path(sys.argv[2]) if len(sys.argv) > 2 else None
    fx = json.loads(FIXTURE.read_text())
    objs = {o["wire"]: o for o in fx["objects"]}
    with Link(port, reset=True) as link:
        boot = link.logs_since(0)
        say(f"== boot log ({len(boot)} lines) ==")
        for line in boot:
            say(line)
        say("\n== upload (the debug build) ==")
        for step in fx["steps"]:
            frame = bytes.fromhex(step["frame"])
            r = link.request(frame[0], frame[1], frame[2:], timeout=3.0)
            if r is None or not r.ok:
                check(step["label"], False, st(r))
        check(f"upload {len(fx['steps'])} steps ({fx['counts']}, arena {fx['arenaBytes']} B, {len(fx['subscribed'])} subscribed)", fails == 0)
        if fails:
            say("errors: " + "; ".join(str(e) for e in link.errors_since(0)[:5]))
            return 1
        start = link.mark()
        run = bytes.fromhex(fx["run"])
        r = link.request(run[0], run[1], run[2:], timeout=3.0)
        check("run", r is not None and r.ok, st(r))
        time.sleep(5.0)

        history: dict[int, list] = {}
        for fr in link.telemetry_since(start):
            b = fr.body
            if len(b) < 3 or b[0] != VM or b[1] != 0x43:
                continue
            off = 3
            for _ in range(b[2]):
                oid, first, ln = struct.unpack_from("<HHH", b, off)
                off += 6
                o = objs.get(oid)
                fmt = FORMATS.get(o["type"]) if o else None
                if fmt:
                    width = struct.calcsize(fmt)
                    history.setdefault(oid, []).extend(struct.unpack_from(fmt, b, off + j * width)[0] for j in range(ln // width))
                off += ln

        say("\n== per block: what the debug view would show ==")
        for blk in fx["blocks"]:
            eno = history.get(blk["eno"], [])
            gates = [history.get(w, []) for w in blk["enables"]]
            en = "always" if not gates else ("open+closed" if any(1 in g for g in gates) and any(0 in g for g in gates) else "open" if any(1 in g for g in gates) else "closed" if any(g for g in gates) else "unknown")
            say(f"  {blk['id']:8s} EN {en:12s} ENO samples={len(eno):3d} seen={sorted(set(eno))} last={eno[-1] if eno else None}")
        say("\n== every subscribed object ==")
        for wire in fx["subscribed"]:
            vals = history.get(wire, [])
            o = objs[wire]
            say(f"  {o['id']:12s} wire {wire:2d} {o['type']:3s} updates={len(vals):4d} last={vals[-1] if vals else None}")

        say("\n== checks ==")
        silent = [objs[w]["id"] for w in fx["subscribed"] if not history.get(w)]
        check("every subscribed object reported at least once", not silent, f"silent: {silent}")
        for blk in fx["blocks"]:
            if blk["id"] in ("every", "cnt", "saw", "gain", "high"):
                check(f"{blk['id']}: ENO true is reported", 1 in history.get(blk["eno"], []))
        gated = next(b for b in fx["blocks"] if b["id"] == "gated")
        gate = history.get(gated["enables"][0], [])
        check("gated: its enable source opens and closes (EN strip would turn green / red)", 0 in gate and 1 in gate, f"{sorted(set(gate))}")
        gated_eno = history.get(gated["eno"], [])
        check("gated: ENO follows its gate (true while open, false while closed)", 0 in gated_eno and 1 in gated_eno, f"{sorted(set(gated_eno))}")
        errors = link.errors_since(start)
        say(f"\n== error packets while running: {len(errors)} ==")
        for e in errors:
            say(f"run: {e}")
        check("no error packets while running", not errors)
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
