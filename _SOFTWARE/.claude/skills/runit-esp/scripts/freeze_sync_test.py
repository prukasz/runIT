"""Simplest freeze / sync test on the TCA6424A (device 1), with status LED 22.

Usage: python freeze_sync_test.py [COM4]
  1. LED 22 on (level 1)
  2. freeze the device
  3. set LED 22 off: accepted, but deferred - the LED stays ON, and a read still answers 1 (the snapshot)
  4. wait 3 s (watch the LED: still on)
  5. sync: the deferred write goes out - the LED turns OFF, a read answers 0
Ends with the LED back on (its state after boot).
"""
from __future__ import annotations

import sys
import time

from runit_link import Link
from vm_tests import st

TCA, LED = 1, 22
fails = 0


def check(name: str, cond: bool, detail: str = "") -> None:
    global fails
    print(f"{'PASS' if cond else 'FAIL'}  {name}" + (f"   ({detail})" if detail else ""))
    if not cond:
        fails += 1


def level(link: Link):
    r = link.call("packet_sys_io_get_level_t", device_id=TCA, pin=LED)
    return r.fields["level"] if r and r.ok and r.fields else None


def ok(name: str, r) -> None:
    check(name, r is not None and r.ok, st(r))


def main() -> int:
    port = sys.argv[1] if len(sys.argv) > 1 else "COM4"
    with Link(port) as link:
        time.sleep(0.3)
        ok("LED 22 on", link.call("packet_sys_io_set_level_t", device_id=TCA, pin=LED, level=1))
        check("reads 1", level(link) == 1, f"got {level(link)}")

        ok("freeze the TCA", link.call("packet_sys_device_freeze_t", device_id=TCA))
        ok("set LED 22 off while frozen (deferred)", link.call("packet_sys_io_set_level_t", device_id=TCA, pin=LED, level=0))
        check("still reads 1 while frozen (snapshot; the LED is still ON)", level(link) == 1, f"got {level(link)}")
        print("      ... watching the LED for 3 s: it should stay ON")
        time.sleep(3.0)

        ok("sync the TCA", link.call("packet_sys_device_sync_t", device_id=TCA))
        time.sleep(0.2)
        check("reads 0 after sync (the deferred write went out: LED OFF)", level(link) == 0, f"got {level(link)}")

        ok("LED 22 back on", link.call("packet_sys_io_set_level_t", device_id=TCA, pin=LED, level=1))
        check("reads 1", level(link) == 1, f"got {level(link)}")
        errors = link.errors_since(0)
        check("no error packets after boot", not [e for e in errors if "DEV_NOT_FOUND" not in str(e)], "; ".join(str(e) for e in errors[:3]))
    print(f"\n{'ALL PASS' if fails == 0 else f'{fails} FAILED'}")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
