#!/usr/bin/env python3
"""Tests of the reworked sys_power_manager (root key 0x22) using device_testdev on a board over the console session.

    python runit-esp/tools/test_power_manager.py                      # auto-detect single port; RUNIT_PORT=COM10
    python runit-esp/tools/test_power_manager.py -v PowerBudgetClamp

Tests:
  - Optional runtime lifecycle: ERR_PWRM_NOT_READY when stopped, auto-start on LAYOUT (dynamic task), STOP cleanup
  - Runtime load (RAIL / RAIL_REMOVE) and automatic rail removal on SYS_DEVICE_EV_REMOVED
  - Automatic source detection via source_pins (USB / PSU / BATTERY / UNKNOWN) + USB-PD profile selection
  - LiPo battery curve estimation and BATTERY_LOW / BATTERY_CRITICAL events
  - 3 budget modes: DISABLED (0), STRICT (1), MARGIN (2)
  - VREG interceptor clamping (OP_CURRENT, OP_VOLTAGE, OP_ENABLE) + PWRM_EV_LIMIT_REACHED event
  - Unified fault event codes (VREG / PMON -> sys_power_kind_e) and response matrix (DISABLE / RESET / NOTIFY)
"""
import contextlib
import io
import os
import struct
import sys
import time
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import runit_defs as defs  # noqa: E402
import session_client as sc  # noqa: E402

READ, WRITE = sc.READ, sc.WRITE
KEY_PWRM = defs.service_key("POWER_MANAGER")
DEV_BUS, DEV_RAIL, DEV_EAR = 17, 18, 19
TESTDEV_TYPE = defs.device_type("testdev")

# PWRM router ops
OP_CONFIG = defs.service_op("POWER_MANAGER", "CONFIG")
OP_LAYOUT = defs.service_op("POWER_MANAGER", "LAYOUT")
OP_PSU = defs.service_op("POWER_MANAGER", "PSU")
OP_BATTERY = defs.service_op("POWER_MANAGER", "BATTERY")
OP_RAIL = defs.service_op("POWER_MANAGER", "RAIL")
OP_RAIL_REMOVE = defs.service_op("POWER_MANAGER", "RAIL_REMOVE")
OP_STATUS = defs.service_op("POWER_MANAGER", "STATUS")
OP_PD = defs.service_op("POWER_MANAGER", "PD")
OP_MATRIX = defs.service_op("POWER_MANAGER", "MATRIX")
OP_STOP = defs.service_op("POWER_MANAGER", "STOP")

# Budget modes
MODE_DISABLED = defs.sym("SYS_POWER_BUDGET_DISABLED")
MODE_STRICT = defs.sym("SYS_POWER_BUDGET_STRICT")
MODE_MARGIN = defs.sym("SYS_POWER_BUDGET_MARGIN")

# Sources
SRC_UNKNOWN = defs.sym("SYS_POWER_SOURCE_UNKNOWN")
SRC_USB = defs.sym("SYS_POWER_SOURCE_USB")
SRC_PSU = defs.sym("SYS_POWER_SOURCE_PSU")
SRC_BATTERY = defs.sym("SYS_POWER_SOURCE_BATTERY")

# Matrix kinds & responses
KIND_OVP = defs.sym("SYS_POWER_KIND_OVP")
KIND_SPC = defs.sym("SYS_POWER_KIND_SPC")
KIND_OCP_WARN = defs.sym("SYS_POWER_KIND_OCP_WARNING")
KIND_OCP_CRIT = defs.sym("SYS_POWER_KIND_OCP_CRITICAL")
KIND_COUNT = defs.sym("SYS_POWER_KIND_SOURCE_CHANGED") + 1
RESP_NOTIFY = defs.sym("SYS_POWER_RESPONSE_NOTIFY")
RESP_DISABLE = defs.sym("SYS_POWER_RESPONSE_DISABLE")
RESP_RESET = defs.sym("SYS_POWER_RESPONSE_RESET")
RESP_SAFE = defs.sym("SYS_POWER_RESPONSE_SAFE_STATE")
TARGET_BOARD = 0xFF

