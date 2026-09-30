"""VM tests on a bare devkit over the console UART link (runit_link.py).

Program (3 blocks, built from data-structures/vm/*.generated.json layouts):
    obj 0 tick   B     PERIODIC output (gate, 1 every period)
    obj 1 count  F     EXPR: count = count + tick   (triggered, so it counts ticks)
    obj 2 period U32   PERIODIC period input (ms), overridable while running
    blk 0 PERIODIC  in period -> q tick
    blk 1 EXPR      in count, tick -> q count
    blk 2 IO_TOGGLE en tick, device 0 pin 5
IDs are registry slots: 0 .. count - 1 (accessor i points at object i).

Usage: python vm_tests.py COM3 [--reset]
"""
from __future__ import annotations

import struct
import sys
import time

from runit_link import Link, tag_name

VM = 0x04
T_PTR, T_U8, T_U32, T_I32, T_F, T_B = 1, 2, 3, 4, 5, 6
WIDTH = {T_U8: 1, T_U32: 4, T_I32: 4, T_F: 4, T_B: 1}
BLK_EXPR, BLK_IO_TOGGLE, BLK_PERIODIC = 1, 11, 13
EXPR_END, EXPR_IN, EXPR_ADD = 0, 1, 6
NO_ID = 0xFFFF
PIN = 5
OBJ_TICK, OBJ_COUNT, OBJ_PERIOD = 0, 1, 2


def align4(n: int) -> int:
    return (n + 3) & ~3


def obj_head(t: int, count: int, name: str, mutable: bool = True, retentive: bool = False) -> bytes:
    """vm_obj_head_t wire bytes (esp32-gcc-bitfield-v1, vm-model.generated.json)."""
    flags = (1 if mutable else 0) | (1 << 3 if name else 0) | (1 << 4 if retentive else 0)
    return struct.pack("<HBB", WIDTH[t] * count, (t & 0x0F) | (len(name) << 4), flags)


class Program:
    def __init__(self):
        self.objs: list[tuple[int, bytes, str]] = []
        self.accs: list[tuple[int, int]] = []
        self.blocks: list[bytes] = []
        self.size = 0

    def obj(self, oid: int, t: int, name: str, count: int = 1, **kw) -> None:
        head = obj_head(t, count, name, **kw)
        self.objs.append((oid, head, name))
        self.size += align4(4 + WIDTH[t] * count + len(name))

    def acc(self, acc_id: int, root: int) -> None:
        self.accs.append((acc_id, root))
        self.size += align4(20)

    def block(self, blk_id: int, idx: int, btype: int, ins=(), qs=(), ens=(), custom: bytes = b"", en_mode=0, on_error=1, eno=NO_ID) -> None:
        hdr = struct.pack("<HHBBBBBBHH", blk_id, idx, btype, len(ins), len(qs), len(ens), en_mode, on_error, len(custom), eno)
        body = b"".join(struct.pack("<H", x) for x in (*ins, *qs, *ens)) + custom
        self.blocks.append(hdr + body)
        self.size += align4(16 + (len(ins) + len(qs) + len(ens)) * 4 + len(custom))

    def total_size(self) -> int:
        regs = sum(align4(n * 4) for n in (len(self.objs), len(self.accs), len(self.blocks)) if n)
        return regs + self.size


