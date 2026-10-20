"""Tests of the onboard chips of the real runIT PCB (every chip connected, HARDWARE.md section 3).

  python board_tests.py --port COM9

The firmware creates every onboard chip at boot and marks them the board's own: they can't be removed
(ERR_DEV_SYSTEM). The checks run on those instances (DAC53202, ADS7128, the two DRV8962, TCA6424A, the power chips);
only a servo (id 20) and refused creates on a spare id (30) are created here, and removed again. The H-bridge motor
supplies (the power muxes) and the rails are never switched on unless `--energise` (dry: nothing connected), so no
load is driven.

Requests go as `[0x05][dir][id u16][op u16][args]` (dir 0 read, 1 write; router_link.frame adds it from the
descriptors). Ops are `(contract_id << 8) | op` (generated/contracts/) or a device's own `0xFF00 | op`
(generated/devices/); a command with both sides gets `dir` here, and so does every own op. A write answers ACK, a
read takes the key (a pin, a channel) and answers it with the value.
"""

from __future__ import annotations

import argparse
import struct
import sys
import time

import router_tests as rt
from router_link import KEY_DEVICE, KEY_SYS, READ, WRITE, Link, Response, frame
from router_tests import check, err_cause, wait_boot

# ---- device ops (generated/contracts/contract_*.json, generated/devices/device_*.json) ----
OP_CREATE, OP_RESET, OP_SUSPEND, OP_RESUME, OP_REMOVE, OP_INFO, OP_OPS = 0x0000, 0x0001, 0x0002, 0x0003, 0x0004, 0x0005, 0x0006
OP_ERROR_POLICY = 0x0007  # RW
IO_LEVEL, IO_TOGGLE, IO_PWM_DUTY, IO_PWM_FREQ = 0x0100, 0x0101, 0x0103, 0x0104  # LEVEL RW
IO_MODE, IO_RESET, IO_INTR, IO_VOLTAGE = 0x0105, 0x0106, 0x0107, 0x0109  # VOLTAGE RW (DAC), R (ESP, ADS)
PINS_LOCKED = 0x0201
EVENT_ON_EVENT = 0x0300
PMON_VOLTAGE, PMON_CURRENT, PMON_POWER = 0x0400, 0x0401, 0x0403
VREG_ENABLE, VREG_VOLTAGE, VREG_CURRENT = 0x0500, 0x0501, 0x0502
PD_OPTIONS, PD_LIMITS = 0x0701, 0x0702
SUPPLY_SOURCE = 0x0800  # RW
HB_DRIVE, HB_BRAKE, HB_COAST, HB_CURRENT = 0x0900, 0x0901, 0x0902, 0x0903
SERVO_HOME, SERVO_TRIM, SERVO_TARGET, SERVO_PULSE, SERVO_RELEASE = 0xFF00, 0xFF01, 0xFF02, 0xFF03, 0xFF04  # W W W R W
DRV_CURRENT_LIMIT, DRV_CLEAR_FAULT, DRV_FAULT, DRV_SETTINGS = 0xFF00, 0xFF01, 0xFF02, 0xFF03  # W W R RW
TPS_PROTECTION = 0xFF01  # RW
SIDE_R, SIDE_W = 0x01, 0x02  # OPS: the sides of an op

# ---- module routers (generated/services/) ----
SYS_STOP, SYS_RESTART, SYS_SAFE, SYS_SAFE_LEAVE = 0x02, 0x03, 0x09, 0x0A
SAFE_STOP = 1  # sys_safe_reason_e
KEY_LOG, LOG_CONFIG = 0x08, 0x01  # RW
LOG_DEFAULT = (3, 1, 1, 1, 1)  # (level, mirror_serial, trace_errors, send_errors, send_logs)
KEY_POWER_MGR, PW_CONFIG, PW_STATUS = 0x0A, 0x01, 0x07  # CONFIG RW, STATUS R

# ---- the board (runit-esp/components/app/runit/board.json) ----
GPIO_TYPE, PCA_TYPE, TCA_TYPE, TPS_TYPE, INA_TYPE, AP_TYPE = 0x40, 0x41, 0x42, 0x43, 0x44, 0x45
DAC_TYPE, ADS_TYPE, SERVO_TYPE, DRV_TYPE, MUX_TYPE = 0x46, 0x47, 0x48, 0x49, 0x4A
GPIO_ID, TCA_ID, ADS_ID, PCA_ID, DAC_ID, DRV1_ID, DRV2_ID = 0, 1, 2, 3, 4, 5, 6
MUX1_ID, MUX2_ID, TPS_A_ID, TPS_B_ID, INA_ID, AP_ID = 7, 8, 10, 11, 12, 13
SERVO_ID = 20  # created by the tests (on PCA CH 0)
SPARE_ID = 30  # creates that must be refused, and the create / remove cycles
NONE = struct.pack("<HB", 0xFFFF, 0xFF)
DRV_LIMIT_MA = 2000  # RUNIT_BOARD_DRV8962_LIMIT_MA: the board's chopping limit of both bridges
PCA_PWM_HZ = 50  # RUNIT_BOARD_DRV8962_PCA_PWM_HZ: the PCA9685's one frequency (servos and DRV8962 #2)
MUX_PINS = {MUX1_ID: (14, 15), MUX2_ID: (12, 13)}  # TCA pins (A = board input, B = rail) of the LM73100 pairs; high = on
INA_CH = {TPS_A_ID: 2, TPS_B_ID: 0}  # INA3221 channel of each rail (HARDWARE.md); the board input is channel 1

# Every onboard chip the firmware creates at boot, in its creation order (runit_board_cfg.c): (id, type, name)
BOOT_CHIPS = [
    (0, 0x40, "GPIO ESP"), (1, 0x42, "TCA6424A"), (7, 0x4A, "power mux #1"), (8, 0x4A, "power mux #2"), (2, 0x47, "ADS7128"),
    (3, 0x41, "PCA9685"), (10, 0x43, "TPS55289 rail A"), (11, 0x43, "TPS55289 rail B"), (12, 0x44, "INA3221"),
    (13, 0x45, "AP33772S"), (4, 0x46, "DAC53202"), (5, 0x49, "DRV8962 #1"), (6, 0x49, "DRV8962 #2"),
]


def dev(link: Link, dev_id: int, op: int, payload: bytes = b"", dir: int | None = None) -> Response | None:
    """A device op; `dir` for an op with both sides (and every own op: they differ between the types)."""
    return link.request(bytes([KEY_DEVICE]) + struct.pack("<HH", dev_id, op), payload=payload, dir=dir)


def steps(permille: int) -> int:
    """A drive given in thousandths as the contract's PWM steps (-4095..4095, 12 bits)."""
    return round(permille * 4095 / 1000)