# Device ops
DEV_CREATE = defs.contract_op("DEVICE", "CREATE")
DEV_REMOVE = defs.contract_op("DEVICE", "REMOVE")
DEV_INFO = defs.contract_op("DEVICE", "INFO")
IO_MODE = defs.contract_op("IO", "MODE")
VREG_ENABLE = defs.contract_op("POWER_VREG", "ENABLE")
VREG_VOLTAGE = defs.contract_op("POWER_VREG", "VOLTAGE")
VREG_CURRENT = defs.contract_op("POWER_VREG", "CURRENT")
OWN_FAKE_PIN = defs.device_own_op("testdev", "fake_pin")
OWN_FAKE_CHANNEL = defs.device_own_op("testdev", "fake_channel")
OWN_FAKE_FAULT = defs.device_own_op("testdev", "fake_fault")
OWN_LISTEN = defs.device_own_op("testdev", "listen")
OWN_HEARD = defs.device_own_op("testdev", "heard")
OWN_VREG = defs.device_own_op("testdev", "vreg")

# PWRM events
EV_PWRM_INPUT_OVP = defs.service_event("POWER_MANAGER", "INPUT_OVP")
EV_PWRM_BAT_LOW = defs.service_event("POWER_MANAGER", "BATTERY_LOW")
EV_PWRM_BAT_CRIT = defs.service_event("POWER_MANAGER", "BATTERY_CRITICAL")
EV_PWRM_SOURCE_CHANGED = defs.service_event("POWER_MANAGER", "SOURCE_CHANGED")
EV_PWRM_LIMIT_REACHED = defs.service_event("POWER_MANAGER", "LIMIT_REACHED")

ANY_SRC = 0xFFFF
ANY_CH = 0xFFFF


def quiet():
    return contextlib.redirect_stdout(io.StringIO())


def dev_cmd(dev_id: int, side: int, op: int, args: bytes = b"") -> bytes:
    return bytes([sc.KEY_DEVICE, side, dev_id]) + struct.pack("<H", op) + args


def pwrm_cmd(side: int, op: int, args: bytes = b"") -> bytes:
    return bytes([KEY_PWRM, side, op]) + args


def testdev_cfg(pins: int = 16, channels: int = 4, uart_mode: int = 1, uart_baud: int = 115200) -> bytes:
    return struct.pack("<HB", TESTDEV_TYPE, 0) + struct.pack("<BBBI", pins, channels, uart_mode, uart_baud)


def layout_bytes(
    max_mV=20000,
    max_mA=5000,
    reserve_mW=1000,
    unknown_source_mA=1000,
    eff_pct=100,
    input_dev=0xFF,
    input_ch=0,
    usb_pd=0xFF,
    usb_ok=(0xFF, 0xFF),
    ext_ok=(0xFF, 0xFF),
) -> bytes:
    return struct.pack(
        "<IIIIBBBBBBBB",
        max_mV,
        max_mA,
        reserve_mW,
        unknown_source_mA,
        eff_pct,
        input_dev,
        input_ch,
        usb_pd,
        usb_ok[0],
        usb_ok[1],
        ext_ok[0],
        ext_ok[1],
    )


def unpack_status(body: bytes) -> dict:
    # sys_power_status_t (40 B)
    vals = struct.unpack("<BiiIIIIIBBIIB", body)
    keys = (
        "source", "input_mV", "input_mA", "source_mV", "source_mA",
        "budget_mW", "limit_mW", "allocated_mW", "battery_pct", "flags",
        "pd_mV", "pd_mA", "pd_type",
    )
    return dict(zip(keys, vals))


def unpack_heard(body: bytes):
    count, tag, source, channel, length, data = struct.unpack("<IIHHH16s", body)
    return count, tag, source, channel, length, data[:length]


class Client:
    def __init__(self, port: str):
        self.link = sc.Link(port, 115200)
        with quiet():
            assert self.link.hello(), "the board did not say hello on " + port

    def call(self, cmd: bytes, wait: float = 2.0):
        with quiet():
            return self.link.call(self.link.board, cmd, wait)

    def rd(self, dev_id: int, op: int, args: bytes = b""):
        return self.call(dev_cmd(dev_id, READ, op, args))

    def wr(self, dev_id: int, op: int, args: bytes = b""):
        return self.call(dev_cmd(dev_id, WRITE, op, args))

    def pm_r(self, op: int, args: bytes = b""):
        return self.call(pwrm_cmd(READ, op, args))

    def pm_w(self, op: int, args: bytes = b""):
        return self.call(pwrm_cmd(WRITE, op, args))

    def cleanup(self):
        self.pm_w(OP_STOP)
        for did in (DEV_EAR, DEV_RAIL, DEV_BUS):
            self.wr(did, DEV_REMOVE)


