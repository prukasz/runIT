"""Tests of the ported devices on the real runIT PCB (every chip connected, HARDWARE.md section 3).

  python board_tests.py --port COM9

Creates the onboard chips over the router (the board config creates none yet), runs checks on DAC53202, ADS7128, servo
and the two DRV8962, and removes everything again. The H-bridge motor supplies (the power muxes) are never switched on:
no load is driven. Needs a freshly booted board (device IDs 0..6 and 20 free).
"""

from __future__ import annotations

import argparse
import struct
import sys
import time

import router_tests as rt
from router_link import KEY_SYS, Link
from router_tests import (
    GPIO_ID,
    GPIO_TYPE,
    IO_CONFIGURE_INTR,
    IO_GET_LEVEL,
    IO_GET_VOLTAGE,
    IO_RESET,
    IO_SET_MODE,
    IO_SET_VOLTAGE,
    OP_CREATE,
    OP_INFO,
    OP_OPS,
    OP_REMOVE,
    OP_RESET,
    OP_RESUME,
    OP_SUSPEND,
    PCA_TYPE,
    PINS_LOCKED,
    check,
    dev,
    err_cause,
    wait_boot,
)

TCA_TYPE, DAC_TYPE, ADS_TYPE, SERVO_TYPE, DRV_TYPE = 0x42, 0x46, 0x47, 0x48, 0x49
TCA_ID, ADS_ID, PCA_ID, DAC_ID, DRV1_ID, DRV2_ID, SERVO_ID = 1, 2, 3, 4, 5, 6, 20
NONE = struct.pack("<HB", 0xFFFF, 0xFF)

SERVO_HOME, SERVO_SET_TRIM, SERVO_SET, SERVO_GET_PULSE, SERVO_RELEASE = 0xFF00, 0xFF01, 0xFF02, 0xFF03, 0xFF04
DRV_SET_LIMIT, DRV_CLEAR_FAULT, DRV_GET_FAULT = 0xFF00, 0xFF01, 0xFF02
HB_SET_DRIVE, HB_BRAKE, HB_COAST, HB_GET_CURRENT = 0x0900, 0x0901, 0x0902, 0x0903


def steps(permille: int) -> int:
    """A drive given in thousandths as the contract's PWM steps (-4095..4095, 12 bits)."""
    return round(permille * 4095 / 1000)



def pin(device: int, p: int) -> bytes:
    return struct.pack("<HB", device, p)


def create(link: Link, dev_id: int, type_id: int, cfg: bytes):
    return dev(link, dev_id, OP_CREATE, struct.pack("<H", type_id) + cfg)


def remove(link: Link, dev_id: int):
    r = dev(link, dev_id, OP_REMOVE)
    check(f"REMOVE {dev_id}", r is not None and r.ok, str(r))


def i32(r) -> int | None:
    return struct.unpack("<i", r.data)[0] if r is not None and r.ok and len(r.data) == 4 else None


def u64(r) -> int | None:
    return struct.unpack("<Q", r.data)[0] if r is not None and r.ok and len(r.data) == 8 else None


def new_errors(link: Link, t: float) -> list[str]:
    return [str(rec) for rec in link.error_records_since(t)]


# ---------------------------------------------------------------------------


BOOT_DEVICES: set[int] = set()  # created by the firmware at boot (CONFIG_RUNIT_ONBOARD_DEVICES): the tests keep them


def exists(link: Link, dev_id: int) -> bool:
    r = dev(link, dev_id, OP_INFO)
    return r is not None and r.ok


def test_boot(link: Link) -> None:
    """What the firmware creates by itself: the ESP GPIO, the TCA6424A, and the status LED lit."""
    for dev_id, type_id, name in ((GPIO_ID, GPIO_TYPE, "GPIO ESP"), (TCA_ID, TCA_TYPE, "TCA6424A")):
        r = dev(link, dev_id, OP_INFO)
        if r is not None and r.ok:
            BOOT_DEVICES.add(dev_id)
        check(f"boot created {name} {dev_id}", r is not None and r.ok and r.data == struct.pack("<HB", type_id, 0), str(r))
    if TCA_ID in BOOT_DEVICES:
        check("boot lit the status LED (TCA pin 23 high)", tca_level(link, 23) == 1, str(tca_level(link, 23)))
        locked_mask = locked(link, TCA_ID) or 0
        check("the LED pin is a plain output (not locked)", not locked_mask & (1 << 23), hex(locked_mask))
        levels = {tca_level(link, 22) for _ in range(20)}
        check("pin 22 is an output at a steady level (a floating input toggles and floods INT)", levels == {0}, str(levels))
        t = link.mark()
        time.sleep(1.0)
        floods = [e for e in new_errors(link, t) if "QUEUE_FULL" in e]
        check("no event storm at idle (no queue-full reports)", floods == [], f"{len(floods)} reports")


def setup_base(link: Link) -> bool:
    """GPIO ESP, TCA6424A (INT ESP 9, RST ESP 8), PCA9685 (OE TCA 0): what the other chips stand on. The first two
    are created by the boot; made here only when it did not."""
    ok = True
    if GPIO_ID not in BOOT_DEVICES:
        r = dev(link, GPIO_ID, OP_CREATE, struct.pack("<H", GPIO_TYPE))
        ok = check("CREATE GPIO ESP 0", r is not None and r.ok, str(r))
    if TCA_ID not in BOOT_DEVICES:
        r = create(link, TCA_ID, TCA_TYPE, bytes([0, 0x23]) + pin(GPIO_ID, 9) + pin(GPIO_ID, 8))
        ok &= check("CREATE TCA6424A 1 (0x23, INT ESP 9, RST ESP 8)", r is not None and r.ok, str(r))
    r = create(link, PCA_ID, PCA_TYPE, bytes([0, 0x60]) + pin(TCA_ID, 0))
    ok &= check("CREATE PCA9685 3 (0x60, OE TCA 0)", r is not None and r.ok, str(r))
    return ok


def test_dac(link: Link) -> None:
    r = create(link, DAC_ID, DAC_TYPE, bytes([0, 0x47]))
    check("CREATE DAC53202 with address 0x47 -> ERR_DEV_RANGE", err_cause(r) == "ERR_DEV_RANGE", str(r))
    r = create(link, DAC_ID, DAC_TYPE, bytes([0, 0x48]))
    if not check("CREATE DAC53202 4 (0x48)", r is not None and r.ok, str(r)):
        return
    r = dev(link, DAC_ID, OP_INFO)
    check("INFO: type 0x0046", r is not None and r.ok and r.data == struct.pack("<HB", DAC_TYPE, 0), str(r))
    r = dev(link, DAC_ID, OP_OPS)
    ops = set(struct.unpack(f"<{len(r.data) // 2}H", r.data)) if r is not None and r.ok else set()
    check("OPS: IO RESET / GET_VOLTAGE / SET_VOLTAGE, no SET_LEVEL", {IO_RESET, IO_GET_VOLTAGE, IO_SET_VOLTAGE} <= ops and 0x0100 not in ops, hex(0) if not ops else str(sorted(map(hex, ops))))

    def volts(ch: int) -> int | None:
        return i32(dev(link, DAC_ID, IO_GET_VOLTAGE, bytes([ch])))

    check("both channels read 0 after create (Hi-Z)", volts(0) == 0 and volts(1) == 0, f"{volts(0)} {volts(1)}")
    for ch, mv in ((0, 1000), (1, 2500), (0, 3300), (0, 0)):
        r = dev(link, DAC_ID, IO_SET_VOLTAGE, struct.pack("<Bi", ch, mv))
        got = volts(ch)
        check(f"SET_VOLTAGE ch{ch} {mv} mV -> reads back", r is not None and r.ok and got is not None and abs(got - mv) <= 4, f"{r} {got}")
    r = dev(link, DAC_ID, IO_SET_VOLTAGE, struct.pack("<Bi", 0, 3301))
    check("3301 mV -> ERR_DEV_RANGE", err_cause(r) == "ERR_DEV_RANGE", str(r))
    r = dev(link, DAC_ID, IO_SET_VOLTAGE, struct.pack("<Bi", 0, -1))
    check("-1 mV -> ERR_DEV_RANGE", err_cause(r) == "ERR_DEV_RANGE", str(r))
    r = dev(link, DAC_ID, IO_SET_VOLTAGE, struct.pack("<Bi", 2, 100))
    check("channel 2 -> ERR_DEV_BAD_PIN", err_cause(r) == "ERR_DEV_BAD_PIN", str(r))
    dev(link, DAC_ID, IO_SET_VOLTAGE, struct.pack("<Bi", 1, 1200))
    r = dev(link, DAC_ID, OP_SUSPEND)
    check("SUSPEND", r is not None and r.ok, str(r))
    r = dev(link, DAC_ID, IO_SET_VOLTAGE, struct.pack("<Bi", 1, 500))
    check("SET_VOLTAGE while suspended -> ERR_DEV_SUSPENDED", err_cause(r) == "ERR_DEV_SUSPENDED", str(r))
    check("GET_VOLTAGE while suspended still answers", volts(1) is not None and abs(volts(1) - 1200) <= 4, str(volts(1)))
    r = dev(link, DAC_ID, OP_RESUME)
    check("RESUME", r is not None and r.ok, str(r))
    check("RESUME keeps the voltage", abs((volts(1) or 0) - 1200) <= 4, str(volts(1)))
    r = dev(link, DAC_ID, IO_RESET, bytes([1]))
    check("IO RESET ch1 -> reads 0", r is not None and r.ok and volts(1) == 0, str(r))
    r = dev(link, DAC_ID, OP_RESET)
    check("device RESET", r is not None and r.ok, str(r))


