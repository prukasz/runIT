"""DRV8962 #2 (PCA9685-driven) bring-up: rail A to 5 V, VM switch (LM73100, TCA 12: DRV2 VM <- rail A) on,
VREF readback, channel 0 forward at 50 %.
Leaves the bridge driving; `python hbridge_test.py COM4 stop` brakes it, opens the VM switch and turns rail A off.
Usage: python hbridge_test.py COM4|BLE [stop]
"""
import sys
import time

from runit_link import open_link
from power_ble import rd, rails

DRV, DAC, TCA = 6, 4, 1
RAIL_A = 10
VM_SW, NFAULT = 12, 10  # LM73100 DRV2 VM <- rail A; DRV2 nFAULT

with open_link(sys.argv[1] if len(sys.argv) > 1 else "COM4") as link:
    time.sleep(0.5)
    if len(sys.argv) > 2 and sys.argv[2] == "stop":
        print("brake ch0:", rd(link, "packet_sys_hbridge_brake_t", device_id=DRV, channel=0))
        print("VM switch off:", rd(link, "packet_sys_io_set_level_t", device_id=TCA, pin=VM_SW, level=0))
        print("rail A off:", rd(link, "packet_sys_power_vreg_set_enable_t", device_id=RAIL_A, state=0))
        sys.exit(0)

    print("start   :", rails(link))
    print("VREF (DAC OUT1):", rd(link, "packet_sys_io_get_voltage_t", device_id=DAC, pin=1))
    print("rail A 5 V:", rd(link, "packet_sys_power_vreg_set_voltage_t", device_id=RAIL_A, voltage_mV=5000),
          rd(link, "packet_sys_power_vreg_set_enable_t", device_id=RAIL_A, state=1))
    time.sleep(1.0)
    print("rails   :", rails(link))
    print("nFAULT2 before VM:", rd(link, "packet_sys_io_get_level_t", device_id=TCA, pin=NFAULT))
    print("VM switch (TCA 12) on:", rd(link, "packet_sys_io_set_level_t", device_id=TCA, pin=VM_SW, level=1))
    time.sleep(0.5)
    print("rails   :", rails(link))
    print("nFAULT2 with VM:", rd(link, "packet_sys_io_get_level_t", device_id=TCA, pin=NFAULT))
    print("clear fault ch0:", rd(link, "packet_sys_hbridge_clear_fault_t", device_id=DRV, channel=0))
    print("drive ch0 +0.5:", rd(link, "packet_sys_hbridge_set_drive_t", device_id=DRV, channel=0, magnitude=0.5))
    time.sleep(1.0)
    print("rails   :", rails(link))
    print("nFAULT2 :", rd(link, "packet_sys_io_get_level_t", device_id=TCA, pin=NFAULT))
    for _, l in link.logs:
        if l.strip() and ("E (" in l or "W (" in l or "fault" in l.lower()):
            print("  LOG", l)
    for _, e in link.errors:
        print("  ERR", e)