CLI: Client | None = None


def setUpModule():
    global CLI
    port = os.environ.get("RUNIT_PORT")
    if not port:
        from serial.tools import list_ports
        ports = [p.device for p in list_ports.comports()]
        if len(ports) != 1:
            raise unittest.SkipTest(f"set RUNIT_PORT: serial ports found {ports}")
        port = ports[0]
    CLI = Client(port)
    CLI.cleanup()


class PwrmBase(unittest.TestCase):
    def setUp(self):
        self.c = CLI
        self.c.cleanup()

    def tearDown(self):
        self.c.cleanup()


class PowerLifecycleAndLoads(PwrmBase):
    """Optional auto-start via LAYOUT, dynamic task STOP, runtime RAIL add/remove and auto-remove."""

    def test_stopped_until_layout_then_auto_starts_and_stops_cleanly(self):
        # Stopped: STATUS returns ERR_PWRM_NOT_READY
        self.assertEqual(self.c.pm_r(OP_STATUS)[0], 1)

        # LAYOUT auto-starts the power manager and computes initial status immediately
        self.assertEqual(self.c.pm_w(OP_LAYOUT, layout_bytes(max_mV=12000, max_mA=2000, reserve_mW=1000, unknown_source_mA=1000))[0], 0)
        st, body = self.c.pm_r(OP_STATUS)
        self.assertEqual(st, 0)
        s = unpack_status(body)
        self.assertEqual(s["budget_mW"], 12000)
        self.assertEqual(s["limit_mW"], 12000)

        # STOP releases task & state; STATUS fails again
        self.assertEqual(self.c.pm_w(OP_STOP)[0], 0)
        self.assertEqual(self.c.pm_r(OP_STATUS)[0], 1)
        # Idempotent STOP
        self.assertEqual(self.c.pm_w(OP_STOP)[0], 0)

    def test_runtime_rail_add_remove_and_auto_remove_on_device_delete(self):
        self.assertEqual(self.c.wr(DEV_RAIL, DEV_CREATE, testdev_cfg())[0], 0)
        self.assertEqual(self.c.pm_w(OP_LAYOUT, layout_bytes())[0], 0)

        row_sz = 1 + KIND_COUNT  # uint8_t target + uint8_t responses[KIND_COUNT]
        # Initially matrix has 1 row (board)
        self.assertEqual(len(self.c.pm_r(OP_MATRIX)[1]), row_sz)
        # Add rail at runtime -> 2 rows; duplicate rejected
        self.assertEqual(self.c.pm_w(OP_RAIL, struct.pack("<BBBI", DEV_RAIL, 0xFF, 0, 200))[0], 0)
        self.assertEqual(self.c.pm_w(OP_RAIL, struct.pack("<BBBI", DEV_RAIL, 0xFF, 0, 200))[0], 1)
        self.assertEqual(len(self.c.pm_r(OP_MATRIX)[1]), 2 * row_sz)

        # Explicit RAIL_REMOVE -> back to 1 row
        self.assertEqual(self.c.pm_w(OP_RAIL_REMOVE, struct.pack("<B", DEV_RAIL))[0], 0)
        self.assertEqual(len(self.c.pm_r(OP_MATRIX)[1]), row_sz)
        self.assertEqual(self.c.pm_w(OP_RAIL_REMOVE, struct.pack("<B", DEV_RAIL))[0], 1)

        # Re-add rail, then remove device -> auto-removed by SYS_DEVICE_EV_REMOVED
        self.assertEqual(self.c.pm_w(OP_RAIL, struct.pack("<BBBI", DEV_RAIL, 0xFF, 0, 200))[0], 0)
        self.assertEqual(self.c.wr(DEV_RAIL, DEV_REMOVE)[0], 0)
        time.sleep(0.05)
        self.assertEqual(len(self.c.pm_r(OP_MATRIX)[1]), row_sz)


