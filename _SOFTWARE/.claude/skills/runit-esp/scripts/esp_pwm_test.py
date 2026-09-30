"""ESP native GPIO PWM (LEDC) pool rules, over any link. No scope needed: it checks
what the firmware accepts and refuses, and resets every pin it used.

    8 channels: a 9th pin set to PWM is refused (ERR_IO_PWM_CHANNELS_EXHAUSTED)
    4 timers:   a 5th distinct frequency is refused (ERR_IO_PWM_TIMERS_EXHAUSTED),
                a pin joining a running frequency is not, a pin alone on its timer
                retunes it, a released pin frees its timer
    ranges:     frequency 5..5000000 Hz, duty 0..4096; duty before frequency binds 1 kHz

Usage (BLE: a Python with bleak):
    python esp_pwm_test.py COM4|BLE <9 free ESP GPIO pins, comma-separated> [-v]   (-v prints the board log)
Pick pins nothing drives or listens to: every one is set to PWM and driven.
Assumes the default Kconfig masks (all 8 channels, all 4 timers for PWM pins).
"""
from __future__ import annotations

import sys

from runit_link import open_link

DEV, PWM = 0, 6
fails = 0


def check(name: str, cond: bool, detail: str = "") -> None:
    global fails
    print(f"{'PASS' if cond else 'FAIL'}  {name}" + (f"   ({detail})" if detail else ""))
    if not cond:
        fails += 1


def st(r) -> str:
    return "no response" if r is None else ("OK" if r.ok else str(r.err))


def main() -> int:
    port, pins = sys.argv[1], [int(p) for p in sys.argv[2].split(",")]
    if len(pins) != 9:
        print("need exactly 9 pins")
        return 2
    with open_link(port, echo_logs="-v" in sys.argv) as link:
        mode = lambda p: link.call("packet_sys_io_set_mode_t", device_id=DEV, pin=p, mode=PWM)
        freq = lambda p, f: link.call("packet_sys_io_set_pwm_frequency_t", device_id=DEV, pin=p, frequency_Hz=f)
        duty = lambda p, d: link.call("packet_sys_io_set_pwm_duty_t", device_id=DEV, pin=p, duty=d)
        reset = lambda p: link.call("packet_sys_io_reset_t", device_id=DEV, pin=p)
        refused = lambda r, tag: r is not None and not r.ok and r.err[0] == tag
        try:
            for p in pins:
                reset(p)
            for p in pins[:8]:
                r = mode(p)
                check(f"pin {p} -> PWM", r is not None and r.ok, st(r))
            r = mode(pins[8])
            check("9th PWM pin refused: channels exhausted", refused(r, "ERR_IO_PWM_CHANNELS_EXHAUSTED"), st(r))

            a, b, c, d, e, f, g, h = pins[:8]
            r = duty(a, 2048)
            check("duty before frequency binds the default timer", r is not None and r.ok, st(r))
            for p, hz in ((b, 50), (c, 20000), (d, 100000)):
                r = freq(p, hz)
                check(f"pin {p} at {hz} Hz (new timer)", r is not None and r.ok, st(r))
            r = freq(e, 7777)
            check("5th frequency refused: timers exhausted", refused(r, "ERR_IO_PWM_TIMERS_EXHAUSTED"), st(r))
            for p, hz in ((e, 50), (f, 20000), (g, 1000)):
                r = freq(p, hz)
                check(f"pin {p} joins the {hz} Hz timer", r is not None and r.ok, st(r))
            r = freq(d, 7777)
            check("pin alone on its timer retunes it", r is not None and r.ok, st(r))
            r = freq(h, 7777)
            check("another pin joins the retuned timer", r is not None and r.ok, st(r))
            r = freq(c, 3333)
            check("pin sharing its timer can't take a 5th frequency", refused(r, "ERR_IO_PWM_TIMERS_EXHAUSTED"), st(r))
            reset(d)
            reset(h)
            r = freq(c, 3333)
            check("released timer is free again", r is not None and r.ok, st(r))
            r = mode(pins[8])
            check("released channel is free again", r is not None and r.ok, st(r))

            for name, r, tag in (("frequency 4 Hz", freq(a, 4), "ERR_INVALID_VAL_UI32"),
                                 ("frequency 5000001 Hz", freq(a, 5000001), "ERR_INVALID_VAL_UI32"),
                                 ("duty 4097", duty(a, 4097), "ERR_INVALID_VAL_UI32")):
                check(f"{name} refused", refused(r, tag), st(r))
            for dv in (0, 1, 4095, 4096):
                r = duty(a, dv)
                check(f"duty {dv} accepted", r is not None and r.ok, st(r))
        finally:
            for p in pins:
                reset(p)
        for _, e in link.errors:
            if "PWM" in str(e) or "INVALID_VAL" in str(e):
                continue
            print(f"      ! {e}")
    print(f"\n{fails} failure(s)")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