def test_ads(link: Link) -> None:
    r = create(link, ADS_ID, ADS_TYPE, bytes([0, 0x10]) + pin(GPIO_ID, 42) + struct.pack("<H", 0))
    check("CREATE ADS7128 with full scale 0 -> ERR_DEV_RANGE", err_cause(r) == "ERR_DEV_RANGE", str(r))
    r = create(link, ADS_ID, ADS_TYPE, bytes([0, 0x10]) + pin(GPIO_ID, 42) + struct.pack("<H", 20000))
    if not check("CREATE ADS7128 2 (0x10, ALERT ESP 42, full scale 20000)", r is not None and r.ok, str(r)):
        return
    r = dev(link, ADS_ID, OP_INFO)
    check("INFO: type 0x0047", r is not None and r.ok and r.data == struct.pack("<HB", ADS_TYPE, 0), str(r))

    def volts(ch: int) -> int | None:
        return i32(dev(link, ADS_ID, IO_GET_VOLTAGE, bytes([ch])))

    readings = [volts(ch) for ch in range(8)]
    check("8 channels read 0..20000 mV", all(v is not None and 0 <= v <= 20000 for v in readings), str(readings))
    print("      readings (mV):", readings)
    r = dev(link, ADS_ID, IO_GET_VOLTAGE, bytes([8]))
    check("channel 8 -> ERR_DEV_BAD_PIN", err_cause(r) == "ERR_DEV_BAD_PIN", str(r))
    r = dev(link, ADS_ID, IO_SET_MODE, bytes([0, 7]))
    check("SET_MODE ADC is accepted", r is not None and r.ok, str(r))
    r = dev(link, ADS_ID, IO_SET_MODE, bytes([0, 0]))
    check("SET_MODE INPUT -> ERR_IO_MODE_UNSUPPORTED", err_cause(r) == "ERR_IO_MODE_UNSUPPORTED", str(r))

    cfg = lambda ch, mode, up, down, hyst=0, count=0: bytes([ch]) + rt.intr_cfg(mode, 0, up, down, hyst, count)
    r = dev(link, ADS_ID, IO_CONFIGURE_INTR, cfg(0, 2, 0, 0))
    check("CONFIGURE_INTR falling edge -> ERR_IO_FEATURE_UNSUPPORTED", err_cause(r) == "ERR_IO_FEATURE_UNSUPPORTED", str(r))
    r = dev(link, ADS_ID, IO_CONFIGURE_INTR, cfg(0, 9, 0, 0))
    check("CONFIGURE_INTR mode 9 -> ERR_CMD_BAD_ARG", err_cause(r) == "ERR_CMD_BAD_ARG", str(r))
    r = dev(link, ADS_ID, IO_CONFIGURE_INTR, cfg(0, 4, 21000, 0))
    check("threshold above full scale -> ERR_DEV_RANGE", err_cause(r) == "ERR_DEV_RANGE", str(r))
    r = dev(link, ADS_ID, IO_CONFIGURE_INTR, cfg(0, 4, 5000, 6000))
    check("lower threshold above the upper one -> ERR_DEV_RANGE", err_cause(r) == "ERR_DEV_RANGE", str(r))
    r = dev(link, ADS_ID, IO_CONFIGURE_INTR, cfg(0, 4, 10000, 1000, 100, 17))
    check("event count 17 -> ERR_DEV_RANGE", err_cause(r) == "ERR_DEV_RANGE", str(r))

    # A window the idle input is outside of: the chip raises ALERT, the device must handle it without an error.
    ch = 0
    time.sleep(0.5)  # the error records of the refused calls above are still on their way
    t1 = link.mark()
    r = dev(link, ADS_ID, IO_CONFIGURE_INTR, cfg(ch, 4, 19000, 18000, 100, 2))
    check("CONFIGURE_INTR window 18..19 V (outside)", r is not None and r.ok, str(r))
    time.sleep(1.0)
    check("autonomous conversions: reads still work", volts(ch) is not None and volts(1) is not None, f"{volts(ch)} {volts(1)}")
    r = dev(link, GPIO_ID, IO_GET_LEVEL, bytes([42]))
    check("ALERT line is released after the handler (ESP 42 high)", r is not None and r.ok and r.data == b"\x01", str(r))
    check("no error records while the alert ran", new_errors(link, t1) == [], str(new_errors(link, t1)))
    r = dev(link, ADS_ID, IO_CONFIGURE_INTR, cfg(ch, 5, 20000, 0))
    check("CONFIGURE_INTR window 0..20 V (inside)", r is not None and r.ok, str(r))
    time.sleep(1.0)
    r = dev(link, GPIO_ID, IO_GET_LEVEL, bytes([42]))
    check("inside window: ALERT released again", r is not None and r.ok and r.data == b"\x01", str(r))
    check("no error records after the inside alert", new_errors(link, t1) == [], str(new_errors(link, t1)))
    r = dev(link, ADS_ID, IO_CONFIGURE_INTR, cfg(ch, 0, 0, 0))
    check("CONFIGURE_INTR disable", r is not None and r.ok, str(r))
    r = dev(link, ADS_ID, OP_SUSPEND)
    check("SUSPEND", r is not None and r.ok, str(r))
    r = dev(link, ADS_ID, IO_GET_VOLTAGE, bytes([0]))
    check("GET_VOLTAGE while suspended -> ERR_DEV_SUSPENDED", err_cause(r) == "ERR_DEV_SUSPENDED", str(r))
    r = dev(link, ADS_ID, OP_RESUME)
    check("RESUME", r is not None and r.ok and volts(0) is not None, str(r))
    r = dev(link, ADS_ID, IO_RESET, bytes([0]))
    check("IO RESET ch0", r is not None and r.ok, str(r))
    r = dev(link, ADS_ID, OP_RESET)
    check("device RESET", r is not None and r.ok, str(r))


def servo_cfg(pwm: bytes, kind: int, byte_mode: int, invert: int, amin: int, amax: int, default: int, min_us: int, max_us: int, dead: int) -> bytes:
    return pwm + struct.pack("<BBBhhhHHH", kind, byte_mode, invert, amin, amax, default, min_us, max_us, dead)