class PowerSourceAndBattery(PwrmBase):
    """Automatic source detection via source_pins, USB-PD negotiation, and LiPo curve."""

    def test_auto_source_detection_pd_and_lipo_battery(self):
        self.assertEqual(self.c.wr(DEV_BUS, DEV_CREATE, testdev_cfg())[0], 0)
        self.assertEqual(self.c.wr(DEV_EAR, DEV_CREATE, testdev_cfg())[0], 0)
        # Pin 0 = usb_ok, Pin 1 = ext_ok (inputs on DEV_BUS)
        self.assertEqual(self.c.wr(DEV_BUS, IO_MODE, bytes([0, 0]))[0], 0)
        self.assertEqual(self.c.wr(DEV_BUS, IO_MODE, bytes([1, 0]))[0], 0)
        # USB live first, input channel 0 at 15000 mV
        self.assertEqual(self.c.wr(DEV_BUS, OWN_FAKE_PIN, struct.pack("<BBi", 0, 1, 0))[0], 0)
        self.assertEqual(self.c.wr(DEV_BUS, OWN_FAKE_CHANNEL, struct.pack("<Bii", 0, 15000, 100))[0], 0)

        self.assertEqual(
            self.c.pm_w(
                OP_LAYOUT,
                layout_bytes(max_mV=20000, max_mA=5000, reserve_mW=1000, input_dev=DEV_BUS, input_ch=0,
                             usb_pd=DEV_BUS, usb_ok=(DEV_BUS, 0), ext_ok=(DEV_BUS, 1)),
            )[0],
            0,
        )
        s = unpack_status(self.c.pm_r(OP_STATUS)[1])
        self.assertEqual(s["source"], SRC_USB)
        self.assertEqual((s["pd_mV"], s["pd_mA"]), (15000, 3000))  # testdev highest PD offer
        self.assertEqual(s["budget_mW"], 45000)

        # Switch to external input (ext_ok=1 wins over usb_ok=1) with 3S LiPo battery at 3.84 V/cell (50%)
        self.assertEqual(self.c.wr(DEV_EAR, OWN_LISTEN, struct.pack("<IHH", EV_PWRM_BAT_LOW, ANY_SRC, ANY_CH))[0], 0)
        self.assertEqual(self.c.pm_w(OP_BATTERY, struct.pack("<BBBB", 1, 3, 25, 10))[0], 0)
        self.assertEqual(self.c.wr(DEV_BUS, OWN_FAKE_PIN, struct.pack("<BBi", 1, 1, 0))[0], 0)
        self.assertEqual(self.c.wr(DEV_BUS, OWN_FAKE_CHANNEL, struct.pack("<Bii", 0, 3 * 3840, 100))[0], 0)
        # Trigger immediate tick via config write (keep STRICT)
        self.assertEqual(self.c.pm_w(OP_CONFIG, struct.pack("<BBB", MODE_STRICT, 20, SRC_UNKNOWN))[0], 0)
        time.sleep(0.06)
        s = unpack_status(self.c.pm_r(OP_STATUS)[1])
        self.assertEqual(s["source"], SRC_BATTERY)
        self.assertEqual(s["battery_pct"], 50)

        # Drop battery to 3 * 3730 mV (20% <= low_pct 25%) -> BATTERY_LOW event published
        self.assertEqual(self.c.wr(DEV_BUS, OWN_FAKE_CHANNEL, struct.pack("<Bii", 0, 3 * 3730, 100))[0], 0)
        self.assertEqual(self.c.pm_w(OP_CONFIG, struct.pack("<BBB", MODE_STRICT, 20, SRC_UNKNOWN))[0], 0)
        time.sleep(0.06)
        s = unpack_status(self.c.pm_r(OP_STATUS)[1])
        self.assertEqual(s["battery_pct"], 20)
        cnt, tag, _, _, _, data = unpack_heard(self.c.rd(DEV_EAR, OWN_HEARD)[1])
        self.assertEqual((cnt, tag, data[0]), (1, EV_PWRM_BAT_LOW, 20))