def vref_mV(limit_mA: int) -> int:
    """DRV8962 VREF for a current limit (default RIPROPI 3090 ohm, 212 uA/A); no limit = 3300 mV."""
    return min(limit_mA * 212 * 3090 // 1_000_000, 3300) if limit_mA else 3300


def pin(device: int, p: int) -> bytes:
    return struct.pack("<HB", device, p)


def create(link: Link, dev_id: int, type_id: int, cfg: bytes):
    return dev(link, dev_id, OP_CREATE, struct.pack("<H", type_id) + cfg)


def remove(link: Link, dev_id: int):
    r = dev(link, dev_id, OP_REMOVE)
    check(f"REMOVE {dev_id}", r is not None and r.ok, str(r))


def discard(link: Link, dev_id: int) -> None:
    """Removes a test device a refused create should not have left (no check: the create's check failed already)."""
    if exists(link, dev_id):
        dev(link, dev_id, OP_REMOVE)


def i32(r) -> int | None:
    return struct.unpack("<i", r.data)[0] if r is not None and r.ok and len(r.data) == 4 else None


def u64(r) -> int | None:
    return struct.unpack("<Q", r.data)[0] if r is not None and r.ok and len(r.data) == 8 else None


def new_errors(link: Link, t: float) -> list[str]:
    return [str(rec) for rec in link.error_records_since(t)]


def exists(link: Link, dev_id: int) -> bool:
    r = dev(link, dev_id, OP_INFO)
    return r is not None and r.ok


def ops(link: Link, dev_id: int) -> dict[int, int] | None:
    """OPS: {op: sides} (rows of [op u16][sides u8])."""
    r = dev(link, dev_id, OP_OPS)
    if r is None or not r.ok or len(r.data) % 3:
        return None
    return {op: sides for op, sides in struct.iter_unpack("<HB", r.data)}


def has_ops(got: dict[int, int] | None, want: dict[int, int]) -> bool:
    return got is not None and all(got.get(op) == sides for op, sides in want.items())


def show_ops(got: dict[int, int] | None) -> str:
    return "none" if got is None else " ".join(f"{op:04x}:{'R' if s & SIDE_R else ''}{'W' if s & SIDE_W else ''}" for op, s in sorted(got.items()))


def level(link: Link, dev_id: int, p: int) -> int | None:
    """IO LEVEL read: {pin} -> {pin, level}."""
    r = dev(link, dev_id, IO_LEVEL, bytes([p]), dir=READ)
    return r.data[1] if r is not None and r.ok and len(r.data) == 2 and r.data[0] == p else None


def set_level(link: Link, dev_id: int, p: int, lv: int) -> Response | None:
    return dev(link, dev_id, IO_LEVEL, bytes([p, lv]), dir=WRITE)


def volts(link: Link, dev_id: int, ch: int) -> int | None:
    """IO VOLTAGE read: {pin} -> {pin, voltage_mV i32} (DAC: the setting, ESP / ADS: a measurement)."""
    r = dev(link, dev_id, IO_VOLTAGE, bytes([ch]), dir=READ)
    return struct.unpack_from("<i", r.data, 1)[0] if r is not None and r.ok and len(r.data) == 5 and r.data[0] == ch else None


def set_volts(link: Link, dev_id: int, ch: int, mv: int) -> Response | None:
    return dev(link, dev_id, IO_VOLTAGE, struct.pack("<Bi", ch, mv), dir=WRITE)


def source(link: Link, mux_id: int) -> int | None:
    """POWER_SUPPLY SOURCE read: {source}."""
    r = dev(link, mux_id, SUPPLY_SOURCE, dir=READ)
    return r.data[0] if r is not None and r.ok and len(r.data) == 1 else None


def select(link: Link, mux_id: int, src: int) -> Response | None:
    return dev(link, mux_id, SUPPLY_SOURCE, bytes([src]), dir=WRITE)


def tca_level(link: Link, p: int) -> int | None:
    return level(link, TCA_ID, p)


def locked(link: Link, dev_id: int) -> int | None:
    return u64(dev(link, dev_id, PINS_LOCKED))


def drv_limit(link: Link, dev_id: int, limit_mA: int) -> Response | None:
    return dev(link, dev_id, DRV_CURRENT_LIMIT, struct.pack("<I", limit_mA), dir=WRITE)


def restore_vref(link: Link) -> None:
    """Both bridges' limit back to the board's: the DAC drives VREF again after a test changed or reset it."""
    for drv_id in (DRV1_ID, DRV2_ID):
        r = drv_limit(link, drv_id, DRV_LIMIT_MA)
        check(f"DRV8962 {drv_id}: limit back to {DRV_LIMIT_MA} mA", r is not None and r.ok, str(r))


def pm(link: Link, op: int, payload: bytes = b"", dir: int | None = None) -> Response | None:
    return link.request(bytes([KEY_POWER_MGR, op]), payload=payload, dir=dir)


def log_get(link: Link) -> tuple | None:
    r = link.request(bytes([KEY_LOG, LOG_CONFIG]), dir=READ)
    return tuple(r.data) if r is not None and r.ok and len(r.data) == 5 else None


def log_set(link: Link, *cfg: int) -> Response | None:
    return link.request(bytes([KEY_LOG, LOG_CONFIG]), payload=bytes(cfg), dir=WRITE)


def safe_reason(link: Link) -> int | None:
    """SYS SAFE: {reason u8, for_ms u32}; 0 = not in the safe state."""
    r = link.request(bytes([KEY_SYS, SYS_SAFE]))
    return r.data[0] if r is not None and r.ok and len(r.data) == 5 else None


# ---------------------------------------------------------------------------


def test_boot(link: Link) -> None:
    """What the firmware creates by itself: the ESP GPIO, the TCA6424A, and the status LED lit."""
    for dev_id, type_id, name in ((GPIO_ID, GPIO_TYPE, "GPIO ESP"), (TCA_ID, TCA_TYPE, "TCA6424A")):
        r = dev(link, dev_id, OP_INFO)
        check(f"boot created {name} {dev_id}", r is not None and r.ok and r.data == struct.pack("<HB", type_id, 0), str(r))
    if exists(link, TCA_ID):
        check("boot lit the status LED (TCA pin 23 high)", tca_level(link, 23) == 1, str(tca_level(link, 23)))
        locked_mask = locked(link, TCA_ID) or 0
        check("the LED pin is a plain output (not locked)", not locked_mask & (1 << 23), hex(locked_mask))
        levels = {tca_level(link, 22) for _ in range(20)}
        check("pin 22 is an output at a steady level (a floating input toggles and floods INT)", levels == {0}, str(levels))
        t = link.mark()
        time.sleep(1.0)
        floods = [e for e in new_errors(link, t) if "QUEUE_FULL" in e]
        check("no event storm at idle (no queue-full reports)", floods == [], f"{len(floods)} reports")


def board_ready(link: Link) -> bool:
    """Every onboard chip is there: the sections below test the board's instances."""
    missing = [name for dev_id, _t, name in BOOT_CHIPS if not exists(link, dev_id)]
    return check("every onboard chip exists (the tests use the board's instances)", missing == [], str(missing))


def test_dac(link: Link) -> None:
    """DAC53202 (id 4): OUT0 / OUT1 are the VREF of DRV8962 #1 / #2 (not powered: the muxes stay off). The bridges set
    VREF again at the end."""
    r = create(link, SPARE_ID, DAC_TYPE, bytes([0, 0x47]))
    check("CREATE DAC53202 with address 0x47 -> ERR_DEV_RANGE", err_cause(r) == "ERR_DEV_RANGE", str(r))
    discard(link, SPARE_ID)
    r = dev(link, DAC_ID, OP_INFO)
    check("INFO: type 0x0046", r is not None and r.ok and r.data == struct.pack("<HB", DAC_TYPE, 0), str(r))
    o = ops(link, DAC_ID)
    check("OPS: IO RESET (write), VOLTAGE (read and write), no LEVEL", has_ops(o, {IO_RESET: SIDE_W, IO_VOLTAGE: SIDE_R | SIDE_W}) and IO_LEVEL not in o, show_ops(o))
    r = dev(link, DAC_ID, IO_VOLTAGE, bytes([1]), dir=READ)
    check("VOLTAGE read answers {pin, voltage_mV}: VREF of the board's limit", r is not None and r.ok and len(r.data) == 5 and r.data[0] == 1
          and abs(struct.unpack_from("<i", r.data, 1)[0] - vref_mV(DRV_LIMIT_MA)) <= 5, str(r))

    for ch, mv in ((0, 1000), (1, 2500), (0, 3300), (0, 0)):
        r = set_volts(link, DAC_ID, ch, mv)
        got = volts(link, DAC_ID, ch)
        check(f"VOLTAGE write ch{ch} {mv} mV: ACK, reads back", r is not None and r.ok and r.data == b"" and got is not None and abs(got - mv) <= 4, f"{r} {got}")
    r = set_volts(link, DAC_ID, 0, 3301)
    check("3301 mV -> ERR_DEV_RANGE", err_cause(r) == "ERR_DEV_RANGE", str(r))
    r = set_volts(link, DAC_ID, 0, -1)
    check("-1 mV -> ERR_DEV_RANGE", err_cause(r) == "ERR_DEV_RANGE", str(r))
    r = set_volts(link, DAC_ID, 2, 100)
    check("channel 2 -> ERR_DEV_BAD_PIN", err_cause(r) == "ERR_DEV_BAD_PIN", str(r))
    r = dev(link, DAC_ID, IO_VOLTAGE, bytes([2]), dir=READ)
    check("VOLTAGE read of channel 2 -> ERR_DEV_BAD_PIN", err_cause(r) == "ERR_DEV_BAD_PIN", str(r))
    set_volts(link, DAC_ID, 1, 1200)
    r = dev(link, DAC_ID, OP_SUSPEND)
    check("SUSPEND", r is not None and r.ok, str(r))
    r = set_volts(link, DAC_ID, 1, 500)
    check("VOLTAGE write while suspended -> ERR_DEV_SUSPENDED", err_cause(r) == "ERR_DEV_SUSPENDED", str(r))
    v = volts(link, DAC_ID, 1)
    check("VOLTAGE read while suspended still answers", v is not None and abs(v - 1200) <= 4, str(v))
    r = dev(link, DAC_ID, OP_RESUME)
    check("RESUME", r is not None and r.ok, str(r))
    v = volts(link, DAC_ID, 1)
    check("RESUME keeps the voltage", abs((v or 0) - 1200) <= 4, str(v))
    r = dev(link, DAC_ID, IO_RESET, bytes([1]))
    check("IO RESET ch1 -> reads 0", r is not None and r.ok and volts(link, DAC_ID, 1) == 0, str(r))
    set_volts(link, DAC_ID, 0, 800)
    r = dev(link, DAC_ID, OP_RESET)
    check("device RESET: both channels read 0", r is not None and r.ok and volts(link, DAC_ID, 0) == 0 and volts(link, DAC_ID, 1) == 0, str(r))
    restore_vref(link)
    vref = [volts(link, DAC_ID, ch) for ch in (0, 1)]
    check(f"VREF of both bridges back at {vref_mV(DRV_LIMIT_MA)} mV", all(v is not None and abs(v - vref_mV(DRV_LIMIT_MA)) <= 5 for v in vref), str(vref))


def intr(ch: int, mode: int, up: int, down: int, hyst: int = 0, count: int = 0) -> bytes:
    """IO INTR args: {pin, mode, debounce, threshold_up_mV, threshold_down_mV, hysteresis_mV, event_counter_threshold}."""
    return struct.pack("<BBBHHHH", ch, mode, 0, up, down, hyst, count)


def test_ads(link: Link) -> None:
    """ADS7128 (id 2: 0x10, ALERT ESP 42, full scale 20000 mV)."""
    r = create(link, SPARE_ID, ADS_TYPE, bytes([0, 0x10]) + pin(GPIO_ID, 42) + struct.pack("<H", 0))
    check("CREATE ADS7128 with full scale 0 -> ERR_DEV_RANGE", err_cause(r) == "ERR_DEV_RANGE", str(r))
    discard(link, SPARE_ID)
    r = dev(link, ADS_ID, OP_INFO)
    check("INFO: type 0x0047", r is not None and r.ok and r.data == struct.pack("<HB", ADS_TYPE, 0), str(r))
    o = ops(link, ADS_ID)
    check("OPS: VOLTAGE read only, MODE / INTR / RESET writes", has_ops(o, {IO_VOLTAGE: SIDE_R, IO_MODE: SIDE_W, IO_INTR: SIDE_W, IO_RESET: SIDE_W}), show_ops(o))

    readings = [volts(link, ADS_ID, ch) for ch in range(8)]
    check("8 channels read 0..20000 mV", all(v is not None and 0 <= v <= 20000 for v in readings), str(readings))
    print("      readings (mV):", readings)
    r = dev(link, ADS_ID, IO_VOLTAGE, bytes([8]), dir=READ)
    check("channel 8 -> ERR_DEV_BAD_PIN", err_cause(r) == "ERR_DEV_BAD_PIN", str(r))
    r = dev(link, ADS_ID, IO_VOLTAGE, struct.pack("<Bi", 0, 1000), dir=WRITE)
    check("VOLTAGE write (no write side here) -> ERR_CMD_DIRECTION", err_cause(r) == "ERR_CMD_DIRECTION", str(r))
    r = dev(link, ADS_ID, IO_MODE, bytes([0, 7]))
    check("MODE ADC is accepted", r is not None and r.ok, str(r))
    r = dev(link, ADS_ID, IO_MODE, bytes([0, 0]))
    check("MODE INPUT -> ERR_IO_MODE_UNSUPPORTED", err_cause(r) == "ERR_IO_MODE_UNSUPPORTED", str(r))

    r = dev(link, ADS_ID, IO_INTR, intr(0, 2, 0, 0))
    check("INTR falling edge -> ERR_IO_FEATURE_UNSUPPORTED", err_cause(r) == "ERR_IO_FEATURE_UNSUPPORTED", str(r))
    r = dev(link, ADS_ID, IO_INTR, intr(0, 9, 0, 0))
    check("INTR mode 9 -> ERR_CMD_BAD_ARG", err_cause(r) == "ERR_CMD_BAD_ARG", str(r))
    r = dev(link, ADS_ID, IO_INTR, intr(0, 4, 21000, 0))
    check("threshold above full scale -> ERR_DEV_RANGE", err_cause(r) == "ERR_DEV_RANGE", str(r))
    r = dev(link, ADS_ID, IO_INTR, intr(0, 4, 5000, 6000))
    check("lower threshold above the upper one -> ERR_DEV_RANGE", err_cause(r) == "ERR_DEV_RANGE", str(r))
    r = dev(link, ADS_ID, IO_INTR, intr(0, 4, 10000, 1000, 100, 17))
    check("event count 17 -> ERR_DEV_RANGE", err_cause(r) == "ERR_DEV_RANGE", str(r))

    # A window the idle input is outside of: the chip raises ALERT, the device must handle it without an error.
    ch = 0
    time.sleep(0.5)  # the error records of the refused calls above are still on their way
    t1 = link.mark()
    r = dev(link, ADS_ID, IO_INTR, intr(ch, 4, 19000, 18000, 100, 2))
    check("INTR window 18..19 V (outside)", r is not None and r.ok, str(r))
    time.sleep(1.0)
    check("autonomous conversions: reads still work", volts(link, ADS_ID, ch) is not None and volts(link, ADS_ID, 1) is not None,
          f"{volts(link, ADS_ID, ch)} {volts(link, ADS_ID, 1)}")
    check("ALERT line is released after the handler (ESP 42 high)", level(link, GPIO_ID, 42) == 1, str(level(link, GPIO_ID, 42)))
    check("no error records while the alert ran", new_errors(link, t1) == [], str(new_errors(link, t1)))
    r = dev(link, ADS_ID, IO_INTR, intr(ch, 5, 20000, 0))
    check("INTR window 0..20 V (inside)", r is not None and r.ok, str(r))
    time.sleep(1.0)
    check("inside window: ALERT released again", level(link, GPIO_ID, 42) == 1, str(level(link, GPIO_ID, 42)))
    check("no error records after the inside alert", new_errors(link, t1) == [], str(new_errors(link, t1)))
    r = dev(link, ADS_ID, IO_INTR, intr(ch, 0, 0, 0))
    check("INTR disable", r is not None and r.ok, str(r))
    r = dev(link, ADS_ID, OP_SUSPEND)
    check("SUSPEND", r is not None and r.ok, str(r))
    r = dev(link, ADS_ID, IO_VOLTAGE, bytes([0]), dir=READ)
    check("VOLTAGE read while suspended -> ERR_DEV_SUSPENDED", err_cause(r) == "ERR_DEV_SUSPENDED", str(r))
    r = dev(link, ADS_ID, OP_RESUME)
    check("RESUME", r is not None and r.ok and volts(link, ADS_ID, 0) is not None, str(r))
    r = dev(link, ADS_ID, IO_RESET, bytes([0]))
    check("IO RESET ch0", r is not None and r.ok, str(r))
    r = dev(link, ADS_ID, OP_RESET)
    check("device RESET", r is not None and r.ok and volts(link, ADS_ID, 0) is not None, str(r))


def servo_cfg(pwm: bytes, kind: int, byte_mode: int, invert: int, amin: int, amax: int, default: int, min_us: int, max_us: int, dead: int) -> bytes:
    return pwm + struct.pack("<BBBhhhHHH", kind, byte_mode, invert, amin, amax, default, min_us, max_us, dead)


def pulse(link: Link) -> int | None:
    r = dev(link, SERVO_ID, SERVO_PULSE, dir=READ)
    return struct.unpack("<H", r.data)[0] if r is not None and r.ok and len(r.data) == 2 else None


def servo_target(link: Link, x10: int) -> Response | None:
    return dev(link, SERVO_ID, SERVO_TARGET, struct.pack("<h", x10), dir=WRITE)


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
    o = ops(link, SERVO_ID)
    check("OPS: home / trim / target / release writes, pulse read", has_ops(o, {SERVO_HOME: SIDE_W, SERVO_TRIM: SIDE_W, SERVO_TARGET: SIDE_W, SERVO_PULSE: SIDE_R, SERVO_RELEASE: SIDE_W}), show_ops(o))

    check("at create the servo is at 90 deg: 1500 us", pulse(link) == 1500, str(pulse(link)))
    for x10, want in ((0, 500), (1800, 2500), (450, 1000), (900, 1500)):
        r = servo_target(link, x10)
        check(f"target {x10 / 10} deg -> {want} us", r is not None and r.ok and pulse(link) == want, f"{r} {pulse(link)}")
    r = servo_target(link, 1801)
    check("target 180.1 deg -> ERR_DEV_RANGE", err_cause(r) == "ERR_DEV_RANGE", str(r))
    r = dev(link, SERVO_ID, SERVO_TRIM, struct.pack("<h", 501), dir=WRITE)
    check("trim 501 -> ERR_DEV_RANGE", err_cause(r) == "ERR_DEV_RANGE", str(r))
    dev(link, SERVO_ID, SERVO_TRIM, struct.pack("<h", 10), dir=WRITE)
    r = dev(link, SERVO_ID, SERVO_HOME, dir=WRITE)
    check("trim 10 deg then HOME -> 1611 us", r is not None and r.ok and pulse(link) == 1611, f"{r} {pulse(link)}")
    dev(link, SERVO_ID, SERVO_TRIM, struct.pack("<h", 0), dir=WRITE)
    r = dev(link, SERVO_ID, OP_SUSPEND)
    check("SUSPEND (no pulse)", r is not None and r.ok and pulse(link) == 0, f"{r} {pulse(link)}")
    r = servo_target(link, 900)
    check("target while suspended -> ERR_DEV_SUSPENDED", err_cause(r) == "ERR_DEV_SUSPENDED", str(r))
    r = dev(link, SERVO_ID, OP_RESUME)
    check("RESUME gives the last pulse back (1611 us after HOME with trim)", r is not None and r.ok and pulse(link) == 1611, f"{r} {pulse(link)}")
    r = dev(link, SERVO_ID, SERVO_RELEASE, dir=WRITE)
    check("RELEASE: pulse 0", r is not None and r.ok and pulse(link) == 0, f"{r} {pulse(link)}")
    r = dev(link, SERVO_ID, SERVO_HOME, dir=WRITE)
    check("HOME drives it again", r is not None and r.ok and pulse(link) == 1500, f"{r} {pulse(link)}")
    remove(link, SERVO_ID)

    # Continuous servo: stop in the middle, speed outwards from beyond the deadband
    r = create(link, SERVO_ID, SERVO_TYPE, servo_cfg(pwm, 1, 0, 0, 0, 0, 0, 1000, 2000, 50))
    check("CREATE continuous servo (1000..2000 us, deadband 50)", r is not None and r.ok, str(r))
    check("stopped at create: 1500 us", pulse(link) == 1500, str(pulse(link)))
    for x10, want in ((500, 1775), (-500, 1225), (1000, 2000), (-1000, 1000), (0, 1500)):
        r = servo_target(link, x10)
        check(f"speed {x10 / 10} % -> {want} us", r is not None and r.ok and pulse(link) == want, f"{r} {pulse(link)}")
    r = servo_target(link, 1001)
    check("speed 100.1 % -> ERR_DEV_RANGE", err_cause(r) == "ERR_DEV_RANGE", str(r))
    remove(link, SERVO_ID)
    r = dev(link, SERVO_ID, OP_INFO)
    check("after REMOVE the servo is gone", err_cause(r) == "ERR_DEV_NOT_FOUND", str(r))


def drv_cfg(topology: int, pins: list[bytes], pwm_hz: int, limit_mA: int) -> bytes:
    """drv8962_cfg_t: topology, 15 pins, PWM frequency, 4 RIPROPI (0 = default), the chip's limit, turn_off_at_ocp 1."""
    assert len(pins) == 15
    return bytes([topology]) + b"".join(pins) + struct.pack("<I4IIB", pwm_hz, 0, 0, 0, 0, limit_mA, 1)


# A DRV8962 on a spare id, wired to the free PCA servo channels: IN1 / IN2 = CH 1 / 2, EN1 / EN2 = CH 3 / 4, nothing else
# (no chip behind it). Only for refused creates and the create / remove cycles.
SPARE_DRV_PINS = [pin(PCA_ID, 1), pin(PCA_ID, 2), NONE, NONE, pin(PCA_ID, 3), pin(PCA_ID, 4)] + [NONE] * 9
SPARE_DRV_EN_MASK = (1 << 3) | (1 << 4)
DRV2_EN_MASK = 0xF << 12


def test_drv2(link: Link) -> None:
    """DRV8962 #2 (id 6): IN1-4 = PCA CH 8-11, EN1-4 = PCA CH 12-15, nSLEEP TCA 8, nFAULT TCA 10, VREF = DAC OUT1."""
    r = create(link, SPARE_ID, DRV_TYPE, drv_cfg(2, SPARE_DRV_PINS, PCA_PWM_HZ, DRV_LIMIT_MA))
    check("CREATE DRV8962 with topology 2 -> ERR_DEV_RANGE", err_cause(r) == "ERR_DEV_RANGE", str(r))
    discard(link, SPARE_ID)
    r = create(link, SPARE_ID, DRV_TYPE, drv_cfg(0, SPARE_DRV_PINS, PCA_PWM_HZ, 10001))
    check("CREATE DRV8962 with a 10001 mA limit -> ERR_DEV_RANGE", err_cause(r) == "ERR_DEV_RANGE", str(r))
    discard(link, SPARE_ID)
    r = create(link, SPARE_ID, DRV_TYPE, drv_cfg(0, SPARE_DRV_PINS, 20000, DRV_LIMIT_MA))
    check("CREATE DRV8962 at 20 kHz on the PCA9685 -> fails (PCA tops at 1526 Hz)", r is not None and not r.ok, str(r))
    r = dev(link, SPARE_ID, OP_INFO)
    check("the failed create left nothing registered", err_cause(r) == "ERR_DEV_NOT_FOUND", str(r))
    discard(link, SPARE_ID)
    check("and nothing new locked on the PCA9685", locked(link, PCA_ID) == DRV2_EN_MASK, hex(locked(link, PCA_ID) or 0))

    o = ops(link, DRV2_ID)
    check("OPS: the H-bridge ops, the own ops, ON_EVENT", has_ops(o, {
        HB_DRIVE: SIDE_W, HB_BRAKE: SIDE_W, HB_COAST: SIDE_W, HB_CURRENT: SIDE_R, DRV_CURRENT_LIMIT: SIDE_W,
        DRV_CLEAR_FAULT: SIDE_W, DRV_FAULT: SIDE_R, DRV_SETTINGS: SIDE_R | SIDE_W, EVENT_ON_EVENT: SIDE_W}), show_ops(o))
    check("OPS: no POWER_MONITOR CURRENT without IPROPI pins", o is not None and PMON_CURRENT not in o, show_ops(o))
    r = dev(link, DRV2_ID, PMON_CURRENT, bytes([0]))
    check("POWER_MONITOR CURRENT -> ERR_DEV_UNKNOWN_OP", err_cause(r) == "ERR_DEV_UNKNOWN_OP", str(r))
    r = dev(link, DRV2_ID, DRV_SETTINGS, dir=READ)
    check(f"settings read: 2 full bridges, {DRV_LIMIT_MA} mA, off at OCP", r is not None and r.ok and r.data == struct.pack("<BIB", 0, DRV_LIMIT_MA, 1), str(r))
    check("PCA locks EN1-4 (CH 12-15)", locked(link, PCA_ID) == DRV2_EN_MASK, hex(locked(link, PCA_ID) or 0))
    check("TCA locks nSLEEP (8) and nFAULT (10) next to INT / RST", (locked(link, TCA_ID) or 0) & ((1 << 8) | (1 << 10)) == ((1 << 8) | (1 << 10)), hex(locked(link, TCA_ID) or 0))
    check("nSLEEP high (TCA 8): the chip is awake", tca_level(link, 8) == 1, str(tca_level(link, 8)))
    print("      nFAULT (TCA 10) level:", tca_level(link, 10))
    mv = volts(link, DAC_ID, 1)
    check(f"VREF (DAC OUT1) follows the {DRV_LIMIT_MA} mA limit: {vref_mV(DRV_LIMIT_MA)} mV", mv is not None and abs(mv - vref_mV(DRV_LIMIT_MA)) <= 5, str(mv))
    r = set_level(link, PCA_ID, 12, 0)
    check("IO LEVEL write to the locked EN1 channel -> ERR_DEV_PIN_LOCKED", err_cause(r) == "ERR_DEV_PIN_LOCKED", str(r))

    r = dev(link, DRV2_ID, DRV_FAULT, bytes([0]), dir=READ)
    check("FAULT ch0: no fault {fault 0, reason 0}", r is not None and r.ok and r.data == b"\x00\x00", str(r))
    for drive in (500, -500, 1000, -1000, 4, 0):
        r = dev(link, DRV2_ID, HB_DRIVE, struct.pack("<Bh", 0, steps(drive)))
        check(f"DRIVE ch0 {drive / 10} %", r is not None and r.ok, str(r))

    def lines() -> tuple:  # IN1 IN2 IN3 IN4 EN1 EN2 EN3 EN4 (PCA CH 8..15)
        return tuple(level(link, PCA_ID, c) for c in range(8, 16))

    check("every channel braked (ch1 since boot, ch0 by DRIVE 0): EN1-4 on, IN off", lines() == (0, 0, 0, 0, 1, 1, 1, 1), str(lines()))
    dev(link, DRV2_ID, HB_COAST, bytes([0]))
    dev(link, DRV2_ID, HB_COAST, bytes([1]))
    for name, drive, want in (
        ("forward 80 %: IN1 on, IN2 the complement (off at this level), EN1+2 on", 800, (1, 0, 0, 0, 1, 1, 0, 0)),
        ("reverse 80 %: IN1 the complement, IN2 on", -800, (0, 1, 0, 0, 1, 1, 0, 0)),
        ("0: brake, EN stays on", 0, (0, 0, 0, 0, 1, 1, 0, 0)),
    ):
        dev(link, DRV2_ID, HB_DRIVE, struct.pack("<Bh", 0, steps(drive)))
        check(f"channel 0 {name}", lines() == want, str(lines()))
    dev(link, DRV2_ID, HB_DRIVE, struct.pack("<Bh", 1, steps(800)))
    check("channel 1 forward 80 %: IN3 on, EN3+4 on", lines() == (0, 0, 1, 0, 1, 1, 1, 1), str(lines()))
    dev(link, DRV2_ID, HB_COAST, bytes([0]))
    dev(link, DRV2_ID, HB_COAST, bytes([1]))
    check("COAST both channels: IN off, EN off", lines() == (0,) * 8, str(lines()))
    dev(link, DRV2_ID, HB_BRAKE, bytes([1]))
    check("BRAKE channel 1: EN3+4 on, IN off", lines() == (0, 0, 0, 0, 0, 0, 1, 1), str(lines()))
    dev(link, DRV2_ID, HB_COAST, bytes([1]))
    r = dev(link, DRV2_ID, HB_DRIVE, struct.pack("<Bh", 1, 4096))
    check("DRIVE 4096 (one step above full scale) -> ERR_DEV_RANGE", err_cause(r) == "ERR_DEV_RANGE", str(r))
    r = dev(link, DRV2_ID, HB_DRIVE, struct.pack("<Bh", 2, 100))
    check("channel 2 -> ERR_DEV_BAD_PIN", err_cause(r) == "ERR_DEV_BAD_PIN", str(r))
    for name, op in (("BRAKE", HB_BRAKE), ("COAST", HB_COAST)):
        r = dev(link, DRV2_ID, op, bytes([1]))
        check(f"{name} ch1", r is not None and r.ok, str(r))
    r = dev(link, DRV2_ID, HB_CURRENT, bytes([0]))
    check("CURRENT without an IPROPI pin: 0 mA", i32(r) == 0, str(r))
    r = drv_limit(link, DRV2_ID, 10001)
    check("CURRENT_LIMIT 10001 mA -> ERR_DEV_RANGE", err_cause(r) == "ERR_DEV_RANGE", str(r))
    for limit in (0, 1000):
        r = drv_limit(link, DRV2_ID, limit)
        mv = volts(link, DAC_ID, 1)
        check(f"CURRENT_LIMIT {limit} mA: ACK, VREF {vref_mV(limit)} mV", r is not None and r.ok and r.data == b"" and mv is not None and abs(mv - vref_mV(limit)) <= 5, f"{r} {mv}")
    r = drv_limit(link, DRV2_ID, DRV_LIMIT_MA)
    mv = volts(link, DAC_ID, 1)
    check(f"limit back to {DRV_LIMIT_MA} mA", r is not None and r.ok and mv is not None and abs(mv - vref_mV(DRV_LIMIT_MA)) <= 5, f"{r} {mv}")
    r = dev(link, DRV2_ID, DRV_CLEAR_FAULT, bytes([0]), dir=WRITE)
    check("CLEAR_FAULT (nSLEEP reset pulse)", r is not None and r.ok, str(r))
    check("nSLEEP high again after the pulse", tca_level(link, 8) == 1, str(tca_level(link, 8)))

    r = dev(link, DRV2_ID, OP_SUSPEND)
    check("SUSPEND", r is not None and r.ok, str(r))
    r = dev(link, DRV2_ID, HB_DRIVE, struct.pack("<Bh", 0, 100))
    check("DRIVE while suspended -> ERR_DEV_SUSPENDED", err_cause(r) == "ERR_DEV_SUSPENDED", str(r))
    r = dev(link, DRV2_ID, OP_RESUME)
    check("RESUME", r is not None and r.ok, str(r))
    r = dev(link, DRV2_ID, OP_RESET)
    check("device RESET (every channel braked)", r is not None and r.ok and lines() == (0, 0, 0, 0, 1, 1, 1, 1), f"{r} {lines()}")


def test_drv1(link: Link) -> None:
    """DRV8962 #1 (id 5): IN1-4 = ESP 21/47/48/45 (PWM), EN1-4 = ESP 38/39/2/1, IPROPI1-4 = ESP 7/6/5/4 (ADC), nSLEEP TCA 9, nFAULT TCA 11, VREF = DAC OUT0."""
    o = ops(link, DRV1_ID)
    check("OPS: the H-bridge ops and POWER_MONITOR CURRENT (IPROPI wired)", has_ops(o, {HB_DRIVE: SIDE_W, HB_CURRENT: SIDE_R, PMON_CURRENT: SIDE_R}), show_ops(o))
    mask = locked(link, GPIO_ID) or 0
    check("GPIO ESP locks EN1-4 (38 39 2 1)", all(mask & (1 << p) for p in (38, 39, 2, 1)), hex(mask))
    en = tuple(level(link, GPIO_ID, p) for p in (38, 39, 2, 1))
    check("braked since boot (EN1-4 high)", en == (1, 1, 1, 1), str(en))
    for drive in (300, -300, 0):
        r = dev(link, DRV1_ID, HB_DRIVE, struct.pack("<Bh", 0, steps(drive)))
        check(f"DRIVE ch0 {drive / 10} %", r is not None and r.ok, str(r))
    currents = [i32(dev(link, DRV1_ID, HB_CURRENT, bytes([ch]))) for ch in (0, 1)]
    check("HBRIDGE CURRENT of both channels reads (IPROPI ADC)", all(c is not None for c in currents), str(currents))
    pmon = [i32(dev(link, DRV1_ID, PMON_CURRENT, bytes([ch]))) for ch in (0, 1)]
    check("POWER_MONITOR CURRENT of both channels reads", all(c is not None for c in pmon), str(pmon))
    print("      currents (mA):", currents, pmon)
    r = dev(link, DRV1_ID, PMON_CURRENT, bytes([2]))
    check("POWER_MONITOR CURRENT channel 2 -> ERR_POWER_BAD_CHANNEL", err_cause(r) == "ERR_POWER_BAD_CHANNEL", str(r))
    time.sleep(0.5)
    t0 = link.mark()  # the refused call above leaves its record
    r = dev(link, DRV1_ID, OP_SUSPEND)
    check("SUSPEND", r is not None and r.ok, str(r))
    r = dev(link, DRV1_ID, OP_RESUME)
    check("RESUME", r is not None and r.ok, str(r))
    time.sleep(0.3)
    check("no error records in the DRV8962 #1 section", new_errors(link, t0) == [], str(new_errors(link, t0)))


def test_tca(link: Link) -> None:
    """The TCA6424A's own IO on the board: the status LEDs on 22 / 23 (push-pull, high = on)."""
    r = dev(link, TCA_ID, OP_INFO)
    check("TCA INFO: type 0x0042", r is not None and r.ok and r.data == struct.pack("<HB", TCA_TYPE, 0), str(r))
    r = dev(link, TCA_ID, IO_LEVEL, bytes([23]), dir=READ)
    check("LEVEL read answers {pin, level}", r is not None and r.ok and r.data == bytes([23, 1]), str(r))
    r = dev(link, TCA_ID, IO_MODE, bytes([23, 3]))
    check("MODE LED pin 23 push-pull", r is not None and r.ok, str(r))
    for lv in (1, 0, 1, 0):
        r = set_level(link, TCA_ID, 23, lv)
        check(f"LEVEL write LED 23 = {lv}: ACK, reads back", r is not None and r.ok and r.data == b"" and tca_level(link, 23) == lv, f"{r} {tca_level(link, 23)}")
    r = dev(link, TCA_ID, IO_TOGGLE, bytes([23]))
    check("TOGGLE LED 23 -> 1", r is not None and r.ok and tca_level(link, 23) == 1, f"{r} {tca_level(link, 23)}")
    r = dev(link, TCA_ID, IO_MODE, bytes([23, 4]))
    check("MODE open-drain -> ERR_IO_MODE_UNSUPPORTED", err_cause(r) == "ERR_IO_MODE_UNSUPPORTED", str(r))
    r = dev(link, TCA_ID, IO_MODE, bytes([24, 0]))
    check("pin 24 -> ERR_DEV_BAD_PIN", err_cause(r) == "ERR_DEV_BAD_PIN", str(r))
    r = dev(link, TCA_ID, IO_RESET, bytes([23]))
    check("IO RESET LED 23 (back to an input)", r is not None and r.ok, str(r))
    dev(link, TCA_ID, IO_MODE, bytes([23, 3]))
    r = set_level(link, TCA_ID, 23, 1)
    check("status LED 23 lit again", r is not None and r.ok and tca_level(link, 23) == 1, str(r))


def test_power_chips(link: Link) -> None:
    """TPS55289 x2, INA3221, AP33772S and the two power muxes (the board's). Nothing is switched on: the rails stay
    disabled (EN low) and the muxes keep both LM73100 switches off."""
    for mux_id, (a, b) in MUX_PINS.items():
        check(f"mux {mux_id}: source OFF, both switches low", source(link, mux_id) == 0 and tca_level(link, a) == 0 and tca_level(link, b) == 0,
              f"{source(link, mux_id)} {tca_level(link, a)} {tca_level(link, b)}")
    mask = locked(link, TCA_ID) or 0
    want = sum(1 << p for p in (1, 2, 5, 6, 12, 13, 14, 15, 16, 17, 21))
    check("TCA locks the INT / EN / alert / switch pins of those devices", mask & want == want, hex(mask))
    check("rail EN pins are low: the rails are off", tca_level(link, 16) == 0 and tca_level(link, 17) == 0, f"{tca_level(link, 16)} {tca_level(link, 17)}")

    volts_ = [i32(dev(link, INA_ID, PMON_VOLTAGE, bytes([ch]))) for ch in range(3)]
    amps = [i32(dev(link, INA_ID, PMON_CURRENT, bytes([ch]))) for ch in range(3)]
    watts = [i32(dev(link, INA_ID, PMON_POWER, bytes([ch]))) for ch in range(3)]
    print("      INA3221 channels 0..2 (rail B, input, rail A): mV", volts_, "mA", amps, "mW", watts)
    check("INA3221 reads the three channels (voltage, current, power)", all(v is not None for v in volts_ + amps + watts), f"{volts_} {amps} {watts}")
    check("board input (channel 1) between 3 V and 21 V", volts_[1] is not None and 3000 <= volts_[1] <= 21000, str(volts_[1]))
    r = dev(link, INA_ID, PMON_VOLTAGE, bytes([3]))
    check("INA3221 channel 3 -> an error", r is not None and not r.ok, str(r))

    default = bytes([0, 1, 7, 0])  # ocp_delay 0, hiccup, all faults, no discharge
    for dev_id in (TPS_A_ID, TPS_B_ID):
        r = dev(link, dev_id, TPS_PROTECTION, dir=READ)
        check(f"TPS {dev_id}: default protection (0, hiccup, all faults, no discharge)", r is not None and r.ok and r.data == default, str(r))
        r = dev(link, dev_id, TPS_PROTECTION, bytes([2, 0, 3, 1]), dir=WRITE)
        r2 = dev(link, dev_id, TPS_PROTECTION, dir=READ)
        check(f"TPS {dev_id}: protection write kept while the rail is off", r is not None and r.ok and r2 is not None and r2.data == bytes([2, 0, 3, 1]), f"{r} {r2}")
        r = dev(link, dev_id, TPS_PROTECTION, bytes([4, 0, 3, 0]), dir=WRITE)
        check(f"TPS {dev_id}: ocp_delay 4 -> ERR_DEV_RANGE", err_cause(r) == "ERR_DEV_RANGE", str(r))
        r = dev(link, dev_id, OP_RESET)
        r2 = dev(link, dev_id, TPS_PROTECTION, dir=READ)
        check(f"TPS {dev_id}: RESET restores the default protection", r is not None and r.ok and r2 is not None and r2.data == default, f"{r} {r2}")

    r = dev(link, AP_ID, PD_OPTIONS)
    check("AP33772S OPTIONS answers (rows of 8 bytes)", r is not None and r.ok and len(r.data) % 8 == 0, str(r))
    if r is not None and r.ok and len(r.data) % 8 == 0:
        print("      AP33772S options (slot, type, min mV, max mV, max mA):", list(struct.iter_unpack("<BBHHH", r.data)))
    r = dev(link, AP_ID, PD_LIMITS)
    check("AP33772S LIMITS answers {voltage_mV, current_mA}", r is not None and r.ok and len(r.data) == 8, str(r))
    if r is not None and r.ok and len(r.data) == 8:
        print("      AP33772S limits (mV, mA):", struct.unpack("<ii", r.data))


def test_safe_state(link: Link) -> None:
    """STOP (the safe state, sys_system) suspends every device, newest first; each one is resumed, oldest first."""
    r = create(link, SERVO_ID, SERVO_TYPE, servo_cfg(pin(PCA_ID, 0), 0, 0, 0, 0, 180, 90, 500, 2500, 0))
    check("safe state: servo created", r is not None and r.ok, str(r))
    set_volts(link, DAC_ID, 0, 1500)
    dev(link, DRV2_ID, HB_DRIVE, struct.pack("<Bh", 0, 600))
    order = [d for d, _t, _n in BOOT_CHIPS] + [SERVO_ID]  # creation order

    def suspended(dev_id: int) -> int | None:
        r = dev(link, dev_id, OP_INFO)
        return r.data[2] if r is not None and r.ok and len(r.data) == 3 else None

    check("all devices running before STOP", all(suspended(d) == 0 for d in order), str([suspended(d) for d in order]))
    t = link.mark()
    r = link.request(bytes([KEY_SYS, SYS_STOP]), urgent=True)
    check("STOP answered", r is not None and r.ok, str(r))
    check("STOP runs the safe-state handler", link.wait_log("safe state", 1.0, since=t))
    time.sleep(0.3)
    check("every device is suspended", all(suspended(d) == 1 for d in order), str([suspended(d) for d in order]))
    check("SAFE: reason STOP", safe_reason(link) == SAFE_STOP, str(safe_reason(link)))
    r = dev(link, DRV2_ID, HB_DRIVE, struct.pack("<Bh", 0, 100))
    check("the bridge refuses a drive", err_cause(r) == "ERR_DEV_SUSPENDED", str(r))
    lv = tuple(level(link, PCA_ID, c) for c in range(8, 16))
    check("bridge lines all low (coasted before the PCA9685 slept)", lv == (0,) * 8, str(lv))
    check("servo: no pulse", pulse(link) == 0, str(pulse(link)))
    for d in order:
        r = dev(link, d, OP_RESUME)
        check(f"RESUME {d}", r is not None and r.ok, str(r))
    check("every device is running again", all(suspended(d) == 0 for d in order), str([suspended(d) for d in order]))
    v = volts(link, DAC_ID, 0)
    check("DAC kept 1.5 V", abs((v or 0) - 1500) <= 4, str(v))
    check("the status LED is on again after the TCA6424A resumed", tca_level(link, 23) == 1, str(tca_level(link, 23)))
    check("servo: the last pulse is back (1500 us)", pulse(link) == 1500, str(pulse(link)))
    r = dev(link, DRV2_ID, HB_DRIVE, struct.pack("<Bh", 0, 100))
    check("the bridge drives again", r is not None and r.ok, str(r))
    r = link.request(bytes([KEY_SYS, SYS_SAFE_LEAVE]))
    check("SAFE_LEAVE clears the note (SAFE reason 0)", r is not None and r.ok and safe_reason(link) == 0, f"{r} {safe_reason(link)}")
    r = dev(link, DRV2_ID, OP_RESET)
    check("DRV8962 #2 braked again (RESET)", r is not None and r.ok, str(r))
    restore_vref(link)
    remove(link, SERVO_ID)


def test_policy_and_cycles(link: Link) -> None:
    """The per-device error policy, and create / remove cycles that would leak a lock or a dependency."""
    r = dev(link, DAC_ID, OP_ERROR_POLICY, dir=READ)
    old = r.data if r is not None and r.ok and len(r.data) == 17 else None
    check("DAC ERROR_POLICY read: LOW, no actions", old == struct.pack("<B4I", 1, 0, 0, 0, 0), str(r))
    r = dev(link, DAC_ID, OP_ERROR_POLICY, struct.pack("<B4I", 3, 0, 0, 0, 0), dir=WRITE)
    r2 = dev(link, DAC_ID, OP_ERROR_POLICY, dir=READ)
    check("DAC ERROR_POLICY write: ACK, kept", r is not None and r.ok and r.data == b"" and r2 is not None and r2.data == struct.pack("<B4I", 3, 0, 0, 0, 0), f"{r} {r2}")
    dev(link, DAC_ID, OP_ERROR_POLICY, old or struct.pack("<B4I", 1, 0, 0, 0, 0), dir=WRITE)

    bad = 0
    for i in range(40):
        r = create(link, SPARE_ID, DRV_TYPE, drv_cfg(0, SPARE_DRV_PINS, PCA_PWM_HZ, DRV_LIMIT_MA))
        mask = locked(link, PCA_ID) if i == 0 else None
        r2 = dev(link, SPARE_ID, OP_REMOVE)
        if i == 0:
            check("spare DRV8962 locks its EN pins (PCA CH 3 / 4)", mask == DRV2_EN_MASK | SPARE_DRV_EN_MASK, hex(mask or 0))
        if r is None or not r.ok or r2 is None or not r2.ok:
            bad += 1
            print("      cycle", i, r, r2)
            break
    check("40 create / remove cycles of a DRV8962 on the PCA9685 (locks, dependencies)", bad == 0)
    check("nothing stays locked on the PCA9685 after the cycles", locked(link, PCA_ID) == DRV2_EN_MASK, hex(locked(link, PCA_ID) or 0))
    bad = 0
    for i in range(40):
        r = create(link, SERVO_ID, SERVO_TYPE, servo_cfg(pin(PCA_ID, 0), 0, 0, 0, 0, 180, 90, 500, 2500, 0))
        r2 = dev(link, SERVO_ID, OP_REMOVE)
        if r is None or not r.ok or r2 is None or not r2.ok:
            bad += 1
            print("      cycle", i, r, r2)
            break
    check("40 create / remove cycles of the servo", bad == 0)


def power_status(link: Link) -> dict | None:
    r = pm(link, PW_STATUS)
    if r is None or not r.ok or len(r.data) != 40:
        return None
    f = struct.unpack("<BiiIIIIIBBIIB", r.data)
    keys = ("source", "input_mV", "input_mA", "source_mV", "source_mA", "budget_mW", "limit_mW", "allocated_mW", "battery_pct",
            "flags", "pd_mV", "pd_mA", "pd_type")
    return dict(zip(keys, f))


def test_boot_chips(link: Link, label: str = "boot") -> None:
    """Every onboard chip exists with its type, nothing is energised, the lines have their idle levels."""
    for dev_id, type_id, name in BOOT_CHIPS:
        r = dev(link, dev_id, OP_INFO)
        check(f"{label}: {name} (id {dev_id}) is there, type {type_id:#04x}, running",
              r is not None and r.ok and r.data == struct.pack("<HB", type_id, 0), str(r))
    check(f"{label}: status LED (TCA 23) on", tca_level(link, 23) == 1, str(tca_level(link, 23)))
    for mux_id in (MUX1_ID, MUX2_ID):
        a, b = MUX_PINS[mux_id]
        check(f"{label}: power mux {mux_id} off, both LM73100 switches low", source(link, mux_id) == 0 and tca_level(link, a) == 0 and tca_level(link, b) == 0,
              f"{source(link, mux_id)} {tca_level(link, a)} {tca_level(link, b)}")
    check(f"{label}: both rails disabled (EN low)", tca_level(link, 16) == 0 and tca_level(link, 17) == 0, f"{tca_level(link, 16)} {tca_level(link, 17)}")
    mask = locked(link, PCA_ID)
    check(f"{label}: PCA9685 CH 12..15 (DRV8962 #2 EN) locked", mask == DRV2_EN_MASK, hex(mask or 0))
    lines = tuple(level(link, PCA_ID, c) for c in range(8, 16))
    check(f"{label}: DRV8962 #2 braked (IN low, EN high)", lines == (0, 0, 0, 0, 1, 1, 1, 1), str(lines))
    en = tuple(level(link, GPIO_ID, p) for p in (38, 39, 2, 1))
    check(f"{label}: DRV8962 #1 braked (EN1-4 on ESP 38 39 2 1 high)", en == (1, 1, 1, 1), str(en))
    vref = [volts(link, DAC_ID, ch) for ch in (0, 1)]
    want = vref_mV(DRV_LIMIT_MA)
    check(f"{label}: VREF of both bridges follows the {DRV_LIMIT_MA} mA limit ({want} mV)", all(v is not None and abs(v - want) <= 5 for v in vref), str(vref))
    st = power_status(link)
    check(f"{label}: power manager answers", st is not None, str(st))
    print("      power manager:", st)
    volts_ = [i32(dev(link, INA_ID, PMON_VOLTAGE, bytes([ch]))) for ch in range(3)]
    amps = [i32(dev(link, INA_ID, PMON_CURRENT, bytes([ch]))) for ch in range(3)]
    print("      INA3221 channels 0..2 (rail B, input, rail A): mV", volts_, "mA", amps)
    check(f"{label}: board input (INA channel 1) 3..21 V", volts_[1] is not None and 3000 <= volts_[1] <= 21000, str(volts_))
    check(f"{label}: the power manager sees the same input", st is not None and volts_[1] is not None and abs(st["input_mV"] - volts_[1]) < 300, f"{st} {volts_}")
    check(f"{label}: ADS7128 reads its eight inputs",
          all(v is not None and 0 <= v <= 20000 for v in [volts(link, ADS_ID, c) for c in range(8)]))
    time.sleep(0.5)
    t = link.mark()
    time.sleep(1.0)
    check(f"{label}: no error records at idle", new_errors(link, t) == [], str(new_errors(link, t))[:300])


def test_board_devices_kept(link: Link) -> None:
    """The board's own devices can't be removed (ERR_DEV_SYSTEM, checked before anything is released)."""
    for dev_id, _t, name in reversed(BOOT_CHIPS):
        r = dev(link, dev_id, OP_REMOVE)
        check(f"REMOVE {name} (id {dev_id}) -> ERR_DEV_SYSTEM, still there", err_cause(r) == "ERR_DEV_SYSTEM" and exists(link, dev_id), str(r))


def test_rails(link: Link) -> None:
    """Each TPS55289 rail through the power manager (the POWER_VREG ops are guarded by its budget): 5 V at the
    minimum current, read back by the INA3221, then off. The muxes stay off: the bridges are not energised."""
    for rail_id, name, en_pin in ((TPS_A_ID, "rail A", 17), (TPS_B_ID, "rail B", 16)):
        t = link.mark()
        r = dev(link, rail_id, VREG_VOLTAGE, struct.pack("<I", 5000))
        check(f"{name}: VOLTAGE 5000 mV", r is not None and r.ok, str(r))
        r = dev(link, rail_id, VREG_CURRENT, struct.pack("<I", 200))
        check(f"{name}: CURRENT 200 mA", r is not None and r.ok, str(r))
        r = dev(link, rail_id, VREG_ENABLE, bytes([1]))
        check(f"{name}: ENABLE on (returns at once, the rise runs in a timer)", r is not None and r.ok, str(r))
        time.sleep(1.5)
        mv = i32(dev(link, INA_ID, PMON_VOLTAGE, bytes([INA_CH[rail_id]])))
        mA = i32(dev(link, INA_ID, PMON_CURRENT, bytes([INA_CH[rail_id]])))
        print(f"      {name}: {mv} mV {mA} mA, EN pin {tca_level(link, en_pin)}")
        check(f"{name}: EN high and the INA3221 reads about 5 V", tca_level(link, en_pin) == 1 and mv is not None and 4500 <= mv <= 5500, f"{mv} {tca_level(link, en_pin)}")
        st = power_status(link)
        check(f"{name}: the manager allocated power to it", st is not None and st["allocated_mW"] > 0, str(st))
        r = dev(link, rail_id, VREG_ENABLE, bytes([0]))
        check(f"{name}: ENABLE off", r is not None and r.ok, str(r))
        time.sleep(0.5)
        check(f"{name}: EN low again", tca_level(link, en_pin) == 0, str(tca_level(link, en_pin)))
        errs = [e for e in new_errors(link, t) if "PWRM_FAULT" in e or "OVP" in e or "SCP" in e]
        check(f"{name}: no fault reported", errs == [], str(errs)[:300])
    r = dev(link, TPS_A_ID, TPS_PROTECTION, bytes([0, 1, 7, 0]), dir=WRITE)
    check("protection back to the default", r is not None and r.ok, str(r))


def test_error_transport(link: Link) -> None:
    """The errors stream over the console UART: a command error is the response record and one record on the stream
    (same chain), a long request is cut and flagged, a burst loses nothing, an action step's failure and the device
    policy's response leave records, a failing policy action does not loop."""
    log_set(link, *LOG_DEFAULT)
    t = link.mark()
    r = dev(link, GPIO_ID, IO_MODE, bytes([22, 3]))  # ERR_DEV_BAD_PIN
    time.sleep(0.4)
    recs = [x for x in link.error_records_since(t) if x.src == 1 and r is not None and x.seq == r.seq]
    check("device error: one record on the stream", r is not None and not r.ok and len(recs) == 1, f"{r} {len(recs)}")
    if recs and r is not None and r.record:
        check("device error: stream chain == response chain, request echoed (key, write side)",
              recs[0].names() == r.record.names() and recs[0].request[:2] == bytes([KEY_DEVICE, WRITE]),
              f"{recs[0].names()} {r.record.names()} {recs[0].request.hex(' ')}")

    unknown = frame(bytes([0x7E]))  # an unknown root key (goes as a write)
    t = link.mark()
    seq = link.next_seq()
    link.send_frame(bytes([seq, 0x00]) + unknown + bytes(range(1, 120)))
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
            link.send_frame(bytes([seqs[-1], 0x00]) + unknown)
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

    # Frames stored in an action carry their direction byte.
    rt.act(link, rt.A_REMOVE, rt.u32(210))
    r = rt.upload(link, 210, rt.image(unknown))
    check("upload an action that fails (unknown root key)", r is not None and r.ok, str(r))
    t = link.mark()
    rt.act(link, rt.A_RUN, rt.u32(210))
    time.sleep(0.8)
    recs = link.error_records_since(t)
    check("a failing action step: the record names ERR_ACTION_STEP and the cause",
          any("ERR_ACTION_STEP" in x.names() and "ERR_ROUTER_UNKNOWN_KEY" in x.names() for x in recs), str([x.names() for x in recs]))
    rt.act(link, rt.A_REMOVE, rt.u32(210))

    # The device policy on the GPIO (MEDIUM: a HIGH error runs the HIGH action); a failing action is not run again.
    old = dev(link, GPIO_ID, OP_ERROR_POLICY, dir=READ)
    old = old.data if old is not None and old.ok else struct.pack("<B4I", 1, 0, 0, 0, 0)
    rt.act(link, rt.A_REMOVE, rt.u32(200))
    r = rt.upload(link, 200, rt.image(frame(bytes([KEY_LOG, LOG_CONFIG]), WRITE) + bytes([2, 1, 1, 1, 1])))
    check("policy action uploaded", r is not None and r.ok, str(r))
    dev(link, GPIO_ID, OP_ERROR_POLICY, struct.pack("<B4I", rt.IMP_MEDIUM, 0, 0, 200, 0), dir=WRITE)
    log_set(link, *LOG_DEFAULT)
    dev(link, GPIO_ID, IO_MODE, bytes([22, 3]))
    time.sleep(0.6)
    check("policy: a HIGH error runs the action (log level 2)", (log_get(link) or (0,))[0] == 2, str(log_get(link)))
    rt.act(link, rt.A_REMOVE, rt.u32(200))
    r = rt.upload(link, 200, rt.image(unknown))
    t = link.mark()
    dev(link, GPIO_ID, IO_MODE, bytes([22, 3]))
    time.sleep(1.2)
    recs = link.error_records_since(t)
    check("a failing policy action: the device record and the action's, no loop", 2 <= len(recs) <= 6 and rt.ping(link) is not None, f"{len(recs)}")
    dev(link, GPIO_ID, OP_ERROR_POLICY, old, dir=WRITE)
    rt.act(link, rt.A_REMOVE, rt.u32(200))
    log_set(link, *LOG_DEFAULT)


def test_restart_boot(link: Link) -> None:
    """A restart brings every chip back by itself, also after the safe state (STOP): the TCA6424A is then held in
    reset, and RST stays low across the ESP restart (the create must release it before probing the chip)."""
    r = link.request(bytes([KEY_SYS, SYS_STOP]), urgent=True)
    check("STOP before the restart", r is not None and r.ok, str(r))
    time.sleep(0.5)
    t = link.mark()
    r = link.request(bytes([KEY_SYS, SYS_RESTART]))
    check("RESTART answered", r is not None and r.ok, str(r))
    check("board boots again", link.wait_log("runIT up (0 boot step(s) failed)", 25.0, since=t))
    time.sleep(1.0)
    check("after restart: not in the safe state", safe_reason(link) == 0, str(safe_reason(link)))
    test_boot_chips(link, "after restart")


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
    r = pm(link, PW_CONFIG, dir=READ)
    pw_cfg = r.data if r is not None and r.ok and len(r.data) == 4 else bytes([1, 1, 20, 0])  # {enabled, strict, margin, source}
    check("power manager CONFIG read", r is not None and r.ok and len(r.data) == 4, str(r))

    def inp() -> tuple:
        return (i32(dev(link, INA_ID, PMON_VOLTAGE, bytes([1]))), i32(dev(link, INA_ID, PMON_CURRENT, bytes([1]))))

    def all_off() -> None:
        for mux_id, drv_id, _nf, rail_id, _ch, _en in DRY_BRIDGES:
            dev(link, drv_id, HB_COAST, bytes([0]))
            dev(link, drv_id, HB_COAST, bytes([1]))
            select(link, mux_id, SRC_OFF)
            dev(link, rail_id, VREG_ENABLE, bytes([0]))

    base_mV, base_mA = inp()
    print(f"      input before: {base_mV} mV {base_mA} mA")
    try:
        for mux_id, drv_id, nfault, rail_id, ina_ch, en_pin in DRY_BRIDGES:
            a, b = MUX_PINS[mux_id]
            name = f"bridge {drv_id}"
            # Source A: the board input
            r = select(link, mux_id, SRC_A)
            check(f"{name}: mux {mux_id} SOURCE A", r is not None and r.ok, str(r))
            time.sleep(0.4)
            check(f"{name}: source A on, only the A switch high (TCA {a})", source(link, mux_id) == SRC_A and tca_level(link, a) == 1 and tca_level(link, b) == 0,
                  f"{source(link, mux_id)} {tca_level(link, a)} {tca_level(link, b)}")
            check(f"{name}: nFAULT stays high with VM from the input", tca_level(link, nfault) == 1, str(tca_level(link, nfault)))
            mV, mA = inp()
            print(f"      {name} on A: input {mV} mV {mA} mA")
            check(f"{name}: the input holds up (>= 4.5 V)", mV is not None and mV >= 4500, str((mV, mA)))
            for ch, drive in ((0, 300), (0, -300), (1, 300), (1, -300)):
                r = dev(link, drv_id, HB_DRIVE, struct.pack("<Bh", ch, drive))
                time.sleep(0.25)
                cur = i32(dev(link, drv_id, HB_CURRENT, bytes([ch])))
                ok_fault = tca_level(link, nfault) == 1
                check(f"{name}: ch{ch} drive {drive / 10:+.0f} % with VM on A (no load): accepted, nFAULT high", r is not None and r.ok and ok_fault, f"{r} nFAULT={tca_level(link, nfault)} {cur}")
                print(f"        current {cur} mA, input {inp()}")
            dev(link, drv_id, HB_BRAKE, bytes([0]))
            dev(link, drv_id, HB_BRAKE, bytes([1]))
            r = select(link, mux_id, SRC_OFF)
            check(f"{name}: SOURCE OFF", r is not None and r.ok and tca_level(link, a) == 0 and tca_level(link, b) == 0, str(r))

            # Source B: its regulator rail at 5 V
            dev(link, rail_id, VREG_VOLTAGE, struct.pack("<I", 5000))
            dev(link, rail_id, VREG_CURRENT, struct.pack("<I", 200))
            r = dev(link, rail_id, VREG_ENABLE, bytes([1]))
            check(f"{name}: rail {rail_id} enabled", r is not None and r.ok, str(r))
            time.sleep(1.5)
            r = select(link, mux_id, SRC_B)
            check(f"{name}: mux {mux_id} SOURCE B", r is not None and r.ok, str(r))
            time.sleep(0.4)
            check(f"{name}: source B on, only the B switch high (TCA {b})", source(link, mux_id) == SRC_B and tca_level(link, b) == 1 and tca_level(link, a) == 0,
                  f"{source(link, mux_id)} {tca_level(link, a)} {tca_level(link, b)}")
            rail_mV = i32(dev(link, INA_ID, PMON_VOLTAGE, bytes([ina_ch])))
            check(f"{name}: the rail holds about 5 V with the bridge on it", rail_mV is not None and 4300 <= rail_mV <= 5500 and tca_level(link, nfault) == 1, f"{rail_mV} nFAULT={tca_level(link, nfault)}")
            for ch, drive in ((0, 500), (1, -500)):
                r = dev(link, drv_id, HB_DRIVE, struct.pack("<Bh", ch, drive))
                time.sleep(0.25)
                check(f"{name}: ch{ch} drive {drive / 10:+.0f} % with VM on B (no load)", r is not None and r.ok and tca_level(link, nfault) == 1, f"{r} nFAULT={tca_level(link, nfault)}")
            dev(link, drv_id, HB_COAST, bytes([0]))
            dev(link, drv_id, HB_COAST, bytes([1]))
            select(link, mux_id, SRC_OFF)
            dev(link, rail_id, VREG_ENABLE, bytes([0]))
            time.sleep(0.4)

        # Everything on at once: both rails, both muxes on their rails, all channels driven. The budget of a plain 5 V USB
        # source (assumed 1 A, minus the ESP32's reserve) fits one rail at 5 V / 200 mA, not two: the manager refuses the
        # second (ERR_PWRM_BUDGET_EXCEEDED, checked below). Its checks are switched off for this step only.
        r = dev(link, TPS_A_ID, VREG_ENABLE, bytes([1]))
        r2 = dev(link, TPS_B_ID, VREG_ENABLE, bytes([1]))
        check("budget: with one rail on, the second 5 V rail is refused", r is not None and r.ok and err_cause(r2) == "ERR_PWRM_BUDGET_EXCEEDED", f"{r} {r2}")
        dev(link, TPS_A_ID, VREG_ENABLE, bytes([0]))
        time.sleep(0.5)
        r = pm(link, PW_CONFIG, bytes([0]) + pw_cfg[1:], dir=WRITE)
        check("power manager checks off for the all-on step", r is not None and r.ok, str(r))
        for mux_id, drv_id, nfault, rail_id, ina_ch, en_pin in DRY_BRIDGES:
            dev(link, rail_id, VREG_VOLTAGE, struct.pack("<I", 5000))
            dev(link, rail_id, VREG_CURRENT, struct.pack("<I", 200))
            r = dev(link, rail_id, VREG_ENABLE, bytes([1]))
            check(f"all on: rail {rail_id} enabled", r is not None and r.ok, str(r))
        time.sleep(1.5)
        for mux_id, drv_id, nfault, rail_id, ina_ch, en_pin in DRY_BRIDGES:
            r = select(link, mux_id, SRC_B)
            check(f"all on: mux {mux_id} SOURCE B", r is not None and r.ok, str(r))
            for ch, drive in ((0, 300), (1, 300)):
                dev(link, drv_id, HB_DRIVE, struct.pack("<Bh", ch, drive))
        time.sleep(1.0)
        mV, mA = inp()
        st = power_status(link)
        print(f"      everything on: input {mV} mV {mA} mA, power manager {st}")
        check("all on: both rails still ~5 V, no nFAULT", all(
            4300 <= (i32(dev(link, INA_ID, PMON_VOLTAGE, bytes([ch]))) or 0) <= 5500 for ch in (0, 2)) and tca_level(link, 10) == 1 and tca_level(link, 11) == 1,
              f"{tca_level(link, 10)} {tca_level(link, 11)}")
        check("all on: the input holds up", mV is not None and mV >= 4300, str((mV, mA)))
    finally:
        all_off()
        pm(link, PW_CONFIG, pw_cfg, dir=WRITE)  # the config read at the start
    time.sleep(0.8)
    check("everything off again: switches low, EN low", all(tca_level(link, p) == 0 for p in (12, 13, 14, 15, 16, 17)), str([tca_level(link, p) for p in (12, 13, 14, 15, 16, 17)]))
    for mux_id in (MUX1_ID, MUX2_ID):
        check(f"mux {mux_id} off", source(link, mux_id) == SRC_OFF, str(source(link, mux_id)))
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
        if not board_ready(link):
            print("the onboard chips are missing (not the PCB firmware?), stopping")
            return 1
        if "boot" not in skip:
            test_boot_chips(link)
            test_board_devices_kept(link)
            if "rails" not in skip:
                test_rails(link)
            if a.energise:
                test_dry_power(link)
        if "dac" not in skip:
            test_dac(link)
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
        if "boot" not in skip:
            test_restart_boot(link)

    failed = [r for r in rt.RESULTS if not r[1]]
    print(f"\n{len(rt.RESULTS) - len(failed)}/{len(rt.RESULTS)} passed")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