def test_servo(link: Link) -> None:
    pwm = pin(PCA_ID, 0)
    r = create(link, SERVO_ID, SERVO_TYPE, servo_cfg(pwm, 0, 0, 0, 0, 180, 200, 500, 2500, 0))
    check("CREATE servo with default 200 deg -> ERR_DEV_RANGE", err_cause(r) == "ERR_DEV_RANGE", str(r))
    r = create(link, SERVO_ID, SERVO_TYPE, servo_cfg(pwm, 0, 0, 0, 0, 180, 90, 2500, 500, 0))
    check("CREATE servo with min_us above max_us -> ERR_DEV_RANGE", err_cause(r) == "ERR_DEV_RANGE", str(r))
    r = create(link, SERVO_ID, SERVO_TYPE, servo_cfg(NONE, 0, 0, 0, 0, 180, 90, 500, 2500, 0))
    check("CREATE servo without a pin -> ERR_DEV_BAD_PIN", err_cause(r) == "ERR_DEV_BAD_PIN", str(r))
    r = create(link, SERVO_ID, SERVO_TYPE, servo_cfg(pwm, 0, 0, 0, 0, 180, 90, 500, 2500, 0))
    if not check("CREATE angle servo on PCA CH 0 (0..180, 500..2500 us, default 90)", r is not None and r.ok, str(r)):
        return

    def pulse() -> int | None:
        r = dev(link, SERVO_ID, SERVO_GET_PULSE)
        return struct.unpack("<H", r.data)[0] if r is not None and r.ok and len(r.data) == 2 else None

    check("at create the servo is at 90 deg: 1500 us", pulse() == 1500, str(pulse()))
    for x10, want in ((0, 500), (1800, 2500), (450, 1000), (900, 1500)):
        r = dev(link, SERVO_ID, SERVO_SET, struct.pack("<h", x10))
        check(f"SET {x10 / 10} deg -> {want} us", r is not None and r.ok and pulse() == want, f"{r} {pulse()}")
    r = dev(link, SERVO_ID, SERVO_SET, struct.pack("<h", 1801))
    check("SET 180.1 deg -> ERR_DEV_RANGE", err_cause(r) == "ERR_DEV_RANGE", str(r))
    r = dev(link, SERVO_ID, SERVO_SET_TRIM, struct.pack("<h", 501))
    check("trim 501 -> ERR_DEV_RANGE", err_cause(r) == "ERR_DEV_RANGE", str(r))
    dev(link, SERVO_ID, SERVO_SET_TRIM, struct.pack("<h", 10))
    r = dev(link, SERVO_ID, SERVO_HOME)
    check("trim 10 deg then HOME -> 1611 us", r is not None and r.ok and pulse() == 1611, f"{r} {pulse()}")
    dev(link, SERVO_ID, SERVO_SET_TRIM, struct.pack("<h", 0))
    r = dev(link, SERVO_ID, OP_SUSPEND)
    check("SUSPEND (no pulse)", r is not None and r.ok and pulse() == 0, f"{r} {pulse()}")
    r = dev(link, SERVO_ID, SERVO_SET, struct.pack("<h", 900))
    check("SET while suspended -> ERR_DEV_SUSPENDED", err_cause(r) == "ERR_DEV_SUSPENDED", str(r))
    r = dev(link, SERVO_ID, OP_RESUME)
    check("RESUME gives the last pulse back (1611 us after HOME with trim)", r is not None and r.ok and pulse() == 1611, f"{r} {pulse()}")
    r = dev(link, SERVO_ID, SERVO_RELEASE)
    check("RELEASE: pulse 0", r is not None and r.ok and pulse() == 0, f"{r} {pulse()}")
    r = dev(link, SERVO_ID, SERVO_HOME)
    check("HOME drives it again", r is not None and r.ok and pulse() == 1500, f"{r} {pulse()}")
    remove(link, SERVO_ID)

    # Continuous servo: stop in the middle, speed outwards from beyond the deadband
    r = create(link, SERVO_ID, SERVO_TYPE, servo_cfg(pwm, 1, 0, 0, 0, 0, 0, 1000, 2000, 50))
    check("CREATE continuous servo (1000..2000 us, deadband 50)", r is not None and r.ok, str(r))
    check("stopped at create: 1500 us", pulse() == 1500, str(pulse()))
    for x10, want in ((500, 1775), (-500, 1225), (1000, 2000), (-1000, 1000), (0, 1500)):
        r = dev(link, SERVO_ID, SERVO_SET, struct.pack("<h", x10))
        check(f"SET speed {x10 / 10} % -> {want} us", r is not None and r.ok and pulse() == want, f"{r} {pulse()}")
    r = dev(link, SERVO_ID, SERVO_SET, struct.pack("<h", 1001))
    check("speed 100.1 % -> ERR_DEV_RANGE", err_cause(r) == "ERR_DEV_RANGE", str(r))
    remove(link, SERVO_ID)
    r = dev(link, SERVO_ID, OP_INFO)
    check("after REMOVE the servo is gone", err_cause(r) == "ERR_DEV_NOT_FOUND", str(r))


def drv_cfg(topology: int, pins: list[bytes], pwm_hz: int, limits: list[int]) -> bytes:
    assert len(pins) == 15
    return bytes([topology]) + b"".join(pins) + struct.pack("<I4I4IB", pwm_hz, 0, 0, 0, 0, *limits, 1)


def locked(link: Link, dev_id: int) -> int | None:
    return u64(dev(link, dev_id, PINS_LOCKED))


def test_drv2(link: Link) -> None:
    """DRV8962 #2: IN1-4 = PCA CH 8-11, EN1-4 = PCA CH 12-15, nSLEEP TCA 8, nFAULT TCA 10, VREF = DAC OUT1."""
    pins = [pin(PCA_ID, 8 + i) for i in range(4)] + [pin(PCA_ID, 12 + i) for i in range(4)] + [NONE] * 4
    pins += [pin(TCA_ID, 8), pin(TCA_ID, 10), pin(DAC_ID, 1)]
    r = create(link, DRV2_ID, DRV_TYPE, drv_cfg(0, pins, 20000, [2000] * 4))
    check("CREATE DRV8962 #2 at 20 kHz on the PCA9685 -> fails (PCA tops at 1526 Hz)", r is not None and not r.ok, str(r))
    r = dev(link, DRV2_ID, OP_INFO)
    check("the failed create left nothing registered", err_cause(r) == "ERR_DEV_NOT_FOUND", str(r))
    check("and nothing locked on the PCA9685", locked(link, PCA_ID) == 0, str(locked(link, PCA_ID)))
    r = create(link, DRV2_ID, DRV_TYPE, drv_cfg(2, pins, 1000, [2000] * 4))
    check("CREATE with topology 2 -> ERR_DEV_RANGE", err_cause(r) == "ERR_DEV_RANGE", str(r))
    time.sleep(0.5)
    t0 = link.mark()
    r = create(link, DRV2_ID, DRV_TYPE, drv_cfg(0, pins, 1000, [2000] * 4))
    if not check("CREATE DRV8962 #2 (2 full bridges, 1 kHz, 2 A)", r is not None and r.ok, str(r)):
        return
    r = dev(link, DRV2_ID, OP_OPS)
    ops = set(struct.unpack(f"<{len(r.data) // 2}H", r.data)) if r is not None and r.ok else set()
    check("OPS: the H-bridge ops and the own ops", {HB_SET_DRIVE, HB_BRAKE, HB_COAST, HB_GET_CURRENT, DRV_SET_LIMIT, DRV_CLEAR_FAULT, DRV_GET_FAULT, 0x0300} <= ops, str(sorted(map(hex, ops))))
    check("PCA locks EN1-4 (CH 12-15)", locked(link, PCA_ID) == (0xF << 12), hex(locked(link, PCA_ID) or 0))
    check("TCA locks nSLEEP (8) and nFAULT (10) next to INT / RST", (locked(link, TCA_ID) or 0) & ((1 << 8) | (1 << 10)) == ((1 << 8) | (1 << 10)), hex(locked(link, TCA_ID) or 0))
    r = dev(link, TCA_ID, IO_GET_LEVEL, bytes([8]))
    check("nSLEEP high (TCA 8): the chip is awake", r is not None and r.ok and r.data == b"\x01", str(r))
    r = dev(link, TCA_ID, IO_GET_LEVEL, bytes([10]))
    print("      nFAULT (TCA 10) level:", r.data.hex() if r is not None and r.ok else r)
    r = dev(link, DAC_ID, IO_GET_VOLTAGE, bytes([1]))
    mv = i32(r)
    want = 2000 * 212 * 3090 // 1_000_000
    check(f"VREF (DAC OUT1) follows the 2 A limit: {want} mV", mv is not None and abs(mv - want) <= 5, str(mv))
    r = dev(link, PCA_ID, 0x0100, bytes([12, 0]))
    check("IO write to the locked EN1 channel -> ERR_DEV_PIN_LOCKED", err_cause(r) == "ERR_DEV_PIN_LOCKED", str(r))

    r = dev(link, DRV2_ID, DRV_GET_FAULT, bytes([0]))
    check("GET_FAULT ch0: no fault", r is not None and r.ok and r.data == b"\x00\x00", str(r))
    for drive in (500, -500, 1000, -1000, 4, 0):
        r = dev(link, DRV2_ID, HB_SET_DRIVE, struct.pack("<Bh", 0, steps(drive)))
        check(f"SET_DRIVE ch0 {drive / 10} %", r is not None and r.ok, str(r))
    def pca(ch: int) -> int | None:
        r = dev(link, PCA_ID, IO_GET_LEVEL, bytes([ch]))
        return r.data[0] if r is not None and r.ok and len(r.data) == 1 else None

    def lines() -> tuple:  # IN1 IN2 IN3 IN4 EN1 EN2 EN3 EN4 (PCA CH 8..15)
        return tuple(pca(c) for c in range(8, 16))

    check("after create every channel is braked: EN1-4 on, IN off", lines() == (0, 0, 0, 0, 1, 1, 1, 1), str(lines()))
    dev(link, DRV2_ID, HB_COAST, bytes([0]))
    dev(link, DRV2_ID, HB_COAST, bytes([1]))
    for name, drive, want in (
        ("forward 80 %: IN1 on, IN2 the complement (off at this level), EN1+2 on", 800, (1, 0, 0, 0, 1, 1, 0, 0)),
        ("reverse 80 %: IN1 the complement, IN2 on", -800, (0, 1, 0, 0, 1, 1, 0, 0)),
        ("0: brake, EN stays on", 0, (0, 0, 0, 0, 1, 1, 0, 0)),
    ):
        dev(link, DRV2_ID, HB_SET_DRIVE, struct.pack("<Bh", 0, steps(drive)))
        check(f"channel 0 {name}", lines() == want, str(lines()))
    dev(link, DRV2_ID, HB_SET_DRIVE, struct.pack("<Bh", 1, steps(800)))
    check("channel 1 forward 80 %: IN3 on, EN3+4 on", lines() == (0, 0, 1, 0, 1, 1, 1, 1), str(lines()))
    dev(link, DRV2_ID, HB_COAST, bytes([0]))
    dev(link, DRV2_ID, HB_COAST, bytes([1]))
    check("COAST both channels: IN off, EN off", lines() == (0,) * 8, str(lines()))
    dev(link, DRV2_ID, HB_BRAKE, bytes([1]))
    check("BRAKE channel 1: EN3+4 on, IN off", lines() == (0, 0, 0, 0, 0, 0, 1, 1), str(lines()))
    dev(link, DRV2_ID, HB_COAST, bytes([1]))
    r = dev(link, DRV2_ID, HB_SET_DRIVE, struct.pack("<Bh", 1, 4096))
    check("SET_DRIVE 4096 (one step above full scale) -> ERR_DEV_RANGE", err_cause(r) == "ERR_DEV_RANGE", str(r))
    r = dev(link, DRV2_ID, HB_SET_DRIVE, struct.pack("<Bh", 2, 100))
    check("channel 2 -> ERR_DEV_BAD_PIN", err_cause(r) == "ERR_DEV_BAD_PIN", str(r))
    for name, op in (("BRAKE", HB_BRAKE), ("COAST", HB_COAST)):
        r = dev(link, DRV2_ID, op, bytes([1]))
        check(f"{name} ch1", r is not None and r.ok, str(r))
    r = dev(link, DRV2_ID, HB_GET_CURRENT, bytes([0]))
    check("GET_CURRENT without an IPROPI pin: 0 mA", i32(r) == 0, str(r))
    r = dev(link, DRV2_ID, DRV_SET_LIMIT, struct.pack("<BI", 0, 10001))
    check("limit 10001 mA -> ERR_DEV_RANGE", err_cause(r) == "ERR_DEV_RANGE", str(r))
    r = dev(link, DRV2_ID, DRV_SET_LIMIT, struct.pack("<BI", 0, 0))
    r2 = dev(link, DRV2_ID, DRV_SET_LIMIT, struct.pack("<BI", 1, 1000))
    mv = i32(dev(link, DAC_ID, IO_GET_VOLTAGE, bytes([1])))
    want = 1000 * 212 * 3090 // 1_000_000
    check(f"limits 0 / 1 A: VREF follows the largest ({want} mV)", r.ok and r2.ok and mv is not None and abs(mv - want) <= 5, f"{r} {r2} {mv}")
    r = dev(link, DRV2_ID, DRV_CLEAR_FAULT, bytes([0]))
    check("CLEAR_FAULT (nSLEEP reset pulse)", r is not None and r.ok, str(r))
    r = dev(link, TCA_ID, IO_GET_LEVEL, bytes([8]))
    check("nSLEEP high again after the pulse", r is not None and r.ok and r.data == b"\x01", str(r))

    r = dev(link, DRV2_ID, OP_SUSPEND)
    check("SUSPEND", r is not None and r.ok, str(r))
    r = dev(link, DRV2_ID, HB_SET_DRIVE, struct.pack("<Bh", 0, 100))
    check("SET_DRIVE while suspended -> ERR_DEV_SUSPENDED", err_cause(r) == "ERR_DEV_SUSPENDED", str(r))
    r = dev(link, DRV2_ID, OP_RESUME)
    check("RESUME", r is not None and r.ok, str(r))
    r = dev(link, DRV2_ID, OP_RESET)
    check("device RESET (every channel braked)", r is not None and r.ok, str(r))
    r = dev(link, DAC_ID, OP_REMOVE)
    check("REMOVE the DAC while the bridge uses it -> ERR_DEV_IN_USE", err_cause(r) == "ERR_DEV_IN_USE", str(r))
    remove(link, DRV2_ID)
    check("REMOVE released the locks (PCA)", locked(link, PCA_ID) == 0, hex(locked(link, PCA_ID) or 0))
    check("and TCA nSLEEP / nFAULT", (locked(link, TCA_ID) or 0) & ((1 << 8) | (1 << 10)) == 0, hex(locked(link, TCA_ID) or 0))


