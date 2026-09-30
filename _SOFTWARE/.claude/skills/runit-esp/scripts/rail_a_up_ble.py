"""Rail A up to 6 V despite its start-up OCP flag: OCP_CRITICAL -> NOTIFY while enabling, then back to DISABLE."""
import sys
import time

from runit_link import open_link
from power_ble import rd, rails

OCP_CRITICAL, NOTIFY, DISABLE = 5, 0, 1

with open_link(sys.argv[1] if len(sys.argv) > 1 else "BLE") as link:
    time.sleep(0.5)
    print("start :", rails(link))
    print("OCP_CRITICAL -> NOTIFY:", rd(link, "packet_sys_power_set_response_t", event=OCP_CRITICAL, response=NOTIFY))
    print("resume A:", rd(link, "packet_sys_device_resume_t", device_id=10))
    print("enable A:", rd(link, "packet_sys_power_vreg_set_enable_t", device_id=10, state=1))
    print("A -> 6 V:", rd(link, "packet_sys_power_vreg_set_voltage_t", device_id=10, voltage_mV=6000))
    time.sleep(1.0)
    print("        ", rails(link))
    print("OCP_CRITICAL -> DISABLE:", rd(link, "packet_sys_power_set_response_t", event=OCP_CRITICAL, response=DISABLE))
    time.sleep(1.0)
    print("end   :", rails(link))
    for _, l in link.logs:
        if l.strip() and "dec_sys" not in l and "subscription" not in l:
            print("  LOG", l)
    for _, e in link.errors:
        print("  ERR", e)