def build() -> Program:
    p = Program()
    p.obj(OBJ_TICK, T_B, "tick")
    p.obj(OBJ_COUNT, T_F, "count")
    p.obj(OBJ_PERIOD, T_U32, "period")
    for i in (OBJ_TICK, OBJ_COUNT, OBJ_PERIOD):
        p.acc(i, i)
    periodic = struct.pack("<IBBHQ", 0, 0, 0, 0, 0)  # period 0 -> taken from the wired input
    p.block(0, 0, BLK_PERIODIC, ins=[OBJ_PERIOD], qs=[OBJ_TICK], custom=periodic)
    code = bytes([EXPR_IN, 0, EXPR_IN, 1, EXPR_ADD, EXPR_END])
    p.block(1, 1, BLK_EXPR, ins=[OBJ_COUNT, OBJ_TICK], qs=[OBJ_COUNT], custom=struct.pack("<BBH", 0, 0, len(code)) + code)
    toggle = struct.pack("<QBBB5s", 1 << PIN, 0, PIN, 0, b"\0" * 5)
    p.block(2, 2, BLK_IO_TOGGLE, ens=[OBJ_TICK], custom=toggle)
    return p


class T:
    fails = 0

    @staticmethod
    def check(name: str, cond: bool, detail: str = "") -> None:
        print(f"{'PASS' if cond else 'FAIL'}  {name}" + (f"   ({detail})" if detail else ""))
        if not cond:
            T.fails += 1


def st(r) -> str:
    if r is None:
        return "no response"
    return "OK" if r.ok else f"{r.err}"


def upload(link: Link, p: Program, period_ms: int) -> bool:
    steps = [("reset", link.request(VM, 0x40))]
    steps.append(("open", link.request(VM, 0x41, struct.pack("<HHHI", len(p.objs), len(p.accs), len(p.blocks), p.total_size()))))
    recs = b"".join(struct.pack("<H", oid) + head + name.encode() for oid, head, name in p.objs)
    steps.append(("objects", link.request(VM, 0x42, bytes([len(p.objs)]) + recs)))
    data = struct.pack("<HHH", OBJ_PERIOD, 0, 4) + struct.pack("<I", period_ms)
    steps.append(("set period", link.request(VM, 0x43, bytes([1]) + data)))
    accs = b"".join(struct.pack("<HHBB", a, r, 0, 0) for a, r in p.accs)
    steps.append(("accessors", link.request(VM, 0x44, bytes([len(p.accs)]) + accs)))
    for i, b in enumerate(p.blocks):
        steps.append((f"block {i + 1}", link.request(VM, 0x45, b)))
    good = True
    for name, r in steps:
        if r is None or not r.ok:
            print(f"      upload step {name}: {st(r)}")
            good = False
    return good


def values(link: Link, since: int) -> dict[int, list]:
    """Decode 0x04 0x43 telemetry records after `since` -> {obj_id: [raw bytes, ...]}."""
    out: dict[int, list] = {}
    for fr in link.telemetry_since(since):
        b = fr.body
        if len(b) < 3 or b[0] != VM or b[1] != 0x43:
            continue
        n, off = b[2], 3
        for _ in range(n):
            oid, start, ln = struct.unpack_from("<HHH", b, off)
            off += 6
            out.setdefault(oid, []).append(b[off:off + ln])
            off += ln
    return out


def count_now(link: Link, since: int) -> float | None:
    v = values(link, since).get(OBJ_COUNT)
    return struct.unpack("<f", v[-1])[0] if v else None


def level(link: Link) -> int | None:
    r = link.call("packet_sys_io_get_level_t", device_id=0, pin=PIN)
    return r.fields["level"] if r and r.ok and r.fields else None


