"""Hardware tests on a bare ESP32-S3 devkit (only the ESP GPIO device, CONFIG_RUNIT_SKIP_DEVICE_INIT=y).

Runs over the console UART command link (runit_link.py). Usage:
    python devkit_tests.py COM3 [--reset] [-k NAME_SUBSTRING] [-v]

Pins used: 4, 5, 6, 7 (digital), 1 (ADC1_CH0). Nothing needs to be wired.
"""
from __future__ import annotations

import sys
import time
import traceback

from runit_link import Link, PACKETS, tag_name

DEV = 0  # DEVICE_ID_GPIO_ESP
IN_PULLUP, IN_PULLDOWN, OUT_PP, OUT_OD, OUT_OD_PU, PWM, ADC = 1, 2, 3, 4, 5, 6, 7
INTR_BOTH = 3

results: list[tuple[str, bool, str]] = []
VERBOSE = "-v" in sys.argv


class Check:
    def __init__(self, name: str):
        self.name = name
        self.fails: list[str] = []
        self.notes: list[str] = []

    def eq(self, what: str, got, want) -> None:
        if got != want:
            self.fails.append(f"{what}: got {got!r}, want {want!r}")
        elif VERBOSE:
            self.notes.append(f"{what} = {got!r}")

    def true(self, what: str, cond: bool, detail: str = "") -> None:
        if not cond:
            self.fails.append(f"{what} {detail}".strip())

    def note(self, s: str) -> None:
        self.notes.append(s)


def ok(r) -> bool:
    return r is not None and r.ok


def err_tag(r) -> str | None:
    if r is None:
        return "<no response>"
    return None if r.ok else (r.err[0] if r.err else "<bad error data>")


def level(link: Link, pin: int):
    r = link.call("packet_sys_io_get_level_t", device_id=DEV, pin=pin)
    return r.fields["level"] if ok(r) and r.fields else err_tag(r)


def reset_pins(link: Link, *pins: int) -> None:
    for p in pins:
        link.call("packet_sys_io_reset_t", device_id=DEV, pin=p)


# ---------------------------------------------------------------------------
# Interface / link
# ---------------------------------------------------------------------------

def t_link_seq_echo(link: Link, c: Check) -> None:
    for seq in (0, 1, 0x7F, 0xFF):
        r = link.request(0x01, 0x20, bytes([DEV, 4]), seq=seq)  # io reset
        c.true(f"response for seq {seq}", r is not None)
        if r:
            c.eq(f"seq {seq} echo", (r.seq, r.cls, r.packet), (seq, 0x01, 0x20))


def t_link_short_frames(link: Link, c: Check) -> None:
    link.send_raw(bytes([0x31]))  # seq only
    r = link.wait_response(0x31)
    c.eq("seq-only frame", err_tag(r), "ERR_INTERFACE_SHORT_FRAME")
    if r:
        c.eq("seq-only echo", (r.cls, r.packet), (0, 0))
    r = link.request(0x01, None)
    c.eq("class without packet", err_tag(r), "ERR_INTERFACE_SHORT_FRAME")
    r = link.call("packet_sys_io_set_level_t", device_id=DEV, pin=4, level=1) if False else link.request(0x01, 0x22, bytes([DEV]))
    c.eq("truncated payload", err_tag(r), "ERR_INTERFACE_SHORT_FRAME")


def t_link_unknown(link: Link, c: Check) -> None:
    r = link.request(0x7E, 0x01)
    c.true("unknown class answered", r is not None)
    c.note(f"unknown class -> {err_tag(r)}")
    r = link.request(0x01, 0xEE)
    c.true("unknown packet answered", r is not None)
    c.note(f"unknown packet -> {err_tag(r)}")


def t_link_burst(link: Link, c: Check) -> None:
    """20 commands back to back: each answered exactly once, in order."""
    seqs = [0x80 + i for i in range(20)]
    for s in seqs:
        link.send_raw(bytes([s, 0x01, 0x20, DEV, 4]))
    got = []
    for s in seqs:
        r = link.wait_response(s, 3.0)
        got.append(r.seq if r else None)
    c.eq("burst responses", got, seqs)


