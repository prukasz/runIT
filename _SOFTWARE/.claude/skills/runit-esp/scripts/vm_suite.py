"""VM suite: the program runtime (vm_core) and its block palette (vm_blocks) on a runIT board, over the router.

  python vm_suite.py --port COM9                  # everything
  python vm_suite.py --port COM9 --only expr,for  # tests whose name contains one of these
  python vm_suite.py --port COM9 --list           # the tests
  python vm_suite.py --port COM9 --skip-hw        # no test that touches a pin or reads a chip

Written against the code of vm_core / vm_blocks as it is (VM_CORE.MD, VM_BLOCKS.MD), not against the old suites. Run with
the IDF venv Python. Most tests load a program, put the VM in scan mode and step it one pass at a time (ONCE): the result
of a pass is read from the telemetry sent at its end, so nothing depends on timing. Only the time blocks (TIMER,
PERIODIC), the supervisor modes and the retained values run in real time and use wide margins.

The suite resets the board when it opens the port (USB-Serial-JTAG), loads its own programs, never starts a program
with a motor, servo or regulator block, and removes the retained values it made.

  vm_lib.py       builder, loader, telemetry reader        vm_t_core.py     wire protocol, expressions
  vm_t_blocks.py  flow, loops, data, state and time blocks vm_t_runtime.py  supervisor, runtime writes, telemetry, retained
  vm_t_hw.py      device blocks and events on the real PCB
"""

from __future__ import annotations

import argparse
import sys
import time

from router_link import Link
from vm_lib import RESULTS, TESTS, Vm, check, section

import vm_t_core  # noqa: F401  (importing registers the tests)
import vm_t_blocks  # noqa: F401
import vm_t_runtime  # noqa: F401
import vm_t_hw  # noqa: F401


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--port")
    ap.add_argument("--only", default="", help="comma list: run the tests whose name contains one of these")
    ap.add_argument("--skip-hw", action="store_true")
    ap.add_argument("--list", action="store_true")
    ap.add_argument("--echo", action="store_true")
    a = ap.parse_args()
    sel = [s for s in a.only.split(",") if s]
    todo = [(n, hw, fn) for (n, hw, fn) in TESTS if (not sel or any(s in n for s in sel)) and not (a.skip_hw and hw)]
    if a.list:
        for n, hw, _ in TESTS:
            print(("hw   " if hw else "     ") + n)
        return 0
    if not a.port:
        ap.error("--port is required")
    t0 = time.time()
    with Link(a.port, echo=a.echo) as link:
        time.sleep(1.5)
        vm = Vm(link)
        vm.reset()
        for name, hw, fn in todo:
            section(name)
            t_test = time.time()
            try:
                fn(vm)
            except Exception as e:  # a test that crashes is a failure, the rest still run
                import traceback

                traceback.print_exc()
                check(f"{name}: the test ran to the end", False, repr(e))
            vm.reset()
            print(f"   ({time.time() - t_test:.0f} s)", flush=True)
    bad = [(s, n, d) for (s, n, o, d) in RESULTS if not o]
    print(f"\n{len(RESULTS) - len(bad)}/{len(RESULTS)} passed in {time.time() - t0:.0f} s")
    for s, n, d in bad:
        print(f"  FAIL [{s}] {n}" + (f"  -- {d}" if d else ""))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