def test_drv1(link: Link) -> None:
    """DRV8962 #1: IN1-4 = ESP 21/47/48/45 (PWM), EN1-4 = ESP 38/39/2/1, IPROPI1-4 = ESP 7/6/5/4 (ADC), nSLEEP TCA 9, nFAULT TCA 11, VREF = DAC OUT0."""
    esp = lambda p: pin(GPIO_ID, p)
    pins = [esp(p) for p in (21, 47, 48, 45)] + [esp(p) for p in (38, 39, 2, 1)] + [esp(p) for p in (7, 6, 5, 4)]
    pins += [pin(TCA_ID, 9), pin(TCA_ID, 11), pin(DAC_ID, 0)]
    time.sleep(0.5)
    t0 = link.mark()
    r = create(link, DRV1_ID, DRV_TYPE, drv_cfg(0, pins, 0, [2000, 2000, 0, 0]))
    if not check("CREATE DRV8962 #1 (ESP PWM / EN / ADC pins, 20 kHz, 2 A)", r is not None and r.ok, str(r)):
        return
    mask = u64(dev(link, GPIO_ID, PINS_LOCKED)) or 0
    check("GPIO ESP locks EN1-4 (38 39 2 1) and the TCA INT / RST pins", all(mask & (1 << p) for p in (38, 39, 2, 1)), hex(mask))
    for drive in (300, -300, 0):
        r = dev(link, DRV1_ID, HB_SET_DRIVE, struct.pack("<Bh", 0, steps(drive)))
        check(f"SET_DRIVE ch0 {drive / 10} %", r is not None and r.ok, str(r))
    currents = [i32(dev(link, DRV1_ID, HB_GET_CURRENT, bytes([ch]))) for ch in (0, 1)]
    check("GET_CURRENT of both channels reads (IPROPI ADC)", all(c is not None for c in currents), str(currents))
    print("      currents (mA):", currents)
    r = dev(link, DRV1_ID, OP_SUSPEND)
    check("SUSPEND", r is not None and r.ok, str(r))
    r = dev(link, DRV1_ID, OP_RESUME)
    check("RESUME", r is not None and r.ok, str(r))
    check("no error records in the DRV8962 #1 section", new_errors(link, t0) == [], str(new_errors(link, t0)))
    remove(link, DRV1_ID)
    mask = u64(dev(link, GPIO_ID, PINS_LOCKED)) or 0
    check("REMOVE released EN1-4", not any(mask & (1 << p) for p in (38, 39, 2, 1)), hex(mask))


INA_TYPE, AP_TYPE, TPS_TYPE, MUX_TYPE = 0x44, 0x45, 0x43, 0x4A
INA_ID, AP_ID, TPS_A_ID, TPS_B_ID, MUX1_ID, MUX2_ID = 12, 13, 10, 11, 7, 8
PMON_GET_VOLTAGE, PMON_GET_CURRENT, PMON_GET_POWER = 0x0400, 0x0401, 0x0403
PD_LIST_OPTIONS, PD_GET_LIMITS = 0x0701, 0x0702
SUPPLY_GET_SOURCE = 0x0801
TPS_SET_PROTECTION, TPS_GET_PROTECTION = 0xFF01, 0xFF02
MUX_PINS = {MUX1_ID: (14, 15), MUX2_ID: (12, 13)}  # TCA pins (A = board input, B = rail) of the LM73100 pairs; high = on


def tca_level(link: Link, p: int) -> int | None:
    r = dev(link, TCA_ID, IO_GET_LEVEL, bytes([p]))
    return r.data[0] if r is not None and r.ok and len(r.data) == 1 else None


def test_tca(link: Link) -> None:
    """The TCA6424A's own IO on the board: the status LEDs on 22 / 23 (push-pull, high = on)."""
    r = dev(link, TCA_ID, OP_INFO)
    check("TCA INFO: type 0x0042", r is not None and r.ok and r.data == struct.pack("<HB", TCA_TYPE, 0), str(r))
    r = dev(link, TCA_ID, IO_SET_MODE, bytes([23, 3]))
    check("SET_MODE LED pin 23 push-pull", r is not None and r.ok, str(r))
    for level in (1, 0, 1, 0):
        r = dev(link, TCA_ID, 0x0100, bytes([23, level]))
        check(f"SET_LEVEL LED 23 = {level} reads back", r is not None and r.ok and tca_level(link, 23) == level, f"{r} {tca_level(link, 23)}")
    r = dev(link, TCA_ID, IO_SET_MODE, bytes([23, 4]))
    check("SET_MODE open-drain -> ERR_IO_MODE_UNSUPPORTED", err_cause(r) == "ERR_IO_MODE_UNSUPPORTED", str(r))
    r = dev(link, TCA_ID, IO_SET_MODE, bytes([24, 0]))
    check("pin 24 -> ERR_DEV_BAD_PIN", err_cause(r) == "ERR_DEV_BAD_PIN", str(r))
    r = dev(link, TCA_ID, IO_RESET, bytes([23]))
    check("IO RESET LED 23 (back to an input)", r is not None and r.ok, str(r))
    dev(link, TCA_ID, IO_SET_MODE, bytes([23, 3]))
    r = dev(link, TCA_ID, 0x0100, bytes([23, 1]))
    check("status LED 23 lit again", r is not None and r.ok and tca_level(link, 23) == 1, str(r))


