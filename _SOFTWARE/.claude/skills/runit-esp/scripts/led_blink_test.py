"""Blink the TCA6424A status LEDs (device 1, pins 22 / 23) from a VM program and read them back.

Usage: python led_blink_test.py [COM4] [log.txt]
Reads app/src/domain/compiler/fixtures/program-samples-led.json (made by the app's
programSamplesLed.test.ts). Resets the board (boot log kept), uploads, subscribes
to every object, runs, then for 5 s reads both pins from the expander every
~100 ms (packet_sys_io_get_level_t) and checks they blink, in turn. Stops the VM
at the end (disabled_action FORCE_LOW: both LEDs go off) and reads them again.
"""
from __future__ import annotations

import json
import struct
import sys
import time
from pathlib import Path

from runit_link import Link
from vm_tests import VM, st

FIXTURE = Path(__file__).resolve().parents[4] / "app" / "src" / "domain" / "compiler" / "fixtures" / "program-samples-led.json"
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


def level(link: Link, device: int, pin: int):
    r = link.call("packet_sys_io_get_level_t", device_id=device, pin=pin)
    return r.fields["level"] if r and r.ok and r.fields else None


def main() -> int:
    port = sys.argv[1] if len(sys.argv) > 1 else "COM4"
    out = Path(sys.argv[2]) if len(sys.argv) > 2 else None
    fx = json.loads(FIXTURE.read_text())
    dev, (p_a, p_b) = fx["leds"]["device"], fx["leds"]["pins"]
    say("blocks: " + ", ".join(fx["blocks"]))
    with Link(port, reset=True) as link:
        boot = link.logs_since(0)
        say(f"\n== boot log ({len(boot)} lines) ==")
        for line in boot:
            say(line)
        say("\n== LEDs before the program ==")
        say(f"TCA {p_a} = {level(link, dev, p_a)}, TCA {p_b} = {level(link, dev, p_b)}")
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
        check("run", (r := send(link, fx["run"])) is not None and r.ok, st(r))

        say(f"\n== TCA {p_a} / {p_b} read back every ~100 ms for 5 s ==")
        reads: list[tuple[float, int | None, int | None]] = []
        t0 = time.time()
        while time.time() - t0 < 5.0:
            reads.append((time.time() - t0, level(link, dev, p_a), level(link, dev, p_b)))
            time.sleep(0.05)
        for t, a, b in reads:
            say(f"  t={t:5.2f}s  LED{p_a}={a}  LED{p_b}={b}")

        good = [(t, a, b) for t, a, b in reads if a in (0, 1) and b in (0, 1)]
        check(f"all {len(reads)} pin reads answered", len(good) == len(reads), f"{len(good)} valid")
        for name, idx, pin in ((f"LED {p_a}", 1, p_a), (f"LED {p_b}", 2, p_b)):
            seq = [row[idx] for row in good]
            flips = sum(1 for x, y in zip(seq, seq[1:]) if x != y)
            check(f"{name} (TCA {pin}) blinks: both levels seen, flips >= 6 in 5 s (500 ms per state)", set(seq) == {0, 1} and flips >= 6, f"{flips} flips")
        same = sum(1 for _, a, b in good if a == b)
        check("the LEDs alternate (equal in at most 3 reads: two reads are ~10 ms apart)", same <= 3, f"{same} of {len(good)} reads equal")

        vals: dict[int, list] = {}
        for fr in link.telemetry_since(start):
            b = fr.body
            if len(b) < 3 or b[0] != VM or b[1] != 0x43:
                continue
            off = 3
            for _ in range(b[2]):
                oid, _s, ln = struct.unpack_from("<HHH", b, off)
                off += 6
                vals.setdefault(oid, []).append(b[off:off + ln])
                off += ln
        say("\n== telemetry (every subscribed object) ==")
        fmt = {"F": "<f", "B": "<B", "U32": "<I", "U8": "<B"}
        for o in fx["objects"]:
            raws = vals.get(o["wire"], [])
            dec = [struct.unpack(fmt[o["type"]], r[:struct.calcsize(fmt[o["type"]])])[0] for r in raws if len(r) >= struct.calcsize(fmt[o["type"]])]
            say(f"  {o['id']:10s} wire {o['wire']} {o['type']:3s} updates={len(dec):3d} distinct={sorted(set(dec))}")
        errors = link.errors_since(start)
        boot_errors = [e for e in link.errors_since(0) if e not in errors]
        logs = link.logs_since(start)
        say(f"\n== logs while running ({len(logs)} lines) ==")
        for line in logs:
            say(line)
        say(f"\n== error packets: {len(boot_errors)} at boot, {len(errors)} while running ==")
        for e in boot_errors:
            say(f"boot: {e}")
        for e in errors:
            say(f"run:  {e}")
        check("no error packets while running", not errors)

        say("\n== stop the VM ==")
        link.request(VM, 0x40)
        time.sleep(0.5)
        say(f"after VM reset: TCA {p_a} = {level(link, dev, p_a)}, TCA {p_b} = {level(link, dev, p_b)}")
    say(f"\n{'ALL PASS' if fails == 0 else f'{fails} FAILED'}")
    if out:
        out.write_text("\n".join(report), encoding="utf-8")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
