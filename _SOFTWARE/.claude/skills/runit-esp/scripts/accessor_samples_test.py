"""Accessors the app compiles (nested, by name, dynamic, nested dynamic), run on a board.

Usage: python accessor_samples_test.py [COM4]
Reads app/tests/domain/compiler/fixtures/accessor-samples.json (made by the app's
app/tests/domain/compiler/accessorSamples.test.ts): the objects and accessors of a program, as upload
frames. Adds a PERIODIC block (output `tick`, 100 ms) and one EXPR block per
sample (output = the value its path reads, triggered by `tick`; the app has no
block compiler yet), runs the program, and checks the values the
board reports; then writes `sel` while running and checks again. Resets the
VM at the end.
"""
from __future__ import annotations

import json
import struct
import sys
import time
from pathlib import Path

from runit_link import Link
from vm_tests import BLK_EXPR, BLK_PERIODIC, EXPR_END, EXPR_IN, NO_ID, VM, align4, st, values

FIXTURE = Path(__file__).resolve().parents[4] / "app" / "tests" / "domain" / "compiler" / "fixtures" / "accessor-samples.json"
EXEC_NORMAL = 5
fails = 0


def check(name: str, cond: bool, detail: str = "") -> None:
    global fails
    print(f"{'PASS' if cond else 'FAIL'}  {name}" + (f"   ({detail})" if detail else ""))
    if not cond:
        fails += 1


def block(blk_id: int, btype: int, ins: list[int], qs: list[int], custom: bytes) -> bytes:
    hdr = struct.pack("<HHBBBBBBHH", blk_id, blk_id, btype, len(ins), len(qs), 0, 0, 1, len(custom), NO_ID)
    return hdr + b"".join(struct.pack("<H", x) for x in (*ins, *qs)) + custom


def block_arena(b: bytes) -> int:
    in_cnt, q_cnt, en_cnt = b[5], b[6], b[7]
    return align4(16 + (in_cnt + q_cnt + en_cnt) * 4 + struct.unpack_from("<H", b, 10)[0])


def expr_block(blk_id: int, acc: int, out: int, tick_acc: int) -> bytes:
    code = bytes([EXPR_IN, 0, EXPR_END])  # input 1 (tick) only triggers it
    return block(blk_id, BLK_EXPR, [acc, tick_acc], [out], struct.pack("<BBH", 0, 0, len(code)) + code)


def read_outputs(link: Link, samples: list[dict], since: int) -> dict[str, float | None]:
    got = values(link, since)
    return {s["key"]: (struct.unpack("<f", got[s["output"]][-1])[0] if s["output"] in got else None) for s in samples}


def main() -> int:
    port = sys.argv[1] if len(sys.argv) > 1 else "COM4"
    fx = json.loads(FIXTURE.read_text())
    samples = fx["samples"]
    frames = [bytes.fromhex(f) for f in fx["frames"]]
    tick = fx["tick"]
    blocks = [block(0, BLK_PERIODIC, [], [tick["output"]], struct.pack("<IBBHQ", 100, 0, 0, 0, 0))]
    blocks += [expr_block(i + 1, s["accessor"], s["output"], tick["accessor"]) for i, s in enumerate(samples)]
    total = fx["arenaBytes"] + align4(len(blocks) * 4) + sum(block_arena(b) for b in blocks)
    counts = fx["counts"]
    open_frame = struct.pack("<BBHHHI", VM, 0x41, counts["objects"], counts["accessors"], len(blocks), total)

    with Link(port) as link:
        steps = [("reset", link.request(VM, 0x40))]
        for i, frame in enumerate([open_frame, *frames[1:]]):
            steps.append((f"frame {i} ({frame[1]:02x})", link.request(frame[0], frame[1], frame[2:])))
        for name, b in zip(["periodic", *(s["key"] for s in samples)], blocks):
            steps.append((f"block {name}", link.request(VM, 0x45, b)))
        for name, r in steps:
            check(f"upload {name}", r is not None and r.ok, st(r))
        if fails:
            return 1

        outputs = [s["output"] for s in samples]
        since = link.mark()
        check("subscribe", (r := link.request(VM, 0x47, bytes([len(outputs)]) + b"".join(struct.pack("<H", o) for o in outputs))) is not None and r.ok, st(r))
        check("run", (r := link.request(VM, 0x48, bytes([EXEC_NORMAL]))) is not None and r.ok, st(r))
        time.sleep(1.0)
        for key, v in read_outputs(link, samples, since).items():
            want = next(s for s in samples if s["key"] == key)["expect"][0]
            check(f"{key} (sel = 2)", v is not None and abs(v - want) < 1e-4, f"got {v}, want {want}")

        since = link.mark()
        check("write sel = 0 while running", (r := link.request(VM, 0x43, bytes([1]) + struct.pack("<HHH", fx["sel"], 0, 1) + b"\0")) is not None and r.ok, st(r))
        time.sleep(1.0)
        for key, v in read_outputs(link, samples, since).items():
            s = next(s for s in samples if s["key"] == key)
            want = s["expect"][1]
            if v is None and want == s["expect"][0]:
                check(f"{key} (sel = 0)", True, "unchanged, not resent")
                continue
            check(f"{key} (sel = 0)", v is not None and abs(v - want) < 1e-4, f"got {v}, want {want}")
        errors = link.errors_since(0)
        check("no error packets", not errors, "; ".join(str(e) for e in errors[:3]))
        link.request(VM, 0x40)

    print(f"\n{'ALL PASS' if fails == 0 else f'{fails} FAILED'}")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