def t_link_bad_lines(link: Link, c: Check) -> None:
    m = link.mark()
    link.send_line("#R:0")           # odd digit count
    link.send_line("#R:ZZ01")        # not hex
    link.send_line("#R:" + "00" * 600)  # longer than the frame limit
    link.send_line("hello, not a frame")  # ignored
    time.sleep(0.6)
    tags = [n.tag for e in link.errors_since(m) for n in e.nodes]
    bad = [t for t in tags if tag_name(t) == "ERR_UART_LINE_BAD"]
    # Identical chains within CONFIG_SYS_ERRORS_REPEAT_WINDOW_MS are sent once and counted.
    c.eq("ERR_UART_LINE_BAD sent once", len(bad), 1)
    repeated = link.wait_log("ERR_UART_LINE_BAD repeated 2 more", 6.0)
    c.true("repeat count logged", repeated)
    r = link.request(0x01, 0x20, bytes([DEV, 4]))
    c.true("link still works", ok(r), str(r))


# ---------------------------------------------------------------------------
# GPIO device
# ---------------------------------------------------------------------------

def t_gpio_input_pulls(link: Link, c: Check) -> None:
    reset_pins(link, 4)
    c.true("pullup mode", ok(link.call("packet_sys_io_set_mode_t", device_id=DEV, pin=4, mode=IN_PULLUP)))
    time.sleep(0.02)
    c.eq("pullup level", level(link, 4), 1)
    reset_pins(link, 4)
    c.true("pulldown mode", ok(link.call("packet_sys_io_set_mode_t", device_id=DEV, pin=4, mode=IN_PULLDOWN)))
    time.sleep(0.02)
    c.eq("pulldown level", level(link, 4), 0)
    reset_pins(link, 4)


def t_gpio_output(link: Link, c: Check) -> None:
    reset_pins(link, 5)
    c.true("output mode", ok(link.call("packet_sys_io_set_mode_t", device_id=DEV, pin=5, mode=OUT_PP)))
    for lv in (1, 0, 1):
        c.true(f"set {lv}", ok(link.call("packet_sys_io_set_level_t", device_id=DEV, pin=5, level=lv)))
        c.eq(f"read back {lv}", level(link, 5), lv)
    c.true("toggle", ok(link.call("packet_sys_io_toggle_t", device_id=DEV, pin=5)))
    c.eq("after toggle", level(link, 5), 0)
    reset_pins(link, 5)


def t_gpio_open_drain(link: Link, c: Check) -> None:
    reset_pins(link, 6)
    r = link.call("packet_sys_io_set_mode_t", device_id=DEV, pin=6, mode=OUT_OD)
    c.true("open-drain mode", ok(r), str(r))
    link.call("packet_sys_io_set_level_t", device_id=DEV, pin=6, level=0)
    c.eq("OD low", level(link, 6), 0)
    reset_pins(link, 6)
    r = link.call("packet_sys_io_set_mode_t", device_id=DEV, pin=6, mode=OUT_OD_PU)
    c.note(f"open-drain+pullup mode -> {err_tag(r) or 'OK'}")
    if ok(r):
        r2 = link.call("packet_sys_io_set_level_t", device_id=DEV, pin=6, level=1)
        c.true("OD+pullup set_level accepted", ok(r2), str(r2))
        c.eq("OD+pullup released reads high", level(link, 6), 1)
    reset_pins(link, 6)


def t_gpio_errors(link: Link, c: Check) -> None:
    reset_pins(link, 4)
    c.eq("get level on unconfigured pin", err_tag(link.call("packet_sys_io_get_level_t", device_id=DEV, pin=4)) is not None, True)
    c.note(f"unconfigured pin -> {err_tag(link.call('packet_sys_io_get_level_t', device_id=DEV, pin=4))}")
    link.call("packet_sys_io_set_mode_t", device_id=DEV, pin=4, mode=IN_PULLUP)
    c.eq("set_level on input", err_tag(link.call("packet_sys_io_set_level_t", device_id=DEV, pin=4, level=1)) is not None, True)
    c.note(f"mode on configured pin -> {err_tag(link.call('packet_sys_io_set_mode_t', device_id=DEV, pin=4, mode=OUT_PP))}")
    c.note(f"invalid pin 99 -> {err_tag(link.call('packet_sys_io_set_mode_t', device_id=DEV, pin=99, mode=OUT_PP))}")
    c.note(f"invalid mode 42 -> {err_tag(link.call('packet_sys_io_set_mode_t', device_id=DEV, pin=7, mode=42))}")
    c.note(f"unknown device 77 -> {err_tag(link.call('packet_sys_io_get_level_t', device_id=77, pin=4))}")
    c.note(f"DAC mode -> {err_tag(link.call('packet_sys_io_set_mode_t', device_id=DEV, pin=7, mode=8))}")
    reset_pins(link, 4, 7)