def test_power_chips(link: Link) -> None:
    """TPS55289 x2, INA3221, AP33772S and the two power muxes on the board. Nothing is switched on: the rails stay
    disabled (EN low) and the muxes keep both LM73100 switches off."""
    r = create(link, INA_ID, INA_TYPE, bytes([0, 0x40]) + pin(TCA_ID, 6) + pin(TCA_ID, 5) + bytes([0x07]))
    check("CREATE INA3221 12 (0x40, CRIT TCA 6, WARN TCA 5, shunts reversed)", r is not None and r.ok, str(r))
    for name, dev_id, addr, intr, en in (("TPS55289 rail A", TPS_A_ID, 0x74, 1, 17), ("TPS55289 rail B", TPS_B_ID, 0x75, 2, 16)):
        r = create(link, dev_id, TPS_TYPE, bytes([0, addr]) + pin(TCA_ID, intr) + pin(TCA_ID, en))
        check(f"CREATE {name} {dev_id} (0x{addr:02X}, INT TCA {intr}, EN TCA {en})", r is not None and r.ok, str(r))
    r = create(link, AP_ID, AP_TYPE, bytes([0, 0x52]) + pin(TCA_ID, 21))
    check("CREATE AP33772S 13 (0x52, INT TCA 21)", r is not None and r.ok, str(r))
    for mux_id, (a, b) in MUX_PINS.items():
        r = create(link, mux_id, MUX_TYPE, pin(TCA_ID, a) + pin(TCA_ID, b) + bytes([1, 5]))
        check(f"CREATE power mux {mux_id} (A TCA {a}, B TCA {b})", r is not None and r.ok, str(r))
        r = dev(link, mux_id, SUPPLY_GET_SOURCE)
        check(f"mux {mux_id}: source OFF, both switches low", r is not None and r.ok and r.data == b"\x00" and tca_level(link, a) == 0 and tca_level(link, b) == 0, f"{r} {tca_level(link, a)} {tca_level(link, b)}")

    mask = locked(link, TCA_ID) or 0
    want = sum(1 << p for p in (1, 2, 5, 6, 12, 13, 14, 15, 16, 17, 21))
    check("TCA locks the INT / EN / alert / switch pins of those devices", mask & want == want, hex(mask))
    check("rail EN pins are low: the rails are off", tca_level(link, 16) == 0 and tca_level(link, 17) == 0, f"{tca_level(link, 16)} {tca_level(link, 17)}")

    volts = [i32(dev(link, INA_ID, PMON_GET_VOLTAGE, bytes([ch]))) for ch in range(3)]
    amps = [i32(dev(link, INA_ID, PMON_GET_CURRENT, bytes([ch]))) for ch in range(3)]
    print("      INA3221 channels 0..2 (rail B, input, rail A): mV", volts, "mA", amps)
    check("INA3221 reads the three channels", all(v is not None for v in volts + amps), f"{volts} {amps}")
    check("board input (channel 1) between 3 V and 21 V", volts[1] is not None and 3000 <= volts[1] <= 21000, str(volts[1]))
    r = dev(link, INA_ID, PMON_GET_VOLTAGE, bytes([3]))
    check("INA3221 channel 3 -> an error", r is not None and not r.ok, str(r))

    for dev_id in (TPS_A_ID, TPS_B_ID):
        r = dev(link, dev_id, TPS_GET_PROTECTION)
        check(f"TPS {dev_id}: default protection (0, hiccup, all faults, no discharge)", r is not None and r.ok and r.data == bytes([0, 1, 7, 0]), str(r))
        r = dev(link, dev_id, TPS_SET_PROTECTION, bytes([2, 0, 3, 1]))
        r2 = dev(link, dev_id, TPS_GET_PROTECTION)
        check(f"TPS {dev_id}: SET_PROTECTION kept while the rail is off", r is not None and r.ok and r2.data == bytes([2, 0, 3, 1]), f"{r} {r2}")
        r = dev(link, dev_id, TPS_SET_PROTECTION, bytes([4, 0, 3, 0]))
        check(f"TPS {dev_id}: ocp_delay 4 -> ERR_DEV_RANGE", err_cause(r) == "ERR_DEV_RANGE", str(r))
        r = dev(link, dev_id, OP_RESET)
        r2 = dev(link, dev_id, TPS_GET_PROTECTION)
        check(f"TPS {dev_id}: RESET restores the default protection", r is not None and r.ok and r2.data == bytes([0, 1, 7, 0]), f"{r} {r2}")

    r = dev(link, AP_ID, PD_LIST_OPTIONS)
    check("AP33772S LIST_OPTIONS answers", r is not None and r.ok, str(r))
    print("      AP33772S options:", r.data.hex(" ") if r is not None and r.ok else r)
    r = dev(link, AP_ID, PD_GET_LIMITS)
    check("AP33772S GET_LIMITS answers", r is not None and r.ok, str(r))
    print("      AP33772S limits:", r.data.hex(" ") if r is not None and r.ok else r)

    for dev_id in (MUX1_ID, MUX2_ID, AP_ID, TPS_B_ID, TPS_A_ID, INA_ID):
        remove(link, dev_id)
    mask = locked(link, TCA_ID) or 0
    check("REMOVE released those TCA pins", mask & want == 0, hex(mask))


def test_safe_state(link: Link) -> None:
    """STOP (the safe state, sys_system) suspends every device, newest first; each one is resumed, oldest first."""
    r = create(link, SERVO_ID, SERVO_TYPE, servo_cfg(pin(PCA_ID, 0), 0, 0, 0, 0, 180, 90, 500, 2500, 0))
    check("safe state: servo created", r is not None and r.ok, str(r))
    pins = [pin(PCA_ID, 8 + i) for i in range(4)] + [pin(PCA_ID, 12 + i) for i in range(4)] + [NONE] * 4
    pins += [pin(TCA_ID, 8), pin(TCA_ID, 10), pin(DAC_ID, 1)]
    r = create(link, DRV2_ID, DRV_TYPE, drv_cfg(0, pins, 1000, [2000] * 4))
    check("safe state: DRV8962 #2 created", r is not None and r.ok, str(r))
    dev(link, DAC_ID, IO_SET_VOLTAGE, struct.pack("<Bi", 0, 1500))
    dev(link, DRV2_ID, HB_SET_DRIVE, struct.pack("<Bh", 0, 600))
    order = [GPIO_ID, TCA_ID, PCA_ID, DAC_ID, DRV2_ID, SERVO_ID]  # creation order
    if exists(link, ADS_ID):
        order.insert(4, ADS_ID)

    def suspended(dev_id: int) -> int | None:
        r = dev(link, dev_id, OP_INFO)
        return r.data[2] if r is not None and r.ok and len(r.data) == 3 else None

    check("all devices running before STOP", all(suspended(d) == 0 for d in order), str([suspended(d) for d in order]))
    t = link.mark()
    r = link.request(bytes([KEY_SYS, 0x02]), urgent=True)
    check("STOP answered", r is not None and r.ok, str(r))
    check("STOP runs the safe-state handler", link.wait_log("safe state", 1.0, since=t))
    time.sleep(0.3)
    check("every device is suspended", all(suspended(d) == 1 for d in order), str([suspended(d) for d in order]))
    r = dev(link, DRV2_ID, HB_SET_DRIVE, struct.pack("<Bh", 0, 100))
    check("the bridge refuses a drive", err_cause(r) == "ERR_DEV_SUSPENDED", str(r))
    lv = tuple((dev(link, PCA_ID, IO_GET_LEVEL, bytes([c])).data[0]) for c in range(8, 16))
    check("bridge lines all low (coasted before the PCA9685 slept)", lv == (0,) * 8, str(lv))
    r = dev(link, SERVO_ID, SERVO_GET_PULSE)
    check("servo: no pulse", r is not None and r.ok and r.data == b"\x00\x00", str(r))
    for d in order:
        r = dev(link, d, OP_RESUME)
        check(f"RESUME {d}", r is not None and r.ok, str(r))
    check("every device is running again", all(suspended(d) == 0 for d in order), str([suspended(d) for d in order]))
    check("DAC kept 1.5 V", abs((i32(dev(link, DAC_ID, IO_GET_VOLTAGE, bytes([0]))) or 0) - 1500) <= 4)
    check("the status LED is on again after the TCA6424A resumed", tca_level(link, 23) == 1, str(tca_level(link, 23)))
    r = dev(link, SERVO_ID, SERVO_GET_PULSE)
    check("servo: the last pulse is back (1500 us)", r is not None and r.ok and struct.unpack("<H", r.data)[0] == 1500, str(r))
    r = dev(link, DRV2_ID, HB_SET_DRIVE, struct.pack("<Bh", 0, 100))
    check("the bridge drives again", r is not None and r.ok, str(r))
    dev(link, DRV2_ID, HB_COAST, bytes([0]))
    for d in (DRV2_ID, SERVO_ID):
        remove(link, d)


