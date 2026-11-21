#!/usr/bin/env python3
"""Tests for device_testdev (type 0x004E) on a board over the console session.

    python runit-esp/tools/test_testdev.py                # the one serial port found; RUNIT_PORT=COM10 to name it
    python runit-esp/tools/test_testdev.py -v TestdevIo   # one class, verbose

Creates temporary testdev instance(s) (IDs 10 and 11), exercises every contract (DEVICE, IO, I2C, EVENT, PMON, VREG,
PD, SUPPLY, HBRIDGE, UART) and every own op (0xFF00..0xFF08), plus SUSPEND/RESUME/RESET lifecycle and an I2C child
device on the simulated bus. Removes every device it created in tearDown.
"""
import contextlib
import io
import os
import struct
import sys
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import session_client as sc  # noqa: E402

READ, WRITE = sc.READ, sc.WRITE
DEV_A, DEV_B = 10, 11
TESTDEV_TYPE = 0x004E

# Lifecycle (CONTRACT_SYS_DEVICE, 0x20xx)
DEV_CREATE = 0x2000
DEV_RESET = 0x2001
DEV_SUSPEND = 0x2002
DEV_RESUME = 0x2003
DEV_REMOVE = 0x2004
DEV_INFO = 0x2005
DEV_OPS = 0x2006

# IO (0x01xx)
IO_LEVEL = 0x0100
IO_TOGGLE = 0x0101
IO_PWM_DUTY = 0x0103
IO_PWM_FREQ = 0x0104
IO_MODE = 0x0105
IO_RESET = 0x0106
IO_INTR = 0x0107
IO_VOLTAGE = 0x0109
IO_AVAILABLE = 0x010A
IO_LOCKED = 0x010B
IO_LOCK = 0x010C
IO_UNLOCK = 0x010D

MODE_INPUT = 0
MODE_INPUT_PULLUP = 1
MODE_OUTPUT_PP = 3
MODE_PWM = 6
MODE_ADC = 7
MODE_DAC = 8

INTR_DISABLE = 0
INTR_RISING = 1
INTR_FALLING = 2
INTR_BOTH = 3
INTR_ADC_OUTSIDE = 4
INTR_ADC_INSIDE = 5

# I2C (0x02xx)
I2C_ATTACH = 0x0200
I2C_PROBE = 0x0203
I2C_SCAN = 0x0204
I2C_RECOVER = 0x0205
I2C_RAW = 0x0206

# PMON (0x04xx)
PMON_VOLTAGE = 0x0400
PMON_CURRENT = 0x0401
PMON_ALERT = 0x0402
PMON_POWER = 0x0403

# VREG (0x05xx)
VREG_ENABLE = 0x0500
VREG_VOLTAGE = 0x0501
VREG_CURRENT = 0x0502

# PD (0x07xx)
PD_SETTINGS = 0x0700
PD_OPTIONS = 0x0701
PD_LIMITS = 0x0702

# SUPPLY (0x08xx)
SUPPLY_SOURCE = 0x0800

# HBRIDGE (0x09xx)
HB_DRIVE = 0x0900
HB_BRAKE = 0x0901
HB_COAST = 0x0902
HB_CURRENT = 0x0903

# UART (0x13xx)
UART_INFO = 0x1300
UART_BAUD = 0x1301
UART_SEND = 0x1302
UART_RECEIVE = 0x1303
UART_FLUSH = 0x1304
UART_SUBSCRIBE = 0x1305
UART_TRANSACT = 0x1306

# Own ops (0xFFxx)
OWN_FAKE_PIN = 0xFF00
OWN_FAKE_I2C = 0xFF01
OWN_FAKE_CHANNEL = 0xFF02
OWN_FAKE_FAULT = 0xFF03
OWN_FAKE_UART_RX = 0xFF04
OWN_LISTEN = 0xFF05
OWN_HEARD = 0xFF06
OWN_VREG = 0xFF07
OWN_LOOPBACK = 0xFF08

