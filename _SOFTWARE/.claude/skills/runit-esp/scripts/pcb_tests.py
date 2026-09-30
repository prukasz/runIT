"""runIT PCB bring-up tests over the native USB command link (COM4), stage 1:
ESP GPIO, TCA6424A (1), ADS7128 (2), PCA9685 (3), AP33772S (13).

Board facts used: TCA pins 22 / 23 are status LEDs (push-pull, high = on);
ADS7128 channel 0 or 7 sits near 3 V.
Usage: python pcb_tests.py COM4|BLE [--reset] [-k NAME]   (BLE: a Python with bleak, no --reset)
"""
from __future__ import annotations

import sys
import time
import traceback

from runit_link import Link, open_link

GPIO, TCA, ADS, PCA, AP = 0, 1, 2, 3, 13
ADC, OUT_PP, INPUT = 7, 3, 0
fails = 0


def check(name: str, cond: bool, detail: str = "") -> None:
    global fails
    print(f"{'PASS' if cond else 'FAIL'}  {name}" + (f"   ({detail})" if detail else ""))
    if not cond:
        fails += 1


def note(s: str) -> None:
    print(f"      - {s}")


def ok(r) -> bool:
    return r is not None and r.ok


def st(r) -> str:
    if r is None:
        return "no response"
    if r.ok:
        return f"OK {r.fields}" if r.fields else "OK"
    return str(r.err)


def level(link, dev, pin):
    r = link.call("packet_sys_io_get_level_t", device_id=dev, pin=pin)
    return r.fields["level"] if ok(r) and r.fields else st(r)


def t_tca_leds(link: Link) -> None:
    for pin in (22, 23):
        for lv in (1, 0):
            r = link.call("packet_sys_io_set_level_t", device_id=TCA, pin=pin, level=lv)
            check(f"LED {pin} set {lv}", ok(r), st(r))
            check(f"LED {pin} reads {lv}", level(link, TCA, pin) == lv)
    print("      blinking LEDs 22 / 23 alternately ...")
    t0 = time.time()
    n = 0
    while time.time() - t0 < 4.0:
        a = n % 2
        link.call("packet_sys_io_set_level_t", device_id=TCA, pin=22, level=a)
        link.call("packet_sys_io_set_level_t", device_id=TCA, pin=23, level=1 - a)
        n += 1
        time.sleep(0.25)
    for _ in range(6):
        r = link.call("packet_sys_io_toggle_t", device_id=TCA, pin=22)
        time.sleep(0.15)
    check("toggle LED 22", ok(r), st(r))
    link.call("packet_sys_io_set_level_t", device_id=TCA, pin=22, level=0)
    link.call("packet_sys_io_set_level_t", device_id=TCA, pin=23, level=0)


def t_tca_freeze_sync(link: Link) -> None:
    """Freeze TCA, set LED 22 on (buffered), wait, sync: the LED lights only at sync."""
    link.call("packet_sys_io_set_level_t", device_id=TCA, pin=22, level=0)
    check("freeze TCA", ok(link.call("packet_sys_device_freeze_t", device_id=TCA)))
    r = link.call("packet_sys_io_set_level_t", device_id=TCA, pin=22, level=1)
    check("set LED 22 while frozen", ok(r), st(r))
    print("      LED 22 should still be OFF for 1.5 s (frozen) ...")
    time.sleep(1.5)
    r = link.call("packet_sys_device_sync_t", device_id=TCA)
    check("sync TCA", ok(r), st(r))
    r = link.call("packet_sys_device_resume_t", device_id=TCA)
    check("resume TCA", ok(r), st(r))
    print("      ... and ON now")
    time.sleep(1.0)
    check("LED 22 on after sync", level(link, TCA, 22) == 1)
    link.call("packet_sys_io_set_level_t", device_id=TCA, pin=22, level=0)


def t_tca_inputs(link: Link) -> None:
    levels = {}
    for pin in (3, 4, 7, 8):  # not used by the stage-1 devices
        link.call("packet_sys_io_reset_t", device_id=TCA, pin=pin)
        r = link.call("packet_sys_io_set_mode_t", device_id=TCA, pin=pin, mode=INPUT)
        levels[pin] = level(link, TCA, pin) if ok(r) else st(r)
    note(f"TCA free pins as inputs: {levels}")
    r = link.call("packet_sys_io_set_mode_t", device_id=TCA, pin=24, mode=INPUT)
    check("TCA pin 24 refused", not ok(r), st(r))
    r = link.call("packet_sys_io_set_mode_t", device_id=TCA, pin=22, mode=INPUT)
    note(f"re-mode LED pin 22 (in use) -> {st(r)}")
    link.call("packet_sys_io_reset_t", device_id=TCA, pin=22)  # put the LED back
    link.call("packet_sys_io_set_mode_t", device_id=TCA, pin=22, mode=OUT_PP)


def t_ads_voltages(link: Link) -> None:
    readings = {}
    for ch in range(8):
        link.call("packet_sys_io_reset_t", device_id=ADS, pin=ch)
        r = link.call("packet_sys_io_set_mode_t", device_id=ADS, pin=ch, mode=ADC)
        if not ok(r):
            readings[ch] = st(r)
    time.sleep(0.3)
    for ch in range(8):
        if ch in readings:
            continue
        vals = []
        for _ in range(3):
            r = link.call("packet_sys_io_get_voltage_t", device_id=ADS, pin=ch)
            vals.append(r.fields["voltage_mV"] if ok(r) and r.fields else st(r))
            time.sleep(0.05)
        readings[ch] = vals
    for ch, v in readings.items():
        note(f"ADS ch{ch}: {v}")
    near3v = [ch for ch in (0, 7) if isinstance(readings.get(ch), list) and all(isinstance(x, int) and 2500 <= x <= 3600 for x in readings[ch])]
    check("ADS ch0 or ch7 near 3 V", bool(near3v), f"channels near 3 V: {near3v}")