def test_policy_and_cycles(link: Link) -> None:
    """The per-device error policy on a new device, and create / remove cycles that would leak a subscription
    (32 slots), a lock or a dependency."""
    r = dev(link, DAC_ID, 0x0008)
    check("DAC GET_ERROR_POLICY: LOW, no actions", r is not None and r.ok and r.data == struct.pack("<B4I", 1, 0, 0, 0, 0), str(r))
    r = dev(link, DAC_ID, 0x0007, struct.pack("<B4I", 3, 0, 0, 0, 0))
    r2 = dev(link, DAC_ID, 0x0008)
    check("DAC SET_ERROR_POLICY kept", r is not None and r.ok and r2.data == struct.pack("<B4I", 3, 0, 0, 0, 0), f"{r} {r2}")
    dev(link, DAC_ID, 0x0007, struct.pack("<B4I", 1, 0, 0, 0, 0))

    pins = [pin(PCA_ID, 8 + i) for i in range(4)] + [pin(PCA_ID, 12 + i) for i in range(4)] + [NONE] * 4
    pins += [pin(TCA_ID, 8), pin(TCA_ID, 10), pin(DAC_ID, 1)]
    bad = 0
    for i in range(40):
        r = create(link, DRV2_ID, DRV_TYPE, drv_cfg(0, pins, 1000, [2000] * 4))
        r2 = dev(link, DRV2_ID, OP_REMOVE)
        if r is None or not r.ok or r2 is None or not r2.ok:
            bad += 1
            print("      cycle", i, r, r2)
            break
    check("40 create / remove cycles of the DRV8962 #2 (subscription, locks, dependencies)", bad == 0)
    check("nothing stays locked on the PCA9685 / TCA after the cycles", locked(link, PCA_ID) == 0 and not (locked(link, TCA_ID) or 0) & ((1 << 8) | (1 << 10)),
          f"{locked(link, PCA_ID)} {hex(locked(link, TCA_ID) or 0)}")
    for i in range(40):
        r = create(link, SERVO_ID, SERVO_TYPE, servo_cfg(pin(PCA_ID, 0), 0, 0, 0, 0, 180, 90, 500, 2500, 0))
        r2 = dev(link, SERVO_ID, OP_REMOVE)
        if r is None or not r.ok or r2 is None or not r2.ok:
            bad += 1
            print("      cycle", i, r, r2)
            break
    check("40 create / remove cycles of the servo", bad == 0)


# Every onboard chip the firmware creates at boot: (id, type, name)
BOOT_CHIPS = [
    (0, 0x40, "GPIO ESP"), (1, 0x42, "TCA6424A"), (2, 0x47, "ADS7128"), (3, 0x41, "PCA9685"), (4, 0x46, "DAC53202"),
    (5, 0x49, "DRV8962 #1"), (6, 0x49, "DRV8962 #2"), (7, 0x4A, "power mux #1"), (8, 0x4A, "power mux #2"),
    (10, 0x43, "TPS55289 rail A"), (11, 0x43, "TPS55289 rail B"), (12, 0x44, "INA3221"), (13, 0x45, "AP33772S"),
]
VREG_SET_ENABLE, VREG_SET_VOLTAGE, VREG_SET_CURRENT = 0x0500, 0x0501, 0x0502
KEY_POWER_MGR = 0x0A
PW_GET_STATUS_OP = 7
INA_CH = {TPS_A_ID: 2, TPS_B_ID: 0}  # INA3221 channel of each rail (HARDWARE.md); the board input is channel 1


def power_status(link: Link) -> dict | None:
    r = link.request(bytes([KEY_POWER_MGR, PW_GET_STATUS_OP]))
    if r is None or not r.ok or len(r.data) != 31:
        return None
    f = struct.unpack("<BiiIIIIIBB", r.data)
    keys = ("source", "input_mV", "input_mA", "source_mV", "source_mA", "budget_mW", "limit_mW", "allocated_mW", "battery_pct", "flags")
    return dict(zip(keys, f))


def test_boot_chips(link: Link, label: str = "boot") -> None:
    """Every onboard chip exists with its type, nothing is energised, the lines have their idle levels."""
    for dev_id, type_id, name in BOOT_CHIPS:
        r = dev(link, dev_id, OP_INFO)
        check(f"{label}: {name} (id {dev_id}) is there, type {type_id:#04x}, running",
              r is not None and r.ok and r.data == struct.pack("<HB", type_id, 0), str(r))
    check(f"{label}: status LED (TCA 23) on", tca_level(link, 23) == 1, str(tca_level(link, 23)))
    for mux_id in (MUX1_ID, MUX2_ID):
        r = dev(link, mux_id, SUPPLY_GET_SOURCE)
        a, b = MUX_PINS[mux_id]
        check(f"{label}: power mux {mux_id} off, both LM73100 switches low", r is not None and r.ok and r.data == b"\x00" and tca_level(link, a) == 0 and tca_level(link, b) == 0,
              f"{r} {tca_level(link, a)} {tca_level(link, b)}")
    check(f"{label}: both rails disabled (EN low)", tca_level(link, 16) == 0 and tca_level(link, 17) == 0, f"{tca_level(link, 16)} {tca_level(link, 17)}")
    r = dev(link, PCA_ID, PINS_LOCKED)
    check(f"{label}: PCA9685 CH 12..15 (DRV8962 #2 EN) locked", u64(r) == (0xF << 12), hex(u64(r) or 0))
    lines = tuple(dev(link, PCA_ID, IO_GET_LEVEL, bytes([c])).data[0] for c in range(8, 16))
    check(f"{label}: DRV8962 #2 braked (IN low, EN high)", lines == (0, 0, 0, 0, 1, 1, 1, 1), str(lines))
    en = tuple(dev(link, GPIO_ID, IO_GET_LEVEL, bytes([p])).data[0] for p in (38, 39, 2, 1))
    check(f"{label}: DRV8962 #1 braked (EN1-4 on ESP 38 39 2 1 high)", en == (1, 1, 1, 1), str(en))
    vref = [i32(dev(link, DAC_ID, IO_GET_VOLTAGE, bytes([ch]))) for ch in (0, 1)]
    want = 2000 * 212 * 3090 // 1_000_000
    check(f"{label}: VREF of both bridges follows the 2 A limit ({want} mV)", all(v is not None and abs(v - want) <= 5 for v in vref), str(vref))
    st = power_status(link)
    check(f"{label}: power manager answers", st is not None, str(st))
    print("      power manager:", st)
    volts = [i32(dev(link, INA_ID, PMON_GET_VOLTAGE, bytes([ch]))) for ch in range(3)]
    amps = [i32(dev(link, INA_ID, PMON_GET_CURRENT, bytes([ch]))) for ch in range(3)]
    print("      INA3221 channels 0..2 (rail B, input, rail A): mV", volts, "mA", amps)
    check(f"{label}: board input (INA channel 1) 3..21 V", volts[1] is not None and 3000 <= volts[1] <= 21000, str(volts))
    check(f"{label}: the power manager sees the same input", st is not None and volts[1] is not None and abs(st["input_mV"] - volts[1]) < 300, f"{st} {volts}")
    check(f"{label}: ADS7128 reads its eight inputs",
          all(v is not None and 0 <= v <= 20000 for v in [i32(dev(link, ADS_ID, IO_GET_VOLTAGE, bytes([c]))) for c in range(8)]))
    time.sleep(0.5)
    t = link.mark()
    time.sleep(1.0)
    check(f"{label}: no error records at idle", new_errors(link, t) == [], str(new_errors(link, t))[:300])


def test_rails(link: Link) -> None:
    """Each TPS55289 rail through the power manager (the POWER_VREG ops are guarded by its budget): 5 V at the
    minimum current, read back by the INA3221, then off. The muxes stay off: the bridges are not energised."""
    for rail_id, name, en_pin in ((TPS_A_ID, "rail A", 17), (TPS_B_ID, "rail B", 16)):
        t = link.mark()
        r = dev(link, rail_id, VREG_SET_VOLTAGE, struct.pack("<I", 5000))
        check(f"{name}: SET_VOLTAGE 5000 mV", r is not None and r.ok, str(r))
        r = dev(link, rail_id, VREG_SET_CURRENT, struct.pack("<I", 200))
        check(f"{name}: SET_CURRENT 200 mA", r is not None and r.ok, str(r))
        r = dev(link, rail_id, VREG_SET_ENABLE, bytes([1]))
        check(f"{name}: SET_ENABLE on (returns at once, the rise runs in a timer)", r is not None and r.ok, str(r))
        time.sleep(1.5)
        mv = i32(dev(link, INA_ID, PMON_GET_VOLTAGE, bytes([INA_CH[rail_id]])))
        mA = i32(dev(link, INA_ID, PMON_GET_CURRENT, bytes([INA_CH[rail_id]])))
        print(f"      {name}: {mv} mV {mA} mA, EN pin {tca_level(link, en_pin)}")
        check(f"{name}: EN high and the INA3221 reads about 5 V", tca_level(link, en_pin) == 1 and mv is not None and 4500 <= mv <= 5500, f"{mv} {tca_level(link, en_pin)}")
        st = power_status(link)
        check(f"{name}: the manager allocated power to it", st is not None and st["allocated_mW"] > 0, str(st))
        r = dev(link, rail_id, VREG_SET_ENABLE, bytes([0]))
        check(f"{name}: SET_ENABLE off", r is not None and r.ok, str(r))
        time.sleep(0.5)
        check(f"{name}: EN low again", tca_level(link, en_pin) == 0, str(tca_level(link, en_pin)))
        errs = [e for e in new_errors(link, t) if "PWRM_FAULT" in e or "OVP" in e or "SCP" in e]
        check(f"{name}: no fault reported", errs == [], str(errs)[:300])
    r = dev(link, TPS_A_ID, TPS_SET_PROTECTION, bytes([0, 1, 7, 0]))
    check("protection back to the default", r is not None and r.ok, str(r))


