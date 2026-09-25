"""Read every TCA6424A pin that no known function uses (they are inputs after reset).
Known: 0 PCA /OE, 1 / 2 TPS INT, 5 / 6 INA CRIT / WARN, 12 AP33772S INT, 16 / 17 TPS EN, 22 / 23 LEDs.
Usage: python tca_scan.py BLE|COM4
"""
import sys
import time

from runit_link import open_link
from power_ble import rd, rails

TCA = 1
KNOWN = {0, 1, 2, 5, 6, 12, 16, 17, 22, 23}

with open_link(sys.argv[1] if len(sys.argv) > 1 else "BLE") as link:
    time.sleep(0.5)
    print("rails:", rails(link))
    print("power:", rd(link, "packet_sys_power_get_status_t"))
    levels = {}
    for pin in range(24):
        if pin in KNOWN:
            continue
        m = rd(link, "packet_sys_io_set_mode_t", device_id=TCA, pin=pin, mode=0)
        lv = rd(link, "packet_sys_io_get_level_t", device_id=TCA, pin=pin)
        levels[pin] = lv.get("level") if isinstance(lv, dict) else f"{m} / {lv}"
    print("TCA free pins:", "  ".join(f"{p}={v}" for p, v in levels.items()))
    print("high:", [p for p, v in levels.items() if v == 1])