def t_ads_window_events(link: Link) -> None:
    """ADS7128 window alert -> GPIO42 -> IO event -> user action (toggle LED 23). ch7 sits near 3.2 V."""
    action = 12
    link.call("packet_sys_io_set_level_t", device_id=TCA, pin=23, level=0)
    link.request(0x03, 0x04, bytes([action]))
    link.request(0x03, 0x02, bytes([action]))
    link.call("packet_sys_io_toggle_t", device_id=TCA, pin=23)  # recording executes it (F-GAP-4)
    link.request(0x03, 0x03)
    link.request(0x09, 0x02, bytes([255]))
    r = link.call("packet_sys_event_subscribe_t", domain=0, device_id=ADS, channel=7, event=255,
                  route_mask=0, static_action_id=0, dynamic_action_id=action)
    check("subscribe ADS ch7 events", ok(r), st(r))
    link.call("packet_sys_io_reset_t", device_id=ADS, pin=7)
    link.call("packet_sys_io_set_mode_t", device_id=ADS, pin=7, mode=ADC)
    for name, mode, up, down, fires in (("INSIDE 3000..3400", 5, 3400, 3000, True),
                                        ("OUTSIDE 1000..2000", 4, 2000, 1000, True),
                                        ("INSIDE 1000..2000", 5, 2000, 1000, False),
                                        ("OUTSIDE 3000..3400", 4, 3400, 3000, False)):
        link.call("packet_sys_io_set_level_t", device_id=TCA, pin=23, level=0)
        r = link.call("packet_sys_io_configure_intr_t", device_id=ADS, pin=7, mode=mode, debounce=0,
                      adc_thresh_up_mV=up, adc_thresh_down_mV=down, adc_thresh_hyst_mV=20, adc_counter_thresh=1)
        time.sleep(0.8)
        lv = level(link, TCA, 23)
        check(f"{name}: {'fires once' if fires else 'stays quiet'}", lv == (1 if fires else 0), f"LED23={lv}")
        link.call("packet_sys_io_configure_intr_t", device_id=ADS, pin=7, mode=0)
    link.request(0x09, 0x02, bytes([255]))
    link.request(0x03, 0x04, bytes([action]))
    link.call("packet_sys_io_set_level_t", device_id=TCA, pin=23, level=0)


def t_pca(link: Link) -> None:
    r = link.call("packet_sys_io_set_pwm_frequency_t", device_id=PCA, pin=0, frequency_Hz=1000)
    note(f"PCA freq 1 kHz -> {st(r)}")
    for duty in (0, 1000, 2048, 4095, 4096):
        r = link.call("packet_sys_io_set_pwm_duty_t", device_id=PCA, pin=0, duty=duty)
        note(f"PCA ch0 duty {duty} -> {st(r)}")
    r = link.call("packet_sys_io_set_level_t", device_id=PCA, pin=1, level=1)
    check("PCA ch1 level 1", ok(r), st(r))
    note(f"PCA ch1 read back -> {level(link, PCA, 1)}")
    r = link.call("packet_sys_io_toggle_t", device_id=PCA, pin=1)
    note(f"PCA ch1 toggle -> {st(r)}, level {level(link, PCA, 1)}")
    r = link.call("packet_sys_io_set_level_t", device_id=PCA, pin=16, level=1)
    check("PCA ch16 refused", not ok(r), st(r))
    note(f"TCA pin 0 (PCA /OE) level -> {level(link, TCA, 0)}")
    for ch in (0, 1):
        link.call("packet_sys_io_set_level_t", device_id=PCA, pin=ch, level=0)


def t_ap33772s(link: Link) -> None:
    r = link.call("packet_sys_power_usb_pd_get_limits_t", device_id=AP)
    note(f"PD limits -> {st(r)}")
    r = link.call("packet_sys_power_usb_pd_list_t", device_id=AP)
    note(f"PD list -> {st(r)}")
    r = link.call("packet_sys_power_monitor_get_voltage_t", device_id=AP, channel=0)
    v = r.fields.get("voltage_mV") if ok(r) and r.fields else None
    note(f"AP VBUS voltage -> {st(r)}")
    check("AP33772S voltage read", v is not None and v > 3000, f"{v} mV")
    r = link.call("packet_sys_power_monitor_get_current_t", device_id=AP, channel=0)
    note(f"AP current -> {st(r)}")
    r = link.call("packet_sys_power_get_status_t")
    note(f"power status -> {st(r)}")


def t_devices_errors(link: Link) -> None:
    r = link.call("packet_sys_device_uninstall_t", device_id=TCA)
    check("uninstall onboard TCA refused", not ok(r), st(r))
    r = link.call("packet_sys_io_get_level_t", device_id=4, pin=0)
    note(f"TPS55289 (not installed) -> {st(r)}")


TESTS = [v for k, v in list(globals().items()) if k.startswith("t_") and callable(v)]


def main() -> int:
    only = sys.argv[sys.argv.index("-k") + 1] if "-k" in sys.argv else None
    with open_link(sys.argv[1], reset="--reset" in sys.argv) as link:
        time.sleep(0.3)
        for t in TESTS:
            if only and only not in t.__name__:
                continue
            print(f"--- {t.__name__[2:]}")
            m = link.mark()
            try:
                t(link)
            except Exception:
                check(t.__name__, False, traceback.format_exc().strip().splitlines()[-1])
            for e in link.errors_since(m):
                print(f"      ! {e}")
        crash = [l for _, l in link.logs if "Guru" in l or "abort" in l.lower()]
        check("no crash", not crash, str(crash))
    print(f"\n{fails} failure(s)")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
