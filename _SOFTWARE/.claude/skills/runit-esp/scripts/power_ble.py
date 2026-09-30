"""Power / rail checks over any link (BLE when the USB-C port feeds power).

Usage (a Python with bleak for BLE):
    python power_ble.py BLE status                  # rails, input, PD offers, power status
    python power_ble.py BLE pd <mV> [mA]            # request a USB-PD contract (AP33772S)
    python power_ble.py BLE rail <A|B> <mV>         # enable a rail and step it to mV (1 V steps)
    python power_ble.py BLE off <A|B>
    python power_ble.py BLE watch <seconds>         # log rails every 0.5 s (short tests)
Rails: A = TPS55289 0x74 (device 10, INA ch2), B = 0x75 (device 11, INA ch0); input = INA ch1.
"""
from __future__ import annotations

import sys
import time

from runit_link import open_link

INA, AP = 12, 13
RAILS = {"A": (10, 2), "B": (11, 0)}


def rd(link, packet, **kw):
    r = link.call(packet, timeout=2.0, **kw)
    if r is None:
        return "no response"
    return (r.fields or "OK") if r.ok else r.err


def v(x, key):
    return x.get(key) if isinstance(x, dict) else x


def rails(link) -> str:
    out = []
    for name, ch in (("B", 0), ("in", 1), ("A", 2)):
        mv = v(rd(link, "packet_sys_power_monitor_get_voltage_t", device_id=INA, channel=ch), "voltage_mV")
        ma = v(rd(link, "packet_sys_power_monitor_get_current_t", device_id=INA, channel=ch), "current_mA")
        out.append(f"{name}={mv} mV/{ma} mA")
    return "  ".join(out)


def dump_new(link, seen: list[int]) -> None:
    for _, l in link.logs[seen[0]:]:
        if l.strip() and "dec_sys_contracts" not in l:
            print("   LOG", l, flush=True)
    for _, e in link.errors[seen[1]:]:
        print("   ERR", e, flush=True)
    seen[0], seen[1] = len(link.logs), len(link.errors)


def main() -> int:
    port, cmd, args = sys.argv[1], sys.argv[2], sys.argv[3:]
    with open_link(port) as link:
        if hasattr(link, "mtu"):
            print(f"BLE connected, MTU {link.mtu}")
        time.sleep(0.5)
        seen = [len(link.logs), len(link.errors)]
        if cmd == "status":
            print("rails :", rails(link))
            print("AP VBUS:", rd(link, "packet_sys_power_monitor_get_voltage_t", device_id=AP, channel=0))
            print("PD lim:", rd(link, "packet_sys_power_usb_pd_get_limits_t", device_id=AP))
            pd = rd(link, "packet_sys_power_usb_pd_list_t", device_id=AP)
            if isinstance(pd, dict):
                for i in range(pd["count"] or 0):
                    print(f"  PD offer slot {pd['slot'][i]} type {pd['type'][i]}: {pd['min_mV'][i]}-{pd['max_mV'][i]} mV, {pd['max_mA'][i]} mA")
            print("power :", rd(link, "packet_sys_power_get_status_t"))
        elif cmd == "pd":
            mv = int(args[0])
            ma = int(args[1]) if len(args) > 1 else 3000
            print(f"PD request {mv} mV / {ma} mA:", rd(link, "packet_sys_power_usb_pd_set_t", device_id=AP, voltage_mV=mv, current_mA=ma))
            time.sleep(1.5)
            print("rails :", rails(link))
            print("power :", rd(link, "packet_sys_power_get_status_t"))
        elif cmd == "rail":
            dev, _ = RAILS[args[0].upper()]
            target = int(args[1])
            print(f"enable {args[0]}:", rd(link, "packet_sys_power_vreg_set_enable_t", device_id=dev, state=1), "|", rails(link))
            mv = 6000
            while mv <= target:
                print(f"  -> {mv} mV:", rd(link, "packet_sys_power_vreg_set_voltage_t", device_id=dev, voltage_mV=mv), "|", rails(link), flush=True)
                dump_new(link, seen)
                if mv == target:
                    break
                mv = min(mv + 1000, target)
        elif cmd == "off":
            dev, _ = RAILS[args[0].upper()]
            print(f"disable {args[0]}:", rd(link, "packet_sys_power_vreg_set_enable_t", device_id=dev, state=0))
        elif cmd == "watch":
            t0 = time.time()
            while time.time() - t0 < float(args[0]):
                print(f"t={time.time() - t0:6.1f}s  {rails(link)}", flush=True)
                dump_new(link, seen)
                time.sleep(0.5)
        dump_new(link, seen)
    return 0


if __name__ == "__main__":
    sys.exit(main())
