"""Persistence across a reboot (devkit, console UART link): recorded actions (NVS) and VM retained values.

Usage: python persist_tests.py COM3
Resets the board twice (RTS pulse).
"""
from __future__ import annotations

import struct
import sys
import time

from runit_link import Link
import vm_tests as v

ACTION_ID = 11
fails = 0


def check(name: str, cond: bool, detail: str = "") -> None:
    global fails
    print(f"{'PASS' if cond else 'FAIL'}  {name}" + (f"   ({detail})" if detail else ""))
    if not cond:
        fails += 1


def retained_program() -> v.Program:
    """Like vm_tests.build(), but count is retentive."""
    p = v.Program()
    p.obj(v.OBJ_TICK, v.T_B, "tick")
    p.obj(v.OBJ_COUNT, v.T_F, "count", retentive=True)
    p.obj(v.OBJ_PERIOD, v.T_U32, "period")
    for i in (v.OBJ_TICK, v.OBJ_COUNT, v.OBJ_PERIOD):
        p.acc(i, i)
    p.block(0, 0, v.BLK_PERIODIC, ins=[v.OBJ_PERIOD], qs=[v.OBJ_TICK], custom=struct.pack("<IBBHQ", 0, 0, 0, 0, 0))
    code = bytes([v.EXPR_IN, 0, v.EXPR_IN, 1, v.EXPR_ADD, v.EXPR_END])
    p.block(1, 1, v.BLK_EXPR, ins=[v.OBJ_COUNT, v.OBJ_TICK], qs=[v.OBJ_COUNT], custom=struct.pack("<BBH", 0, 0, len(code)) + code)
    p.block(2, 2, v.BLK_IO_TOGGLE, ens=[v.OBJ_TICK], custom=struct.pack("<QBBB5s", 1 << v.PIN, 0, v.PIN, 0, b"\0" * 5))
    return p


def main() -> int:
    port = sys.argv[1]
    with Link(port, reset=True) as link:
        time.sleep(0.3)
        # action: set pin 6 output + high, recorded
        link.call("packet_sys_io_reset_t", device_id=0, pin=6)
        link.request(0x03, 0x04, bytes([ACTION_ID]))
        check("record start", link.request(0x03, 0x02, bytes([ACTION_ID])).ok)
        link.call("packet_sys_io_set_mode_t", device_id=0, pin=6, mode=3)
        link.call("packet_sys_io_set_level_t", device_id=0, pin=6, level=1)
        check("record stop", link.request(0x03, 0x03).ok)

        # VM with a retentive counter; teardown (0x40) saves it
        link.call("packet_sys_io_reset_t", device_id=0, pin=v.PIN)
        link.call("packet_sys_io_set_mode_t", device_id=0, pin=v.PIN, mode=3)
        link.request(v.VM, 0x48, bytes([10]))  # forget retained values from earlier runs
        p = retained_program()
        check("upload retentive program", v.upload(link, p, 100))
        m = link.mark()
        link.request(v.VM, 0x47, bytes([1]) + struct.pack("<H", v.OBJ_COUNT))
        link.request(v.VM, 0x48, bytes([5]))
        time.sleep(1.5)
        saved = v.count_now(link, m)
        m = link.mark()
        check("vm reset (saves retained)", link.request(v.VM, 0x40).ok)
        time.sleep(0.5)
        print(f"      count at teardown ~{saved}")
        for l in link.logs_since(m):
            if "retain" in l.lower():
                print("      LOG", l)

    with Link(port, reset=True) as link:
        time.sleep(0.3)
        r = link.request(0x03, 0x01, bytes([ACTION_ID]))
        check("recorded action survives reboot", r is not None and r.ok, str(r))
        lv = link.call("packet_sys_io_get_level_t", device_id=0, pin=6)
        check("action replay set pin 6 high", lv is not None and lv.ok and lv.fields["level"] == 1, str(lv))
        link.request(0x03, 0x04, bytes([ACTION_ID]))
        link.call("packet_sys_io_reset_t", device_id=0, pin=6)

        link.call("packet_sys_io_set_mode_t", device_id=0, pin=v.PIN, mode=3)
        check("re-upload after reboot", v.upload(link, retained_program(), 100))
        m = link.mark()
        link.request(v.VM, 0x47, bytes([1]) + struct.pack("<H", v.OBJ_COUNT))
        link.request(v.VM, 0x48, bytes([5]))
        time.sleep(0.25)
        # vals[0] is the snapshot sent on subscribe while stopped (the loaded 0);
        # retained values are restored on the first start, so the next one continues.
        vals = [struct.unpack("<f", x)[0] for x in v.values(link, m).get(v.OBJ_COUNT, [])]
        first = vals[1] if len(vals) > 1 else None
        check("retained count restored on first start", first is not None and saved is not None and first >= saved,
              f"first after start={first} saved~{saved} values={vals[:5]}")
        link.request(v.VM, 0x40)
        link.request(v.VM, 0x48, bytes([10]))
        link.call("packet_sys_io_reset_t", device_id=0, pin=v.PIN)
        errs = link.errors_since(0)
        for e in errs:
            print("   !", e)
    print(f"\n{fails} failure(s)")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