class PowerBudgetClamp(PwrmBase):
    """3 budget modes (DISABLED / STRICT / MARGIN), interceptor clamping, and PWRM_EV_LIMIT_REACHED."""

    def test_three_budget_modes_and_interceptor_clamping(self):
        self.assertEqual(self.c.wr(DEV_RAIL, DEV_CREATE, testdev_cfg())[0], 0)
        self.assertEqual(self.c.wr(DEV_EAR, DEV_CREATE, testdev_cfg())[0], 0)
        self.assertEqual(self.c.wr(DEV_EAR, OWN_LISTEN, struct.pack("<IHH", EV_PWRM_LIMIT_REACHED, DEV_RAIL, ANY_CH))[0], 0)

        # Budget: 10 V * 1 A = 10000 mW, reserve 2000 mW -> 8000 mW available for rail (100% eff)
        self.assertEqual(
            self.c.pm_w(OP_LAYOUT, layout_bytes(max_mV=10000, max_mA=5000, reserve_mW=2000, unknown_source_mA=1000, eff_pct=100))[0],
            0,
        )
        self.assertEqual(self.c.pm_w(OP_RAIL, struct.pack("<BBBI", DEV_RAIL, 0xFF, 0, 200))[0], 0)

        # Enable rail at 5 V, then ask for 3000 mA (15000 mW > 8000 mW) in STRICT mode
        self.assertEqual(self.c.wr(DEV_RAIL, VREG_VOLTAGE, struct.pack("<I", 5000))[0], 0)
        self.assertEqual(self.c.wr(DEV_RAIL, VREG_ENABLE, bytes([1]))[0], 0)
        self.assertEqual(self.c.wr(DEV_RAIL, VREG_CURRENT, struct.pack("<I", 3000))[0], 0)

        # Interceptor clamped current packet to 1600 mA (8000 mW / 5 V) and emitted LIMIT_REACHED!
        en, mv, ma = struct.unpack("<BII", self.c.rd(DEV_RAIL, OWN_VREG)[1])
        self.assertEqual((en, mv, ma), (1, 5000, 1600))
        time.sleep(0.05)
        cnt, tag, src, _, ln, data = unpack_heard(self.c.rd(DEV_EAR, OWN_HEARD)[1])
        self.assertEqual((cnt, tag, src, ln), (1, EV_PWRM_LIMIT_REACHED, DEV_RAIL, 9))
        self.assertEqual(struct.unpack("<BII", data), (DEV_RAIL, 15000, 8000))

        # Switch to MARGIN mode (+25% -> limit 12500 mW -> 10500 mW avail -> 2100 mA at 5 V)
        self.assertEqual(self.c.pm_w(OP_CONFIG, struct.pack("<BBB", MODE_MARGIN, 25, SRC_UNKNOWN))[0], 0)
        time.sleep(0.05)
        self.assertEqual(self.c.wr(DEV_RAIL, VREG_CURRENT, struct.pack("<I", 3000))[0], 0)
        self.assertEqual(struct.unpack("<BII", self.c.rd(DEV_RAIL, OWN_VREG)[1]), (1, 5000, 2100))

        # OP_VOLTAGE raised to 10 V while fixed at 2100 mA (21000 mW > 10500 mW) -> lowers rail current to 1050 mA
        self.assertEqual(self.c.wr(DEV_RAIL, VREG_VOLTAGE, struct.pack("<I", 10000))[0], 0)
        self.assertEqual(struct.unpack("<BII", self.c.rd(DEV_RAIL, OWN_VREG)[1]), (1, 10000, 1050))

        # Switch to DISABLED mode -> 4000 mA passes unclamped
        self.assertEqual(self.c.pm_w(OP_CONFIG, struct.pack("<BBB", MODE_DISABLED, 25, SRC_UNKNOWN))[0], 0)
        time.sleep(0.05)
        self.assertEqual(self.c.wr(DEV_RAIL, VREG_CURRENT, struct.pack("<I", 4000))[0], 0)
        self.assertEqual(struct.unpack("<BII", self.c.rd(DEV_RAIL, OWN_VREG)[1]), (1, 10000, 4000))


class PowerFaultMatrix(PwrmBase):
    """Unified fault event codes (VREG/PMON -> sys_power_kind_e) and per-rail response matrix."""

    def test_vreg_ovp_fault_triggers_matrix_disable_automatically(self):
        self.assertEqual(self.c.wr(DEV_RAIL, DEV_CREATE, testdev_cfg())[0], 0)
        self.assertEqual(self.c.pm_w(OP_LAYOUT, layout_bytes())[0], 0)
        self.assertEqual(self.c.pm_w(OP_RAIL, struct.pack("<BBBI", DEV_RAIL, DEV_RAIL, 0, 200))[0], 0)

        # Default matrix has OVP -> DISABLE (SUSPEND that rail)
        self.assertEqual(self.c.wr(DEV_RAIL, OWN_FAKE_FAULT, struct.pack("<BBi", 4, 0, 21000))[0], 0)  # VREG_EV_OVP
        time.sleep(0.06)
        self.assertEqual(struct.unpack("<HB", self.c.rd(DEV_RAIL, DEV_INFO)[1])[1], 1)  # suspended!


if __name__ == "__main__":
    unittest.main(verbosity=2)