def t_gpio_adc(link: Link, c: Check) -> None:
    reset_pins(link, 1)
    r = link.call("packet_sys_io_set_mode_t", device_id=DEV, pin=1, mode=ADC)
    c.true("ADC mode on GPIO1", ok(r), str(r))
    time.sleep(0.3)
    readings = []
    for _ in range(3):
        v = link.call("packet_sys_io_get_voltage_t", device_id=DEV, pin=1)
        readings.append(v.fields["voltage_mV"] if ok(v) and v.fields else err_tag(v))
        time.sleep(0.1)
    c.true("ADC readings numeric", all(isinstance(x, int) for x in readings), str(readings))
    c.note(f"GPIO1 floating mV: {readings}")
    c.note(f"ADC on GPIO4 (ADC1_CH3) -> {err_tag(link.call('packet_sys_io_set_mode_t', device_id=DEV, pin=4, mode=ADC)) or 'OK'}")
    c.note(f"ADC on GPIO11 (ADC2) -> {err_tag(link.call('packet_sys_io_set_mode_t', device_id=DEV, pin=11, mode=ADC)) or 'OK'}")
    reset_pins(link, 1, 4, 11)


def t_gpio_pwm(link: Link, c: Check) -> None:
    reset_pins(link, 7)
    r = link.call("packet_sys_io_set_mode_t", device_id=DEV, pin=7, mode=PWM)
    c.note(f"PWM mode -> {err_tag(r) or 'OK'}")
    if ok(r):
        c.note(f"freq 1 kHz -> {err_tag(link.call('packet_sys_io_set_pwm_frequency_t', device_id=DEV, pin=7, frequency_Hz=1000)) or 'OK'}")
        c.note(f"duty 500 -> {err_tag(link.call('packet_sys_io_set_pwm_duty_t', device_id=DEV, pin=7, duty=500)) or 'OK'}")
    reset_pins(link, 7)


# ---------------------------------------------------------------------------
# Device lifecycle
# ---------------------------------------------------------------------------

def t_device_freeze_sync(link: Link, c: Check) -> None:
    reset_pins(link, 5)
    link.call("packet_sys_io_set_mode_t", device_id=DEV, pin=5, mode=OUT_PP)
    link.call("packet_sys_io_set_level_t", device_id=DEV, pin=5, level=0)
    c.true("freeze", ok(link.call("packet_sys_device_freeze_t", device_id=DEV)))
    c.true("set while frozen", ok(link.call("packet_sys_io_set_level_t", device_id=DEV, pin=5, level=1)))
    c.eq("frozen read (cached)", level(link, 5), 1)
    c.note("pin state while frozen not observable without a second reader")
    r = link.call("packet_sys_device_sync_t", device_id=DEV)
    c.true("sync", ok(r), str(r))
    r = link.call("packet_sys_device_resume_t", device_id=DEV)
    c.true("resume", ok(r), str(r))
    c.eq("after resume (hardware)", level(link, 5), 1)
    reset_pins(link, 5)


def t_device_suspend(link: Link, c: Check) -> None:
    reset_pins(link, 5)
    link.call("packet_sys_io_set_mode_t", device_id=DEV, pin=5, mode=OUT_PP)
    link.call("packet_sys_io_set_level_t", device_id=DEV, pin=5, level=1)
    r = link.call("packet_sys_device_suspend_t", device_id=DEV)
    c.true("suspend", ok(r), str(r))
    r = link.call("packet_sys_io_set_level_t", device_id=DEV, pin=5, level=0)
    c.note(f"set_level while suspended -> {err_tag(r) or 'OK'}")
    c.true("set_level refused while suspended", not ok(r))
    r = link.call("packet_sys_device_resume_t", device_id=DEV)
    c.true("resume", ok(r), str(r))
    r = link.call("packet_sys_io_set_level_t", device_id=DEV, pin=5, level=0)
    c.note(f"set_level after resume -> {err_tag(r) or 'OK'}")
    c.note(f"level after resume -> {level(link, 5)}")
    reset_pins(link, 5)