# Event tags: (module << 16) | code
EV_IO_RISING = (0x01 << 16) | 1
EV_IO_FALLING = (0x01 << 16) | 2
EV_IO_ADC_OUTSIDE = (0x01 << 16) | 3
EV_IO_ADC_INSIDE = (0x01 << 16) | 4
EV_PMON_WARN = (0x04 << 16) | 4
EV_PMON_CRIT = (0x04 << 16) | 5
EV_VREG_OVP = (0x05 << 16) | 1
EV_VREG_SCP = (0x05 << 16) | 3
EV_VREG_OCP = (0x05 << 16) | 5
EV_VREG_READY = (0x05 << 16) | 0x10
EV_HB_OC = (0x09 << 16) | 1
EV_HB_THERMAL = (0x09 << 16) | 2
EV_HB_EXT = (0x09 << 16) | 3
EV_UART_OVERFLOW = (0x13 << 16) | 1

ANY_TAG = 0xFFFFFFFF
ANY_SRC = 0xFFFF
ANY_CH = 0xFFFF


def quiet():
    return contextlib.redirect_stdout(io.StringIO())


def dev_cmd(dev_id: int, side: int, op: int, args: bytes = b"") -> bytes:
    return bytes([sc.KEY_DEVICE, side, dev_id]) + struct.pack("<H", op) + args


def testdev_cfg(pins: int = 16, channels: int = 4, uart_mode: int = 1, uart_baud: int = 115200) -> bytes:
    return struct.pack("<HB", TESTDEV_TYPE, 0) + struct.pack("<BBBI", pins, channels, uart_mode, uart_baud)


def unpack_heard(body: bytes):
    """testdev_heard_t: count u32, tag u32, source u16, channel u16, len u16, data[16]."""
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

    def create_testdev(self, dev_id: int = DEV_A, **kwargs):
        return self.wr(dev_id, DEV_CREATE, testdev_cfg(**kwargs))

    def remove_if_present(self, *ids: int):
        for dev_id in ids:
            self.wr(dev_id, DEV_REMOVE)


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
    CLI.remove_if_present(DEV_B, DEV_A)


class TestdevBase(unittest.TestCase):
    def setUp(self):
        self.c = CLI
        self.c.remove_if_present(DEV_B, DEV_A)
        st, _ = self.c.create_testdev(DEV_A)
        self.assertEqual(st, 0)

    def tearDown(self):
        self.c.remove_if_present(DEV_B, DEV_A)


