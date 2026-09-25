"""Hold a BLE link for N seconds (one status call every 5 s) with the serial log attached.
Usage: python ble_hold_test.py COM4 [seconds]
"""
import subprocess
import sys
import time

from runit_link import Link

CLIENT = r'''
import sys, time
sys.path.insert(0, r"{here}")
from runit_link import open_link
link = open_link("BLE")
print("connected mtu", link.mtu, flush=True)
t0 = time.time()
while time.time() - t0 < {secs}:
    try:
        r = link.call("packet_sys_power_get_status_t", timeout=3)
    except Exception as e:
        print(f"{{time.time() - t0:6.1f}}s EXC {{e!r}}", flush=True)
        break
    print(f"{{time.time() - t0:6.1f}}s {{'ok' if r and r.ok else r}}", flush=True)
    time.sleep(5)
link.close()
'''

if __name__ == "__main__":
    import os
    port = sys.argv[1] if len(sys.argv) > 1 else "COM4"
    secs = float(sys.argv[2]) if len(sys.argv) > 2 else 240
    here = os.path.dirname(os.path.abspath(__file__))
    with Link(port) as link:
        t = link.mark()
        p = subprocess.run([sys.executable, "-c", CLIENT.format(here=here, secs=secs)], capture_output=True, text=True, timeout=secs + 60)
        lines = (p.stdout + p.stderr).strip().splitlines()
        bad = [l for l in lines if "ok" not in l.split()[-1:]]
        print(f"client: {len(lines)} lines, last: {lines[-1] if lines else '-'}")
        for l in bad[:20]:
            print("  ", l)
        time.sleep(2)
        for l in link.logs_since(t):
            if any(k in l for k in ("sys_ble", "Disconnect", "dvertis", "NimBLE", "E (", "W (", "rst:", "reset reason")):
                print("  LOG", l)