def main() -> int:
    with Link(sys.argv[1], reset="--reset" in sys.argv) as link:
        time.sleep(0.3)
        link.call("packet_sys_io_reset_t", device_id=0, pin=PIN)
        T.check("pin 5 output", bool(link.call("packet_sys_io_set_mode_t", device_id=0, pin=PIN, mode=3).ok))
        p = build()
        m = link.mark()
        T.check("upload", upload(link, p, 100), f"total_size={p.total_size()}")
        for e in link.errors_since(m):
            print("      !", e)

        T.check("subscribe count+tick", st(link.request(VM, 0x47, bytes([2]) + struct.pack("<HH", OBJ_COUNT, OBJ_TICK))) == "OK")
        m = link.mark()
        T.check("run (normal mode)", st(link.request(VM, 0x48, bytes([5]))) == "OK")
        # Back-to-back reads (~70-120 ms apart). Don't add a sleep: a ~200 ms
        # spacing equals the toggle cycle and samples the same phase every time.
        levels = [level(link) for _ in range(12)]
        time.sleep(1.0)
        c1 = count_now(link, m)
        T.check("count increases (period 100 ms)", c1 is not None and c1 >= 12, f"count after ~2-3 s = {c1}")
        T.check("pin 5 toggles", len(set(x for x in levels if x is not None)) == 2, f"levels={levels}")

        # runtime override: period 100 -> 500 ms
        m2 = link.mark()
        r = link.request(VM, 0x43, bytes([1]) + struct.pack("<HHH", OBJ_PERIOD, 0, 4) + struct.pack("<I", 500))
        T.check("override period while running", r is not None and r.ok, st(r))
        time.sleep(0.4)
        a = count_now(link, m2)
        time.sleep(2.0)
        b = count_now(link, m2)
        rate = (b - a) / 2.0 if a is not None and b is not None else None
        T.check("rate follows override (~2/s)", rate is not None and 1.0 <= rate <= 3.0, f"rate={rate}")
        tel_period = values(link, m2).get(OBJ_PERIOD)
        T.check("unsubscribed object not sent", tel_period is None)

        # structure changes refused while running
        r = link.request(VM, 0x42, bytes([1]) + struct.pack("<H", 9) + obj_head(T_U8, 1, "x") + b"x")
        T.check("add object while running refused", r is not None and not r.ok, st(r))

        # pause / step
        T.check("pause", st(link.request(VM, 0x48, bytes([6]))) == "OK")
        time.sleep(0.3)
        mp = link.mark()
        time.sleep(1.2)
        c_paused = count_now(link, mp)
        T.check("no telemetry while paused", c_paused is None, f"got {c_paused}")
        T.check("resume", st(link.request(VM, 0x48, bytes([7]))) == "OK")
        time.sleep(1.2)
        T.check("counts again after resume", count_now(link, mp) is not None)

        # once (single pass) in scan mode
        T.check("scan mode", st(link.request(VM, 0x48, bytes([0]))) == "OK")
        r = link.request(VM, 0x48, bytes([1]))
        T.check("once", r is not None and r.ok, st(r))
        r = link.request(VM, 0x48, bytes([2]))
        T.check("block mode", r is not None and r.ok, st(r))
        r = link.request(VM, 0x48, bytes([3]))
        T.check("next block", r is not None and r.ok, st(r))
        T.check("normal mode again", st(link.request(VM, 0x48, bytes([5]))) == "OK")

        # rewind: restarts the pass at block 0, keeps values, leaves a running VM frozen
        T.check("rewind to start", st(link.request(VM, 0x48, bytes([4]))) == "OK")
        time.sleep(0.3)
        m3 = link.mark()
        time.sleep(1.0)
        T.check("frozen after rewind (no telemetry)", count_now(link, m3) is None, f"count={count_now(link, m3)}")
        T.check("resume after rewind", st(link.request(VM, 0x48, bytes([7]))) == "OK")
        time.sleep(1.0)
        after = count_now(link, m3)
        T.check("counts on after resume, value kept", after is not None and after > 5, f"count={after}")

        # teardown
        T.check("vm reset", st(link.request(VM, 0x40)) == "OK")
        lv1 = level(link)
        time.sleep(0.5)
        T.check("pin stops toggling after reset", level(link) == lv1)
        errs = link.errors_since(m)
        if errs:
            print("  errors during the run:")
            for e in errs:
                print("   !", e)
        link.call("packet_sys_io_reset_t", device_id=0, pin=PIN)
    print(f"\n{T.fails} failure(s)")
    return 1 if T.fails else 0


if __name__ == "__main__":
    sys.exit(main())