def t_device_onboard_protected(link: Link, c: Check) -> None:
    r = link.call("packet_sys_device_uninstall_t", device_id=DEV)
    c.true("uninstall onboard refused", not ok(r), str(r))
    c.note(f"uninstall onboard -> {err_tag(r)}")
    r = link.call("packet_sys_io_reset_t", device_id=DEV, pin=4)
    c.true("device still there", ok(r), str(r))


def t_device_install_duplicate(link: Link, c: Check) -> None:
    r = link.request(0x01, 0x40, bytes([DEV]))
    c.true("install on taken ID refused", not ok(r), str(r))
    c.note(f"install GPIO again on ID 0 -> {err_tag(r)}")


# ---------------------------------------------------------------------------
# Actions, events
# ---------------------------------------------------------------------------

ACTION_ID = 10


def t_actions_record_replay(link: Link, c: Check) -> None:
    reset_pins(link, 6)
    link.call("packet_sys_io_set_mode_t", device_id=DEV, pin=6, mode=OUT_PP)
    link.call("packet_sys_io_set_level_t", device_id=DEV, pin=6, level=0)
    link.request(0x03, 0x04, bytes([ACTION_ID]))  # remove leftovers
    r = link.request(0x03, 0x02, bytes([ACTION_ID]))
    c.true("record start", ok(r), str(r))
    r = link.call("packet_sys_io_toggle_t", device_id=DEV, pin=6)
    c.true("toggle while recording answered", ok(r), str(r))
    lv_rec = level(link, 6)
    c.note(f"level after toggle while recording = {lv_rec} (1 = recording executes frames, F-GAP-4)")
    r = link.request(0x03, 0x03)
    c.true("record stop", ok(r), str(r))
    before = level(link, 6)
    r = link.request(0x03, 0x01, bytes([ACTION_ID]))
    c.true("invoke", ok(r), str(r))
    time.sleep(0.1)
    after = level(link, 6)
    c.true("replay toggled pin", before != after and isinstance(after, int), f"before={before} after={after}")
    r = link.request(0x03, 0x01, bytes([99]))
    c.eq("invoke missing action", err_tag(r), "ERR_ACTION_NOT_FOUND")


def t_events_io_to_action(link: Link, c: Check) -> None:
    """Pin 5 edge -> IO event -> user action ACTION_ID (toggles pin 6)."""
    reset_pins(link, 5)
    link.call("packet_sys_io_set_mode_t", device_id=DEV, pin=5, mode=OUT_PP)
    link.call("packet_sys_io_set_level_t", device_id=DEV, pin=5, level=0)
    r = link.call("packet_sys_io_configure_intr_t", device_id=DEV, pin=5, mode=INTR_BOTH)
    c.true("arm pin 5 interrupt", ok(r), str(r))
    link.request(0x09, 0x02, bytes([255]))  # clear user subscriptions
    r = link.call("packet_sys_event_subscribe_t", domain=0, device_id=DEV, channel=5, event=255, route_mask=0,
                  static_action_id=0, dynamic_action_id=ACTION_ID)
    c.true("subscribe", ok(r), str(r))
    sub_id = r.fields.get("subscription_id") if ok(r) and r.fields else None
    c.note(f"subscription id {sub_id}")
    before = level(link, 6)
    link.call("packet_sys_io_set_level_t", device_id=DEV, pin=5, level=1)
    time.sleep(0.3)
    after = level(link, 6)
    c.true("edge ran the action (pin 6 toggled)", isinstance(after, int) and before != after, f"before={before} after={after}")
    link.call("packet_sys_io_set_level_t", device_id=DEV, pin=5, level=0)
    time.sleep(0.3)
    c.true("second edge toggled back", level(link, 6) == before, f"pin6={level(link, 6)}")
    if sub_id is not None:
        c.true("unsubscribe", ok(link.request(0x09, 0x02, bytes([sub_id]))))
    link.call("packet_sys_io_set_level_t", device_id=DEV, pin=5, level=1)
    time.sleep(0.3)
    c.eq("no action after unsubscribe", level(link, 6), before)
    link.call("packet_sys_io_configure_intr_t", device_id=DEV, pin=5, mode=0)
    link.request(0x03, 0x04, bytes([ACTION_ID]))
    reset_pins(link, 5, 6)