class TestdevLifecycle(TestdevBase):
    """CREATE validation, INFO, OPS, SUSPEND/RESUME safe state, RESET, REMOVE."""

    def test_info_and_ops(self):
        st, b = self.c.rd(DEV_A, DEV_INFO)
        self.assertEqual(st, 0)
        self.assertEqual(struct.unpack("<HB", b), (TESTDEV_TYPE, 0))
        st, ops = self.c.rd(DEV_A, DEV_OPS)
        self.assertEqual(st, 0)
        self.assertGreater(len(ops), 20)

    def test_create_rejects_bad_config(self):
        # 0 pins, 0 channels, >4 channels, bad uart_mode, bad uart_baud
        for bad in (
            dict(pins=0),
            dict(pins=65),
            dict(channels=0),
            dict(channels=5),
            dict(uart_mode=9),
            dict(uart_baud=300),
            dict(uart_baud=6000000),
        ):
            self.assertEqual(self.c.create_testdev(DEV_B, **bad)[0], 1, bad)

    def test_suspend_safe_state_and_resume(self):
        # Set active outputs across contracts
        self.assertEqual(self.c.wr(DEV_A, IO_MODE, bytes([0, MODE_OUTPUT_PP]))[0], 0)
        self.assertEqual(self.c.wr(DEV_A, IO_LEVEL, bytes([0, 1]))[0], 0)
        self.assertEqual(self.c.wr(DEV_A, IO_MODE, bytes([1, MODE_DAC]))[0], 0)
        self.assertEqual(self.c.wr(DEV_A, IO_VOLTAGE, struct.pack("<Bi", 1, 2500))[0], 0)
        self.assertEqual(self.c.wr(DEV_A, VREG_ENABLE, bytes([1]))[0], 0)
        self.assertEqual(self.c.wr(DEV_A, SUPPLY_SOURCE, bytes([1]))[0], 0)
        self.assertEqual(self.c.wr(DEV_A, HB_DRIVE, struct.pack("<Bh", 0, 2000))[0], 0)

        self.assertEqual(self.c.wr(DEV_A, DEV_SUSPEND)[0], 0)
        self.assertEqual(struct.unpack("<HB", self.c.rd(DEV_A, DEV_INFO)[1])[1], 1)
        # Safe state verified via reads
        self.assertEqual(self.c.rd(DEV_A, IO_LEVEL, bytes([0]))[1], bytes([0, 0]))
        self.assertEqual(struct.unpack("<Bi", self.c.rd(DEV_A, IO_VOLTAGE, bytes([1]))[1]), (1, 0))
        self.assertEqual(struct.unpack("<BII", self.c.rd(DEV_A, OWN_VREG)[1])[0], 0)
        self.assertEqual(self.c.rd(DEV_A, SUPPLY_SOURCE)[1], bytes([0]))
        # Non-ALWAYS write is refused while suspended; ALWAYS write (FAKE_PIN) still works
        self.assertEqual(self.c.wr(DEV_A, IO_LEVEL, bytes([0, 1]))[0], 1)
        self.assertEqual(self.c.wr(DEV_A, OWN_FAKE_PIN, struct.pack("<BBi", 2, 1, 1234))[0], 0)

        # Resume does not restore previous outputs, allows writes again
        self.assertEqual(self.c.wr(DEV_A, DEV_RESUME)[0], 0)
        self.assertEqual(self.c.rd(DEV_A, IO_LEVEL, bytes([0]))[1], bytes([0, 0]))
        self.assertEqual(self.c.wr(DEV_A, IO_LEVEL, bytes([0, 1]))[0], 0)

    def test_reset_restores_defaults(self):
        self.assertEqual(self.c.wr(DEV_A, VREG_VOLTAGE, struct.pack("<I", 12000))[0], 0)
        self.assertEqual(self.c.wr(DEV_A, SUPPLY_SOURCE, bytes([2]))[0], 0)
        self.assertEqual(self.c.wr(DEV_A, OWN_LOOPBACK, bytes([0]))[0], 0)
        self.assertEqual(self.c.wr(DEV_A, DEV_RESET)[0], 0)
        self.assertEqual(struct.unpack("<BII", self.c.rd(DEV_A, OWN_VREG)[1]), (0, 5000, 1000))
        self.assertEqual(self.c.rd(DEV_A, SUPPLY_SOURCE)[1], bytes([0]))
        self.assertEqual(self.c.rd(DEV_A, OWN_LOOPBACK)[1], bytes([1]))


