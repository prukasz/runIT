"""BLE re-advertising check with the serial log attached (USB).
1. clean connect / disconnect, 2. connect then kill the client process (stale script).
After each drop: look for the disconnect / advertising log lines and scan for the board.
Usage: python ble_readv_test.py COM4
"""
import asyncio
import subprocess
import sys
import time

from runit_link import Link

CLIENT = r'''
import sys, time
sys.path.insert(0, r"{here}")
from runit_link import open_link
link = open_link("BLE")
print("client connected mtu", link.mtu, flush=True)
print(link.call("packet_sys_power_get_status_t"), flush=True)
if "{mode}" == "clean":
    link.close()
    print("client closed", flush=True)
else:
    time.sleep(60)
'''


async def scan(seconds: float) -> bool:
    from bleak import BleakScanner
    dev = await BleakScanner.find_device_by_name("runit", timeout=seconds)
    return dev is not None


def dump(link: Link, t: int) -> None:
    for l in link.logs_since(t):
        if any(k in l for k in ("sys_ble", "BLE", "Disconnect", "Connect", "dvertis", "NimBLE", "E (", "W (")):
            print("  LOG", l)
    for e in link.errors_since(t):
        print("  ERR", e)


def run(link: Link, mode: str) -> None:
    import os
    here = os.path.dirname(os.path.abspath(__file__))
    t = link.mark()
    p = subprocess.Popen([sys.executable, "-c", CLIENT.format(here=here, mode=mode)], stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    if mode == "clean":
        out, _ = p.communicate(timeout=60)
        print(out.strip())
    else:
        deadline = time.time() + 40
        while time.time() < deadline:
            line = p.stdout.readline()
            if not line:
                break
            print(" ", line.rstrip())
            if line.startswith("seq=") or "OK" in line or "ERROR" in line or "None" in line:
                break
        p.kill()
        print("  client killed")
    # Windows keeps the link until its supervision timeout; give it time
    for i in range(8):
        time.sleep(5)
        found = asyncio.run(scan(5))
        print(f"  +{(i + 1) * 10}s scan: {'FOUND' if found else 'not found'}")
        if found:
            break
    dump(link, t)


with Link(sys.argv[1] if len(sys.argv) > 1 else "COM4") as link:
    time.sleep(0.5)
    for mode in sys.argv[2:] or ["clean", "kill"]:
        print(f"== {mode}")
        run(link, mode)
