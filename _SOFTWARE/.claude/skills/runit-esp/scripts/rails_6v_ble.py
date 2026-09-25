"""Both rails to 6 V over BLE (resume a suspended rail first), report state and events."""
import sys
import time

from runit_link import open_link
from power_ble import rd, rails

with open_link(sys.argv[1] if len(sys.argv) > 1 else "BLE") as link:
    time.sleep(0.5)
    print("start :", rails(link))
    print("power :", rd(link, "packet_sys_power_get_status_t"))
    for dev, name in ((11, "B"), (10, "A")):
        print(f"resume {name}:", rd(link, "packet_sys_device_resume_t", device_id=dev))
        print(f"enable {name}:", rd(link, "packet_sys_power_vreg_set_enable_t", device_id=dev, state=1))
        print(f"{name} -> 6 V :", rd(link, "packet_sys_power_vreg_set_voltage_t", device_id=dev, voltage_mV=6000))
        time.sleep(0.8)
        print("        ", rails(link))
    time.sleep(1.5)
    print("end   :", rails(link))
    for _, l in link.logs:
        if l.strip() and "dec_sys" not in l:
            print("  LOG", l)
    for _, e in link.errors:
        print("  ERR", e)