class TestdevIo(TestdevBase):
    """IO contract: digital, PWM, ADC/DAC, AVAILABLE/LOCKED, LOCAL refusal, edges and ADC window events."""

    def test_available_locked_and_local_refused(self):
        self.assertEqual(struct.unpack("<Q", self.c.rd(DEV_A, IO_AVAILABLE)[1])[0], (1 << 16) - 1)
        self.assertEqual(struct.unpack("<Q", self.c.rd(DEV_A, IO_LOCKED)[1])[0], 0)
        self.assertEqual(self.c.wr(DEV_A, IO_LOCK, bytes([0]))[0], 1)
        self.assertEqual(self.c.wr(DEV_A, IO_UNLOCK, bytes([0]))[0], 1)

    def test_digital_input_output_toggle_and_reset(self):
        # Unconfigured pin fails read/write
        self.assertEqual(self.c.rd(DEV_A, IO_LEVEL, bytes([0]))[0], 1)
        # Input pullup starts high, FAKE_PIN drives it low
        self.assertEqual(self.c.wr(DEV_A, IO_MODE, bytes([0, MODE_INPUT_PULLUP]))[0], 0)
        self.assertEqual(self.c.rd(DEV_A, IO_LEVEL, bytes([0]))[1], bytes([0, 1]))
        self.assertEqual(self.c.wr(DEV_A, IO_LEVEL, bytes([0, 0]))[0], 1)  # cannot write input
        self.assertEqual(self.c.wr(DEV_A, OWN_FAKE_PIN, struct.pack("<BBi", 0, 0, 0))[0], 0)
        self.assertEqual(self.c.rd(DEV_A, IO_LEVEL, bytes([0]))[1], bytes([0, 0]))

        # Output push-pull: level and toggle
        self.assertEqual(self.c.wr(DEV_A, IO_MODE, bytes([1, MODE_OUTPUT_PP]))[0], 0)
        self.assertEqual(self.c.wr(DEV_A, IO_MODE, bytes([1, MODE_OUTPUT_PP]))[0], 1)  # already in use
        self.assertEqual(self.c.wr(DEV_A, IO_LEVEL, bytes([1, 1]))[0], 0)
        self.assertEqual(self.c.rd(DEV_A, IO_LEVEL, bytes([1]))[1], bytes([1, 1]))
        self.assertEqual(self.c.wr(DEV_A, IO_TOGGLE, bytes([1]))[0], 0)
        self.assertEqual(self.c.rd(DEV_A, IO_LEVEL, bytes([1]))[1], bytes([1, 0]))
        self.assertEqual(self.c.wr(DEV_A, IO_RESET, bytes([1]))[0], 0)
        self.assertEqual(self.c.rd(DEV_A, IO_LEVEL, bytes([1]))[0], 1)

    def test_pwm_adc_dac(self):
        self.assertEqual(self.c.wr(DEV_A, IO_MODE, bytes([2, MODE_PWM]))[0], 0)
        self.assertEqual(self.c.wr(DEV_A, IO_PWM_DUTY, struct.pack("<BI", 2, 2048))[0], 0)
        self.assertEqual(self.c.wr(DEV_A, IO_PWM_DUTY, struct.pack("<BI", 2, 4096))[0], 1)  # out of range
        self.assertEqual(self.c.wr(DEV_A, IO_PWM_FREQ, struct.pack("<BI", 2, 5000))[0], 0)
        self.assertEqual(self.c.wr(DEV_A, IO_PWM_FREQ, struct.pack("<BI", 2, 0))[0], 1)

        self.assertEqual(self.c.wr(DEV_A, IO_MODE, bytes([3, MODE_ADC]))[0], 0)
        self.assertEqual(self.c.wr(DEV_A, OWN_FAKE_PIN, struct.pack("<BBi", 3, 0, 1650))[0], 0)
        self.assertEqual(struct.unpack("<Bi", self.c.rd(DEV_A, IO_VOLTAGE, bytes([3]))[1]), (3, 1650))
        self.assertEqual(struct.unpack("<BBi", self.c.rd(DEV_A, OWN_FAKE_PIN, bytes([3]))[1]), (3, 0, 1650))

        self.assertEqual(self.c.wr(DEV_A, IO_MODE, bytes([4, MODE_DAC]))[0], 0)
        self.assertEqual(self.c.wr(DEV_A, IO_VOLTAGE, struct.pack("<Bi", 4, 3300))[0], 0)
        self.assertEqual(self.c.wr(DEV_A, IO_VOLTAGE, struct.pack("<Bi", 4, 3301))[0], 1)
        self.assertEqual(struct.unpack("<Bi", self.c.rd(DEV_A, IO_VOLTAGE, bytes([4]))[1]), (4, 3300))

    def test_io_interrupts_heard_by_second_device(self):
        self.assertEqual(self.c.create_testdev(DEV_B)[0], 0)
        self.assertEqual(self.c.wr(DEV_B, OWN_LISTEN, struct.pack("<IHH", ANY_TAG, DEV_A, ANY_CH))[0], 0)

        # Digital both-edges interrupt on pin 5
        self.assertEqual(self.c.wr(DEV_A, IO_MODE, bytes([5, MODE_INPUT]))[0], 0)
        self.assertEqual(self.c.wr(DEV_A, IO_INTR, struct.pack("<BBBHHHH", 5, INTR_BOTH, 0, 0, 0, 0, 0))[0], 0)
        self.assertEqual(self.c.wr(DEV_A, OWN_FAKE_PIN, struct.pack("<BBi", 5, 1, 0))[0], 0)
        cnt, tag, src, ch, ln, _ = unpack_heard(self.c.rd(DEV_B, OWN_HEARD)[1])
        self.assertEqual((cnt, tag, src, ch, ln), (1, EV_IO_RISING, DEV_A, 5, 0))
        self.assertEqual(self.c.wr(DEV_A, OWN_FAKE_PIN, struct.pack("<BBi", 5, 0, 0))[0], 0)
        cnt, tag, src, ch, ln, _ = unpack_heard(self.c.rd(DEV_B, OWN_HEARD)[1])
        self.assertEqual((cnt, tag, src, ch, ln), (2, EV_IO_FALLING, DEV_A, 5, 0))

        # ADC window outside interrupt on pin 6: [1000..2000] mV
        self.assertEqual(self.c.wr(DEV_A, IO_MODE, bytes([6, MODE_ADC]))[0], 0)
        self.assertEqual(self.c.wr(DEV_A, OWN_FAKE_PIN, struct.pack("<BBi", 6, 0, 1500))[0], 0)
        self.assertEqual(self.c.wr(DEV_A, IO_INTR, struct.pack("<BBBHHHH", 6, INTR_ADC_OUTSIDE, 0, 2000, 1000, 0, 0))[0], 0)
        self.assertEqual(self.c.wr(DEV_A, OWN_FAKE_PIN, struct.pack("<BBi", 6, 0, 2500))[0], 0)
        cnt, tag, src, ch, ln, data = unpack_heard(self.c.rd(DEV_B, OWN_HEARD)[1])
        self.assertEqual((cnt, tag, src, ch, ln), (3, EV_IO_ADC_OUTSIDE, DEV_A, 6, 4))
        self.assertEqual(struct.unpack("<i", data)[0], 2500)