# ---------------------------------------------------------------------------
# Settings, connectors, power
# ---------------------------------------------------------------------------

def t_connector_protection(link: Link, c: Check) -> None:
    r = link.call("packet_settings_data_connector_remove_t", id=3)
    c.eq("remove interface connector", err_tag(r), "ERR_DATA_CONNECTOR_PROTECTED")
    r = link.call("packet_settings_data_connector_suspend_t", id=3)
    c.eq("suspend interface connector", err_tag(r), "ERR_DATA_CONNECTOR_PROTECTED")
    r = link.call("packet_settings_data_connector_create_t", id=1, header=0x44, max_packet_len=0, name="x")
    c.eq("re-create errors connector", err_tag(r), "ERR_DATA_CONNECTOR_PROTECTED")
    r = link.call("packet_settings_data_connector_tx_remove_t", connector_id=1, provider_id=2)
    c.note(f"unbind errors<-UART (BLE still bound) -> {err_tag(r) or 'OK'}")
    if ok(r):
        r = link.call("packet_settings_data_connector_tx_add_t", connector_id=1, provider_id=2, provider_param=0)
        c.true("rebind errors->UART", ok(r), str(r))


def t_connector_user(link: Link, c: Check) -> None:
    link.call("packet_settings_data_connector_remove_t", id=5)
    r = link.call("packet_settings_data_connector_create_t", id=5, header=0x77, max_packet_len=0, name="user5")
    c.true("create user connector 5", ok(r), str(r))
    r = link.call("packet_settings_data_connector_tx_add_t", connector_id=5, provider_id=2, provider_param=0)
    c.true("bind TX to UART", ok(r), str(r))
    r = link.call("packet_settings_data_connector_tx_add_t", connector_id=5, provider_id=2, provider_param=7)
    c.note(f"bind TX to UART endpoint 7 -> {err_tag(r) or 'OK'}")
    r = link.call("packet_settings_data_connector_tx_add_t", connector_id=5, provider_id=9, provider_param=0)
    c.eq("bind unknown provider", err_tag(r), "ERR_DATA_CONNECTOR_NO_PROVIDER")
    r = link.call("packet_settings_data_connector_remove_t", id=5)
    c.true("remove user connector", ok(r), str(r))


def t_power_status(link: Link, c: Check) -> None:
    r = link.call("packet_sys_power_get_status_t")
    c.true("power status answered", r is not None)
    c.note(f"power status -> {r}")


def t_logs_settings(link: Link, c: Check) -> None:
    desc = PACKETS["packet_settings_logs_set_t"]
    c.note(f"logs_set fields: {desc['field_order']}")


TESTS = [v for k, v in list(globals().items()) if k.startswith("t_") and callable(v)]


def main() -> int:
    port = sys.argv[1]
    only = sys.argv[sys.argv.index("-k") + 1] if "-k" in sys.argv else None
    with Link(port, reset="--reset" in sys.argv) as link:
        time.sleep(0.3)
        for t in TESTS:
            name = t.__name__[2:]
            if only and only not in name:
                continue
            c = Check(name)
            m = link.mark()
            try:
                t(link, c)
            except Exception:
                c.fails.append("exception: " + traceback.format_exc().strip().splitlines()[-1])
            passed = not c.fails
            results.append((name, passed, "; ".join(c.fails)))
            print(f"{'PASS' if passed else 'FAIL'}  {name}")
            for f in c.fails:
                print(f"        x {f}")
            for n in c.notes:
                print(f"        - {n}")
            errs = link.errors_since(m)
            if VERBOSE and errs:
                for e in errs:
                    print(f"        ! {e}")
        crit = [l for _, l in link.logs if "Guru" in l or "abort" in l.lower() or "rst:" in l]
        if crit:
            print("CRASH/RESET LINES:")
            for l in crit:
                print("   ", l)
    failed = [r for r in results if not r[1]]
    print(f"\n{len(results) - len(failed)}/{len(results)} passed")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