def strip_boot_chips(link: Link) -> None:
    """Removes the chips the boot created except the GPIO and the TCA6424A, users before what they use, so the
    create-based tests below start from the devkit state."""
    for dev_id in (DRV2_ID, DRV1_ID, AP_ID, INA_ID, TPS_B_ID, TPS_A_ID, MUX2_ID, MUX1_ID, ADS_ID, PCA_ID, DAC_ID):
        r = dev(link, dev_id, OP_REMOVE)
        check(f"removed boot chip {dev_id} for the create tests", r is not None and r.ok, str(r))


def test_error_transport(link: Link) -> None:
    """The errors stream over the console UART: a command error is the response record and one record on the stream
    (same chain), a long request is cut and flagged, a burst loses nothing, an action step's failure and the device
    policy's response leave records, a failing policy action does not loop."""
    rt.log_set(link, *rt.LOG_DEFAULT)
    t = link.mark()
    r = dev(link, GPIO_ID, IO_SET_MODE, bytes([22, 3]))  # ERR_DEV_BAD_PIN
    time.sleep(0.4)
    recs = [x for x in link.error_records_since(t) if x.src == 1 and r is not None and x.seq == r.seq]
    check("device error: one record on the stream", r is not None and not r.ok and len(recs) == 1, f"{r} {len(recs)}")
    if recs and r is not None and r.record:
        check("device error: stream chain == response chain, request echoed",
              recs[0].names() == r.record.names() and recs[0].request[:1] == bytes([rt.KEY_DEVICE]),
              f"{recs[0].names()} {r.record.names()}")

    t = link.mark()
    seq = link.next_seq()
    link.send_frame(bytes([seq, 0x00, 0x7E]) + bytes(range(1, 120)))
    r = link.wait_response(seq)
    time.sleep(0.4)
    recs = [x for x in link.error_records_since(t) if x.src == 1 and x.seq == seq]
    check("long request: answered, the echoed copy is cut and flagged, the chain still decodes",
          r is not None and not r.ok and len(recs) == 1 and recs[0].request_cut and "ERR_ROUTER_UNKNOWN_KEY" in recs[0].names(),
          f"{r} {len(recs)}")

    for n in (10, 40):
        t = link.mark()
        seqs = []
        for _ in range(n):
            seqs.append(link.next_seq())
            link.send_frame(bytes([seqs[-1], 0x00, 0x7E]))
        got = {}
        end = time.time() + 6
        while time.time() < end and len(got) < n:
            for s in seqs:
                if s not in got:
                    resp = link.wait_response(s, timeout=0.05)
                    if resp is not None:
                        got[s] = resp
        time.sleep(1.0)
        recs = [x for x in link.error_records_since(t) if x.src == 1 and x.seq in seqs]
        check(f"burst of {n} errors: all answered, {n} distinct records on the stream, none corrupted",
              len(got) == n and len(recs) == n and len({x.seq for x in recs}) == n and all("ERR_ROUTER_UNKNOWN_KEY" in x.names() for x in recs),
              f"{len(got)} answers, {len(recs)} records")

    rt.act(link, rt.A_REMOVE, rt.u32(210))
    r = rt.upload(link, 210, rt.image(bytes([0x7E])))
    check("upload an action that fails (unknown root key)", r is not None and r.ok, str(r))
    t = link.mark()
    rt.act(link, rt.A_RUN, rt.u32(210))
    time.sleep(0.8)
    recs = link.error_records_since(t)
    check("a failing action step: the record names ERR_ACTION_STEP and the cause",
          any("ERR_ACTION_STEP" in x.names() and "ERR_ROUTER_UNKNOWN_KEY" in x.names() for x in recs), str([x.names() for x in recs]))
    rt.act(link, rt.A_REMOVE, rt.u32(210))

    # The device policy on the GPIO (MEDIUM: a HIGH error runs the HIGH action); a failing action is not run again.
    old = dev(link, GPIO_ID, rt.OP_GET_ERR_POLICY)
    old = old.data if old is not None and old.ok else struct.pack("<B4I", 1, 0, 0, 0, 0)
    rt.act(link, rt.A_REMOVE, rt.u32(200))
    r = rt.upload(link, 200, rt.image(bytes([rt.KEY_LOG, rt.LOG_SET, 2, 1, 1, 1, 1])))
    check("policy action uploaded", r is not None and r.ok, str(r))
    dev(link, GPIO_ID, rt.OP_SET_ERR_POLICY, struct.pack("<B4I", rt.IMP_MEDIUM, 0, 0, 200, 0))
    rt.log_set(link, *rt.LOG_DEFAULT)
    dev(link, GPIO_ID, IO_SET_MODE, bytes([22, 3]))
    time.sleep(0.6)
    check("policy: a HIGH error runs the action (log level 2)", (rt.log_get(link) or (0,))[0] == 2, str(rt.log_get(link)))
    rt.act(link, rt.A_REMOVE, rt.u32(200))
    r = rt.upload(link, 200, rt.image(bytes([0x7E])))
    t = link.mark()
    dev(link, GPIO_ID, IO_SET_MODE, bytes([22, 3]))
    time.sleep(1.2)
    recs = link.error_records_since(t)
    check("a failing policy action: the device record and the action's, no loop", 2 <= len(recs) <= 6 and rt.ping(link) is not None, f"{len(recs)}")
    dev(link, GPIO_ID, rt.OP_SET_ERR_POLICY, old)
    rt.act(link, rt.A_REMOVE, rt.u32(200))
    rt.log_set(link, *rt.LOG_DEFAULT)


def test_restart_boot(link: Link) -> None:
    """A restart brings every chip back by itself, also after the safe state (STOP): the TCA6424A is then held in
    reset, and RST stays low across the ESP restart (the create must release it before probing the chip)."""
    r = link.request(bytes([KEY_SYS, 0x02]), urgent=True)
    check("STOP before the restart", r is not None and r.ok, str(r))
    time.sleep(0.5)
    t = link.mark()
    r = link.request(bytes([KEY_SYS, 0x03]))
    check("RESTART answered", r is not None and r.ok, str(r))
    check("board boots again", link.wait_log("runIT up (0 boot step(s) failed)", 25.0, since=t))
    time.sleep(1.0)
    test_boot_chips(link, "after restart")


SUPPLY_SELECT = 0x0800
SRC_OFF, SRC_A, SRC_B = 0, 1, 2
# (mux id, bridge id, bridge nFAULT TCA pin, rail feeding source B, its INA channel, its EN pin)
DRY_BRIDGES = (
    (MUX1_ID, DRV1_ID, 11, TPS_B_ID, 0, 16),
    (MUX2_ID, DRV2_ID, 10, TPS_A_ID, 2, 17),
)