class TestdevI2c(TestdevBase):
    """I2C contract: default chips, SCAN, PROBE, RAW read/write, FAKE_I2C, bus stuck & RECOVER, LOCAL refusal."""

    def test_scan_probe_raw_and_fake_i2c(self):
        # LOCAL op refused over wire
        self.assertEqual(self.c.wr(DEV_A, I2C_ATTACH, struct.pack("<BI", 0x20, 400000))[0], 1)

        # Default chips: 0x20, 0x40, 0x48, 0x50
        st, body = self.c.rd(DEV_A, I2C_SCAN)
        self.assertEqual(st, 0)
        words = struct.unpack("<4I", body)
        found = {a for a in range(0x08, 0x78) if (words[a // 32] >> (a % 32)) & 1}
        self.assertEqual(found, {0x20, 0x40, 0x48, 0x50})
        self.assertEqual(self.c.rd(DEV_A, I2C_PROBE, bytes([0x20]))[1], bytes([1]))
        self.assertEqual(self.c.rd(DEV_A, I2C_PROBE, bytes([0x21]))[1], bytes([0]))

        # RAW write + read back on 0x50
        payload = bytes([0xDE, 0xAD, 0xBE, 0xEF])
        self.assertEqual(self.c.wr(DEV_A, I2C_RAW, bytes([0x50, 0x10]) + payload)[0], 0)
        st, rd = self.c.rd(DEV_A, I2C_RAW, bytes([0x50, 0x10, len(payload)]))
        self.assertEqual((st, rd), (0, bytes([0x50, 0x10]) + payload))

        # Remove 0x48, add 0x33
        self.assertEqual(self.c.wr(DEV_A, OWN_FAKE_I2C, bytes([0x48, 0]))[0], 0)
        self.assertEqual(self.c.rd(DEV_A, I2C_PROBE, bytes([0x48]))[1], bytes([0]))
        self.assertEqual(self.c.wr(DEV_A, OWN_FAKE_I2C, bytes([0x33, 1]))[0], 0)
        self.assertEqual(self.c.rd(DEV_A, I2C_PROBE, bytes([0x33]))[1], bytes([1]))

        # Fault I2C_STUCK blocks PROBE/SCAN/RAW until RECOVER
        self.assertEqual(self.c.wr(DEV_A, OWN_FAKE_FAULT, struct.pack("<BBi", 11, 0, 0))[0], 0)
        self.assertEqual(self.c.rd(DEV_A, I2C_PROBE, bytes([0x20]))[0], 1)
        self.assertEqual(self.c.rd(DEV_A, I2C_SCAN)[0], 1)
        self.assertEqual(self.c.wr(DEV_A, I2C_RECOVER)[0], 0)
        self.assertEqual(self.c.rd(DEV_A, I2C_PROBE, bytes([0x20]))[1], bytes([1]))


class TestdevPowerAndBridge(TestdevBase):
    """PMON, VREG, PD, SUPPLY, HBRIDGE and FAKE_FAULT events."""

    def test_pmon_and_alerts(self):
        self.assertEqual(self.c.create_testdev(DEV_B)[0], 0)
        self.assertEqual(self.c.wr(DEV_B, OWN_LISTEN, struct.pack("<IHH", ANY_TAG, DEV_A, ANY_CH))[0], 0)

        # Default channel 0: 5000 mV, 0 mA
        self.assertEqual(struct.unpack("<i", self.c.rd(DEV_A, PMON_VOLTAGE, bytes([0]))[1])[0], 5000)
        self.assertEqual(struct.unpack("<i", self.c.rd(DEV_A, PMON_CURRENT, bytes([0]))[1])[0], 0)
        self.assertEqual(struct.unpack("<i", self.c.rd(DEV_A, PMON_POWER, bytes([0]))[1])[0], 0)

        # Arm warning alert at 500 mA on channel 1, then cross it
        self.assertEqual(self.c.wr(DEV_A, PMON_ALERT, struct.pack("<BBi", 1, 0, 500))[0], 0)
        self.assertEqual(self.c.wr(DEV_A, OWN_FAKE_CHANNEL, struct.pack("<Bii", 1, 12000, 750))[0], 0)
        self.assertEqual(struct.unpack("<i", self.c.rd(DEV_A, PMON_POWER, bytes([1]))[1])[0], 9000)
        self.assertEqual(struct.unpack("<Bii", self.c.rd(DEV_A, OWN_FAKE_CHANNEL, bytes([1]))[1]), (1, 12000, 750))
        cnt, tag, src, ch, ln, data = unpack_heard(self.c.rd(DEV_B, OWN_HEARD)[1])
        self.assertEqual((cnt, tag, src, ch, ln), (1, EV_PMON_WARN, DEV_A, 1, 4))
        self.assertEqual(struct.unpack("<i", data)[0], 750)

    def test_vreg_pd_supply_and_hbridge(self):
        self.assertEqual(self.c.create_testdev(DEV_B)[0], 0)
        self.assertEqual(self.c.wr(DEV_B, OWN_LISTEN, struct.pack("<IHH", EV_VREG_READY, DEV_A, ANY_CH))[0], 0)

        # VREG settings + READY on enable and on raising voltage while enabled
        self.assertEqual(self.c.wr(DEV_A, VREG_VOLTAGE, struct.pack("<I", 9000))[0], 0)
        self.assertEqual(self.c.wr(DEV_A, VREG_CURRENT, struct.pack("<I", 2500))[0], 0)
        self.assertEqual(self.c.wr(DEV_A, VREG_ENABLE, bytes([1]))[0], 0)
        self.assertEqual(struct.unpack("<BII", self.c.rd(DEV_A, OWN_VREG)[1]), (1, 9000, 2500))
        cnt, tag, src, ch, ln, data = unpack_heard(self.c.rd(DEV_B, OWN_HEARD)[1])
        self.assertEqual((cnt, tag, src, ch, ln), (1, EV_VREG_READY, DEV_A, 0, 4))
        self.assertEqual(struct.unpack("<i", data)[0], 9000)
        self.assertEqual(self.c.wr(DEV_A, VREG_VOLTAGE, struct.pack("<I", 12000))[0], 0)
        cnt, _, _, _, _, data = unpack_heard(self.c.rd(DEV_B, OWN_HEARD)[1])
        self.assertEqual((cnt, struct.unpack("<i", data)[0]), (2, 12000))
        self.assertEqual(self.c.wr(DEV_A, VREG_VOLTAGE, struct.pack("<I", 999))[0], 1)  # below min

        # PD options, limits, settings
        st, opts = self.c.rd(DEV_A, PD_OPTIONS)
        self.assertEqual((st, len(opts)), (0, 4 * 8))
        self.assertEqual(struct.unpack("<ii", self.c.rd(DEV_A, PD_LIMITS)[1]), (15000, 3000))
        self.assertEqual(self.c.wr(DEV_A, PD_SETTINGS, struct.pack("<II", 9000, 2000))[0], 0)
        self.assertEqual(struct.unpack("<II", self.c.rd(DEV_A, PD_SETTINGS)[1]), (9000, 2000))

        # SUPPLY source
        self.assertEqual(self.c.wr(DEV_A, SUPPLY_SOURCE, bytes([2]))[0], 0)
        self.assertEqual(self.c.rd(DEV_A, SUPPLY_SOURCE)[1], bytes([2]))
        self.assertEqual(self.c.wr(DEV_A, SUPPLY_SOURCE, bytes([3]))[0], 1)

        # HBRIDGE drive -> current follows |drive| * 1000 / 4095; brake/coast -> 0 mA
        self.assertEqual(self.c.wr(DEV_A, HB_DRIVE, struct.pack("<Bh", 0, 4095))[0], 0)
        self.assertEqual(struct.unpack("<i", self.c.rd(DEV_A, HB_CURRENT, bytes([0]))[1])[0], 1000)
        self.assertEqual(self.c.wr(DEV_A, HB_BRAKE, bytes([0]))[0], 0)
        self.assertEqual(struct.unpack("<i", self.c.rd(DEV_A, HB_CURRENT, bytes([0]))[1])[0], 0)
        self.assertEqual(self.c.wr(DEV_A, HB_DRIVE, struct.pack("<Bh", 0, -2048))[0], 0)
        self.assertEqual(struct.unpack("<i", self.c.rd(DEV_A, HB_CURRENT, bytes([0]))[1])[0], 500)
        self.assertEqual(self.c.wr(DEV_A, HB_COAST, bytes([0]))[0], 0)
        self.assertEqual(struct.unpack("<i", self.c.rd(DEV_A, HB_CURRENT, bytes([0]))[1])[0], 0)

    def test_fake_fault_publishes_events(self):
        self.assertEqual(self.c.create_testdev(DEV_B)[0], 0)
        self.assertEqual(self.c.wr(DEV_B, OWN_LISTEN, struct.pack("<IHH", ANY_TAG, DEV_A, ANY_CH))[0], 0)

        cases = [
            (0, 2, 0, EV_IO_RISING, 2, b""),
            (1, 2, 0, EV_IO_FALLING, 2, b""),
            (2, 1, 800, EV_PMON_WARN, 1, struct.pack("<i", 800)),
            (3, 1, 1500, EV_PMON_CRIT, 1, struct.pack("<i", 1500)),
            (4, 0, 21000, EV_VREG_OVP, 0, struct.pack("<i", 21000)),
            (5, 0, 5500, EV_VREG_OCP, 0, struct.pack("<i", 5500)),
            (6, 0, 0, EV_VREG_SCP, 0, b""),
            (7, 0, 3200, EV_HB_OC, 0, struct.pack("<i", 3200)),
            (8, 0, 1200, EV_HB_THERMAL, 0, struct.pack("<i", 1200)),
            (9, 0, 900, EV_HB_EXT, 0, struct.pack("<i", 900)),
            (10, 0, 12, EV_UART_OVERFLOW, 0, struct.pack("<H", 12)),
        ]
        for idx, (kind, ch, val, want_tag, want_ch, want_data) in enumerate(cases, start=1):
            self.assertEqual(self.c.wr(DEV_A, OWN_FAKE_FAULT, struct.pack("<BBi", kind, ch, val))[0], 0)
            cnt, tag, src, got_ch, ln, data = unpack_heard(self.c.rd(DEV_B, OWN_HEARD)[1])
            self.assertEqual((cnt, tag, src, got_ch, ln, data), (idx, want_tag, DEV_A, want_ch, len(want_data), want_data))


class TestdevUart(TestdevBase):
    """UART contract: INFO, BAUD, LOOPBACK, SEND/RECEIVE, TRANSACT, FLUSH, FAKE_UART_RX, LOCAL refusal."""

    def test_uart_info_baud_loopback_and_queue(self):
        # LOCAL op refused
        self.assertEqual(self.c.wr(DEV_A, UART_SUBSCRIBE, struct.pack("<H", 0xFF00))[0], 1)

        # INFO & BAUD
        self.assertEqual(struct.unpack("<BIBB", self.c.rd(DEV_A, UART_INFO)[1]), (1, 115200, 0, 1))
        self.assertEqual(self.c.wr(DEV_A, UART_BAUD, struct.pack("<I", 921600))[0], 0)
        self.assertEqual(struct.unpack("<I", self.c.rd(DEV_A, UART_BAUD)[1])[0], 921600)
        self.assertEqual(self.c.wr(DEV_A, UART_BAUD, struct.pack("<I", 100))[0], 1)

        # Loopback on by default: SEND -> RECEIVE
        self.assertEqual(self.c.wr(DEV_A, UART_SEND, struct.pack("<HB", 10, 0) + b"hello")[0], 0)
        self.assertEqual(self.c.rd(DEV_A, UART_RECEIVE, struct.pack("<H", 0)), (0, b"hello"))
        self.assertEqual(self.c.rd(DEV_A, UART_RECEIVE, struct.pack("<H", 0)), (0, b""))  # empty queue

        # TRANSACT flushes old messages and returns the echoed reply
        self.assertEqual(self.c.wr(DEV_A, OWN_FAKE_UART_RX, bytes([2]) + b"old")[0], 0)
        self.assertEqual(self.c.wr(DEV_A, UART_TRANSACT, struct.pack("<H", 10) + b"ping"), (0, b"ping"))
        self.assertEqual(self.c.rd(DEV_A, UART_RECEIVE, struct.pack("<H", 0)), (0, b""))

        # Loopback off: SEND goes nowhere, FAKE_UART_RX feeds queue, FLUSH clears it
        self.assertEqual(self.c.wr(DEV_A, OWN_LOOPBACK, bytes([0]))[0], 0)
        self.assertEqual(self.c.rd(DEV_A, OWN_LOOPBACK)[1], bytes([0]))
        self.assertEqual(self.c.wr(DEV_A, UART_SEND, struct.pack("<HB", 10, 0) + b"void")[0], 0)
        self.assertEqual(self.c.rd(DEV_A, UART_RECEIVE, struct.pack("<H", 0)), (0, b""))
        self.assertEqual(self.c.wr(DEV_A, OWN_FAKE_UART_RX, bytes([3]) + b"rx!")[0], 0)
        self.assertEqual(self.c.rd(DEV_A, UART_RECEIVE, struct.pack("<H", 0)), (0, b"rx!"))
        self.assertEqual(self.c.wr(DEV_A, UART_FLUSH)[0], 0)
        self.assertEqual(self.c.rd(DEV_A, UART_RECEIVE, struct.pack("<H", 0)), (0, b""))


if __name__ == "__main__":
    unittest.main(verbosity=2)