def test_dry_power(link: Link) -> None:
    """The LM73100 muxes and the rails switched on with nothing connected ("dry"): each bridge fed from the board
    input (source A) and from its regulator rail (source B), a channel driven for a moment, everything off again."""
    time.sleep(0.5)
    t0 = link.mark()

    def inp() -> tuple:
        return (i32(dev(link, INA_ID, PMON_GET_VOLTAGE, bytes([1]))), i32(dev(link, INA_ID, PMON_GET_CURRENT, bytes([1]))))

    def all_off() -> None:
        for mux_id, drv_id, _nf, rail_id, _ch, _en in DRY_BRIDGES:
            dev(link, drv_id, HB_COAST, bytes([0]))
            dev(link, drv_id, HB_COAST, bytes([1]))
            dev(link, mux_id, SUPPLY_SELECT, bytes([SRC_OFF]))
            dev(link, rail_id, VREG_SET_ENABLE, bytes([0]))

    base_mV, base_mA = inp()
    print(f"      input before: {base_mV} mV {base_mA} mA")
    try:
        for mux_id, drv_id, nfault, rail_id, ina_ch, en_pin in DRY_BRIDGES:
            a, b = MUX_PINS[mux_id]
            name = f"bridge {drv_id}"
            # Source A: the board input
            r = dev(link, mux_id, SUPPLY_SELECT, bytes([SRC_A]))
            check(f"{name}: mux {mux_id} SELECT A", r is not None and r.ok, str(r))
            time.sleep(0.4)
            r = dev(link, mux_id, SUPPLY_GET_SOURCE)
            check(f"{name}: source A on, only the A switch high (TCA {a})", r is not None and r.ok and r.data == bytes([SRC_A]) and tca_level(link, a) == 1 and tca_level(link, b) == 0,
                  f"{r} {tca_level(link, a)} {tca_level(link, b)}")
            check(f"{name}: nFAULT stays high with VM from the input", tca_level(link, nfault) == 1, str(tca_level(link, nfault)))
            mV, mA = inp()
            print(f"      {name} on A: input {mV} mV {mA} mA")
            check(f"{name}: the input holds up (>= 4.5 V)", mV is not None and mV >= 4500, str((mV, mA)))
            for ch, drive in ((0, 300), (0, -300), (1, 300), (1, -300)):
                r = dev(link, drv_id, HB_SET_DRIVE, struct.pack("<Bh", ch, drive))
                time.sleep(0.25)
                cur = i32(dev(link, drv_id, HB_GET_CURRENT, bytes([ch])))
                ok_fault = tca_level(link, nfault) == 1
                check(f"{name}: ch{ch} drive {drive / 10:+.0f} % with VM on A (no load): accepted, nFAULT high", r is not None and r.ok and ok_fault, f"{r} nFAULT={tca_level(link, nfault)} {cur}")
                print(f"        current {cur} mA, input {inp()}")
            dev(link, drv_id, HB_BRAKE, bytes([0]))
            dev(link, drv_id, HB_BRAKE, bytes([1]))
            r = dev(link, mux_id, SUPPLY_SELECT, bytes([SRC_OFF]))
            check(f"{name}: SELECT OFF", r is not None and r.ok and tca_level(link, a) == 0 and tca_level(link, b) == 0, str(r))

            # Source B: its regulator rail at 5 V
            dev(link, rail_id, VREG_SET_VOLTAGE, struct.pack("<I", 5000))
            dev(link, rail_id, VREG_SET_CURRENT, struct.pack("<I", 200))
            r = dev(link, rail_id, VREG_SET_ENABLE, bytes([1]))
            check(f"{name}: rail {rail_id} enabled", r is not None and r.ok, str(r))
            time.sleep(1.5)
            r = dev(link, mux_id, SUPPLY_SELECT, bytes([SRC_B]))
            check(f"{name}: mux {mux_id} SELECT B", r is not None and r.ok, str(r))
            time.sleep(0.4)
            r = dev(link, mux_id, SUPPLY_GET_SOURCE)
            check(f"{name}: source B on, only the B switch high (TCA {b})", r is not None and r.ok and r.data == bytes([SRC_B]) and tca_level(link, b) == 1 and tca_level(link, a) == 0,
                  f"{r} {tca_level(link, a)} {tca_level(link, b)}")
            rail_mV = i32(dev(link, INA_ID, PMON_GET_VOLTAGE, bytes([ina_ch])))
            check(f"{name}: the rail holds about 5 V with the bridge on it", rail_mV is not None and 4300 <= rail_mV <= 5500 and tca_level(link, nfault) == 1, f"{rail_mV} nFAULT={tca_level(link, nfault)}")
            for ch, drive in ((0, 500), (1, -500)):
                r = dev(link, drv_id, HB_SET_DRIVE, struct.pack("<Bh", ch, drive))
                time.sleep(0.25)
                check(f"{name}: ch{ch} drive {drive / 10:+.0f} % with VM on B (no load)", r is not None and r.ok and tca_level(link, nfault) == 1, f"{r} nFAULT={tca_level(link, nfault)}")
            dev(link, drv_id, HB_COAST, bytes([0]))
            dev(link, drv_id, HB_COAST, bytes([1]))
            dev(link, mux_id, SUPPLY_SELECT, bytes([SRC_OFF]))
            dev(link, rail_id, VREG_SET_ENABLE, bytes([0]))
            time.sleep(0.4)

        # Everything on at once: both rails, both muxes on their rails, all channels driven. The budget of a plain 5 V USB
        # source (assumed 1 A, minus the ESP32's reserve) fits one rail at 5 V / 200 mA, not two: the manager refuses the
        # second (ERR_PWRM_BUDGET_EXCEEDED, checked below). Its checks are switched off for this step only.
        r = dev(link, TPS_A_ID, VREG_SET_ENABLE, bytes([1]))
        r2 = dev(link, TPS_B_ID, VREG_SET_ENABLE, bytes([1]))
        check("budget: with one rail on, the second 5 V rail is refused", r is not None and r.ok and err_cause(r2) == "ERR_PWRM_BUDGET_EXCEEDED", f"{r} {r2}")
        dev(link, TPS_A_ID, VREG_SET_ENABLE, bytes([0]))
        time.sleep(0.5)
        r = link.request(bytes([KEY_POWER_MGR, 1]), payload=rt.pw_config(enabled=0))
        check("power manager checks off for the all-on step", r is not None and r.ok, str(r))
        for mux_id, drv_id, nfault, rail_id, ina_ch, en_pin in DRY_BRIDGES:
            dev(link, rail_id, VREG_SET_VOLTAGE, struct.pack("<I", 5000))
            dev(link, rail_id, VREG_SET_CURRENT, struct.pack("<I", 200))
            r = dev(link, rail_id, VREG_SET_ENABLE, bytes([1]))
            check(f"all on: rail {rail_id} enabled", r is not None and r.ok, str(r))
        time.sleep(1.5)
        for mux_id, drv_id, nfault, rail_id, ina_ch, en_pin in DRY_BRIDGES:
            r = dev(link, mux_id, SUPPLY_SELECT, bytes([SRC_B]))
            check(f"all on: mux {mux_id} SELECT B", r is not None and r.ok, str(r))
            for ch, drive in ((0, 300), (1, 300)):
                dev(link, drv_id, HB_SET_DRIVE, struct.pack("<Bh", ch, drive))
        time.sleep(1.0)
        mV, mA = inp()
        st = power_status(link)
        print(f"      everything on: input {mV} mV {mA} mA, power manager {st}")
        check("all on: both rails still ~5 V, no nFAULT", all(
            4300 <= (i32(dev(link, INA_ID, PMON_GET_VOLTAGE, bytes([ch]))) or 0) <= 5500 for ch in (0, 2)) and tca_level(link, 10) == 1 and tca_level(link, 11) == 1,
              f"{tca_level(link, 10)} {tca_level(link, 11)}")
        check("all on: the input holds up", mV is not None and mV >= 4300, str((mV, mA)))
    finally:
        all_off()
        link.request(bytes([KEY_POWER_MGR, 1]), payload=rt.pw_config())  # the default (enabled, strict)
    time.sleep(0.8)
    check("everything off again: switches low, EN low", all(tca_level(link, p) == 0 for p in (12, 13, 14, 15, 16, 17)), str([tca_level(link, p) for p in (12, 13, 14, 15, 16, 17)]))
    for mux_id in (MUX1_ID, MUX2_ID):
        r = dev(link, mux_id, SUPPLY_GET_SOURCE)
        check(f"mux {mux_id} off", r is not None and r.ok and r.data == bytes([SRC_OFF]), str(r))
    errs = new_errors(link, t0)
    # A rail in its 200 mA limit while it charges the bridge's bulk capacitance is reported (OCP, NOTIFY): not a failure.
    inrush = [e for e in errs if "ERR_PWRM_FAULT" in e and " 05 00 c8 00" in e]
    refused = [e for e in errs if "ERR_PWRM_BUDGET_EXCEEDED" in e]
    other = [e for e in errs if e not in inrush and e not in refused]
    print(f"      informational: {len(inrush)} OCP-limit notice(s) (inrush), {len(refused)} budget refusal(s)")
    check("no other error record during the dry power test", other == [], str(other)[:600])


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", required=True)
    ap.add_argument("--boot-wait", type=float, default=15.0)
    ap.add_argument("--echo", action="store_true")
    ap.add_argument("--energise", action="store_true", help="switch the power muxes and the rails on with nothing connected (dry)")
    ap.add_argument("--skip", default="", help="comma list: boot,rails,dac,ads,servo,drv2,drv1,tca,power,safe,cycles,errors")
    a = ap.parse_args()
    skip = set(a.skip.split(","))

    with Link(a.port, echo=a.echo) as link:
        if not wait_boot(link, a.boot_wait):
            print("note: no 'runIT up' seen (already running?) - continuing")
        test_boot(link)
        if "boot" not in skip:
            test_boot_chips(link)
            if "rails" not in skip:
                test_rails(link)
            if a.energise:
                test_dry_power(link)
            strip_boot_chips(link)
        if not setup_base(link):
            print("base devices failed, stopping")
            return 1
        if "dac" not in skip:
            test_dac(link)
        else:
            create(link, DAC_ID, DAC_TYPE, bytes([0, 0x48]))
        if "ads" not in skip:
            test_ads(link)
        if "servo" not in skip:
            test_servo(link)
        if "drv2" not in skip:
            test_drv2(link)
        if "drv1" not in skip:
            test_drv1(link)
        if "tca" not in skip:
            test_tca(link)
        if "power" not in skip:
            test_power_chips(link)
        if "safe" not in skip:
            test_safe_state(link)
        if "cycles" not in skip:
            test_policy_and_cycles(link)
        if "errors" not in skip:
            test_error_transport(link)
        for dev_id in (ADS_ID, DAC_ID, PCA_ID, TCA_ID, GPIO_ID):
            if dev_id not in BOOT_DEVICES:
                dev(link, dev_id, OP_REMOVE)
        if "boot" not in skip:
            test_restart_boot(link)

    failed = [r for r in rt.RESULTS if not r[1]]
    print(f"\n{len(rt.RESULTS) - len(failed)}/{len(rt.RESULTS)} passed")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
