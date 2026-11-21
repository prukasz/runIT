#!/usr/bin/env python3
"""Hardware tests of sys_event + sys_device_event + sys_event_stream (PUSH key 0x25) on a board using device_testdev.

    python runit-esp/tools/test_events.py                 # auto-detect single port; RUNIT_PORT=COM10
    python runit-esp/tools/test_events.py -v EventStreamWire

Tests:
  - In-firmware event filtering: exact (tag, source, channel), wildcards (ANY_CHANNEL, ANY_TAG), subscription replace
  - Multiple listeners receiving the same event simultaneously
  - IO digital edges (RISING / FALLING) and ADC window (ADC_OUTSIDE / ADC_INSIDE) with mV payload
  - Wire streaming (sys_event_stream, KEY_EVENTS = 0x25):
      * SYS_EVENT_NOTIFY events arrive as URGENT PUSH frames with 20-byte sys_event_t header + payload + monotonic t_ms
      * Non-NOTIFY events (IO_EV_RISING, VREG_EV_READY, CREATED/REMOVED) reach local listeners without spamming the wire
"""
import contextlib
import io
import os
import struct
import sys
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import runit_defs as defs  # noqa: E402
import session_client as sc  # noqa: E402

READ, WRITE = sc.READ, sc.WRITE
KEY_EVENTS = defs.sym("MODULE_ID_SYS_EVENT")
F_URGENT = sc.F_URGENT
SYS_EVENT_NOTIFY = 0x0001

DEV_SRC, DEV_EAR1, DEV_EAR2 = 22, 23, 24
TESTDEV_TYPE = defs.device_type("testdev")

# Device ops
DEV_CREATE = defs.contract_op("DEVICE", "CREATE")
DEV_RESET = defs.contract_op("DEVICE", "RESET")
DEV_SUSPEND = defs.contract_op("DEVICE", "SUSPEND")
DEV_RESUME = defs.contract_op("DEVICE", "RESUME")
DEV_REMOVE = defs.contract_op("DEVICE", "REMOVE")
IO_MODE = defs.contract_op("IO", "MODE")
IO_INTR = defs.contract_op("IO", "INTR")
VREG_ENABLE = defs.contract_op("POWER_VREG", "ENABLE")
VREG_VOLTAGE = defs.contract_op("POWER_VREG", "VOLTAGE")
OWN_FAKE_PIN = defs.device_own_op("testdev", "fake_pin")
OWN_FAKE_FAULT = defs.device_own_op("testdev", "fake_fault")
OWN_LISTEN = defs.device_own_op("testdev", "listen")
OWN_HEARD = defs.device_own_op("testdev", "heard")

MODE_INPUT = defs.sym("SYS_IO_MODE_INPUT")
MODE_ADC = defs.sym("SYS_IO_MODE_ADC")
INTR_BOTH = defs.sym("SYS_IO_INTR_MODE_BOTH_EDGES")
INTR_ADC_OUTSIDE = defs.sym("SYS_IO_INTR_ADC_WINDOW_OUTSIDE")
INTR_ADC_INSIDE = defs.sym("SYS_IO_INTR_ADC_WINDOW_INSIDE")

# Event tags
EV_IO_RISING = defs.contract_event("IO", "RISING")
EV_IO_FALLING = defs.contract_event("IO", "FALLING")
EV_IO_ADC_OUTSIDE = defs.contract_event("IO", "ADC_OUTSIDE")
EV_IO_ADC_INSIDE = defs.contract_event("IO", "ADC_INSIDE")
EV_PMON_WARN = defs.contract_event("POWER_MONITOR", "OCP_WARNING")
EV_VREG_OVP = defs.contract_event("POWER_VREG", "OVP")
EV_VREG_READY = defs.contract_event("POWER_VREG", "READY")
EV_HB_OC = defs.contract_event("HBRIDGE", "OVERCURRENT")
EV_UART_OVERFLOW = defs.contract_event("UART", "OVERFLOW")
EV_DEV_SUSPENDED = defs.contract_event("DEVICE", "SUSPENDED")
EV_DEV_RESUMED = defs.contract_event("DEVICE", "RESUMED")
EV_DEV_RESET = defs.contract_event("DEVICE", "RESET")
EV_DEV_CREATED = defs.contract_event("DEVICE", "CREATED")
EV_DEV_REMOVED = defs.contract_event("DEVICE", "REMOVED")

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
    count, tag, source, channel, length, data = struct.unpack("<IIHHH16s", body)
    return count, tag, source, channel, length, data[:length]


def unpack_wire_event(body: bytes):
    """sys_event_t (20 B): tag u32, source u16, channel u16, t_ms u64, flags u16, len u16, data[]."""
    tag, source, channel, t_ms, flags, length = struct.unpack_from("<IHHQHH", body, 0)
    return tag, source, channel, t_ms, flags, length, body[20:20 + length]


class Client:
    def __init__(self, port: str):
        self.link = sc.Link(port, 115200)
        with quiet():
            assert self.link.hello(), "the board did not say hello on " + port

    def call(self, cmd: bytes, wait: float = 2.0):
        with quiet():
            return self.link.call(self.link.board, cmd, wait)

    def pump(self, seconds: float):
        with quiet():
            self.link.listen(seconds)

    def rd(self, dev_id: int, op: int, args: bytes = b""):
        return self.call(dev_cmd(dev_id, READ, op, args))

    def wr(self, dev_id: int, op: int, args: bytes = b""):
        return self.call(dev_cmd(dev_id, WRITE, op, args))

    def wire_events(self, since: int = 0):
        out = []
        for flags, _src, _seq, sid, body in self.link.pushes[since:]:
            if sid == KEY_EVENTS and len(body) >= 20:
                out.append((flags, *unpack_wire_event(body)))
        return out

    def cleanup(self):
        for did in (DEV_EAR2, DEV_EAR1, DEV_SRC):
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


class EventBase(unittest.TestCase):
    def setUp(self):
        self.c = CLI
        self.c.cleanup()
        self.assertEqual(self.c.wr(DEV_SRC, DEV_CREATE, testdev_cfg())[0], 0)
        self.assertEqual(self.c.wr(DEV_EAR1, DEV_CREATE, testdev_cfg())[0], 0)

    def tearDown(self):
        self.c.cleanup()


class EventFilteringAndDelivery(EventBase):
    """In-firmware sys_event filtering, subscription replacement, fan-out to multiple listeners, and IO/ADC events."""

    def test_exact_filter_wildcards_and_replace(self):
        # Exact filter: only PMON_WARN on DEV_SRC channel 1
        self.assertEqual(self.c.wr(DEV_EAR1, OWN_LISTEN, struct.pack("<IHH", EV_PMON_WARN, DEV_SRC, 1))[0], 0)
        # Wrong channel (0) and wrong tag (VREG_OVP) must be ignored
        self.assertEqual(self.c.wr(DEV_SRC, OWN_FAKE_FAULT, struct.pack("<BBi", 2, 0, 500))[0], 0)
        self.assertEqual(self.c.wr(DEV_SRC, OWN_FAKE_FAULT, struct.pack("<BBi", 4, 0, 21000))[0], 0)
        self.assertEqual(unpack_heard(self.c.rd(DEV_EAR1, OWN_HEARD)[1])[0], 0)

        # Matching channel 1 -> heard!
        self.assertEqual(self.c.wr(DEV_SRC, OWN_FAKE_FAULT, struct.pack("<BBi", 2, 1, 888))[0], 0)
        cnt, tag, src, ch, ln, data = unpack_heard(self.c.rd(DEV_EAR1, OWN_HEARD)[1])
        self.assertEqual((cnt, tag, src, ch, ln, struct.unpack("<i", data)[0]), (1, EV_PMON_WARN, DEV_SRC, 1, 4, 888))

        # Replacing subscription clears count and switches to wildcard channel
        self.assertEqual(self.c.wr(DEV_EAR1, OWN_LISTEN, struct.pack("<IHH", EV_PMON_WARN, DEV_SRC, ANY_CH))[0], 0)
        self.assertEqual(unpack_heard(self.c.rd(DEV_EAR1, OWN_HEARD)[1])[0], 0)
        self.assertEqual(self.c.wr(DEV_SRC, OWN_FAKE_FAULT, struct.pack("<BBi", 2, 3, 999))[0], 0)
        cnt, _, _, ch, _, data = unpack_heard(self.c.rd(DEV_EAR1, OWN_HEARD)[1])
        self.assertEqual((cnt, ch, struct.unpack("<i", data)[0]), (1, 3, 999))

    def test_multiple_listeners_fan_out(self):
        self.assertEqual(self.c.wr(DEV_EAR2, DEV_CREATE, testdev_cfg())[0], 0)
        self.assertEqual(self.c.wr(DEV_EAR1, OWN_LISTEN, struct.pack("<IHH", EV_HB_OC, DEV_SRC, ANY_CH))[0], 0)
        self.assertEqual(self.c.wr(DEV_EAR2, OWN_LISTEN, struct.pack("<IHH", ANY_TAG, DEV_SRC, 2))[0], 0)

        self.assertEqual(self.c.wr(DEV_SRC, OWN_FAKE_FAULT, struct.pack("<BBi", 7, 2, 3100))[0], 0)
        for ear in (DEV_EAR1, DEV_EAR2):
            cnt, tag, src, ch, ln, data = unpack_heard(self.c.rd(ear, OWN_HEARD)[1])
            self.assertEqual((cnt, tag, src, ch, ln, struct.unpack("<i", data)[0]), (1, EV_HB_OC, DEV_SRC, 2, 4, 3100))

    def test_adc_window_outside_and_inside_events(self):
        self.assertEqual(self.c.wr(DEV_EAR1, OWN_LISTEN, struct.pack("<IHH", ANY_TAG, DEV_SRC, 4))[0], 0)
        self.assertEqual(self.c.wr(DEV_SRC, IO_MODE, bytes([4, MODE_ADC]))[0], 0)
        # Start inside [1000..2000], arm OUTSIDE -> leave to 2500 -> EV_IO_ADC_OUTSIDE
        self.assertEqual(self.c.wr(DEV_SRC, OWN_FAKE_PIN, struct.pack("<BBi", 4, 0, 1500))[0], 0)
        self.assertEqual(self.c.wr(DEV_SRC, IO_INTR, struct.pack("<BBBHHHH", 4, INTR_ADC_OUTSIDE, 0, 2000, 1000, 0, 0))[0], 0)
        self.assertEqual(self.c.wr(DEV_SRC, OWN_FAKE_PIN, struct.pack("<BBi", 4, 0, 2500))[0], 0)
        cnt, tag, _, _, _, data = unpack_heard(self.c.rd(DEV_EAR1, OWN_HEARD)[1])
        self.assertEqual((cnt, tag, struct.unpack("<i", data)[0]), (1, EV_IO_ADC_OUTSIDE, 2500))

        # Arm INSIDE while still outside at 2500 -> enter to 1200 -> EV_IO_ADC_INSIDE
        self.assertEqual(self.c.wr(DEV_SRC, IO_INTR, struct.pack("<BBBHHHH", 4, INTR_ADC_INSIDE, 0, 2000, 1000, 0, 0))[0], 0)
        self.assertEqual(self.c.wr(DEV_SRC, OWN_FAKE_PIN, struct.pack("<BBi", 4, 0, 1200))[0], 0)
        cnt, tag, _, _, _, data = unpack_heard(self.c.rd(DEV_EAR1, OWN_HEARD)[1])
        self.assertEqual((cnt, tag, struct.unpack("<i", data)[0]), (2, EV_IO_ADC_INSIDE, 1200))


class EventStreamWire(EventBase):
    """sys_event_stream (PUSH key 0x25): NOTIFY events stream over wire as URGENT PUSH; non-NOTIFY stay internal."""

    def test_notify_events_stream_over_wire_with_exact_format(self):
        n = len(self.c.link.pushes)
        self.assertEqual(self.c.wr(DEV_SRC, OWN_FAKE_FAULT, struct.pack("<BBi", 4, 0, 22000))[0], 0)   # VREG_OVP (i32)
        self.assertEqual(self.c.wr(DEV_SRC, OWN_FAKE_FAULT, struct.pack("<BBi", 10, 0, 7))[0], 0)      # UART_OVERFLOW (u16)
        self.c.pump(0.08)

        evs = [e for e in self.c.wire_events(n) if e[2] == DEV_SRC]
        self.assertEqual(len(evs), 2, evs)
        f1, tag1, src1, ch1, t1, fl1, ln1, d1 = evs[0]
        f2, tag2, src2, ch2, t2, fl2, ln2, d2 = evs[1]
        self.assertTrue(f1 & F_URGENT)
        self.assertTrue(fl1 & SYS_EVENT_NOTIFY)
        self.assertEqual((tag1, src1, ch1, ln1, struct.unpack("<i", d1)[0]), (EV_VREG_OVP, DEV_SRC, 0, 4, 22000))
        self.assertTrue(f2 & F_URGENT)
        self.assertTrue(fl2 & SYS_EVENT_NOTIFY)
        self.assertEqual((tag2, src2, ch2, ln2, struct.unpack("<H", d2)[0]), (EV_UART_OVERFLOW, DEV_SRC, 0, 2, 7))
        self.assertGreater(t1, 0)
        self.assertGreaterEqual(t2, t1)

    def test_non_notify_events_reach_local_listener_but_not_wire(self):
        self.assertEqual(self.c.wr(DEV_EAR1, OWN_LISTEN, struct.pack("<IHH", ANY_TAG, DEV_SRC, ANY_CH))[0], 0)
        n = len(self.c.link.pushes)
        # VREG_EV_READY (flags = 0, not NOTIFY) and IO_EV_RISING (flags = 0, not NOTIFY)
        self.assertEqual(self.c.wr(DEV_SRC, VREG_VOLTAGE, struct.pack("<I", 6000))[0], 0)
        self.assertEqual(self.c.wr(DEV_SRC, VREG_ENABLE, bytes([1]))[0], 0)
        self.assertEqual(self.c.wr(DEV_SRC, OWN_FAKE_FAULT, struct.pack("<BBi", 0, 1, 0))[0], 0)
        self.c.pump(0.08)

        # Local listener heard BOTH events
        cnt, tag, _, _, _, _ = unpack_heard(self.c.rd(DEV_EAR1, OWN_HEARD)[1])
        self.assertEqual((cnt, tag), (2, EV_IO_RISING))
        # Neither appeared on the wire event stream!
        wire_tags = [e[1] for e in self.c.wire_events(n) if e[2] == DEV_SRC]
        self.assertNotIn(EV_VREG_READY, wire_tags)
        self.assertNotIn(EV_IO_RISING, wire_tags)

    def test_device_lifecycle_notify_vs_non_notify_on_wire(self):
        n = len(self.c.link.pushes)
        self.assertEqual(self.c.wr(DEV_SRC, DEV_SUSPEND)[0], 0)
        self.assertEqual(self.c.wr(DEV_SRC, DEV_RESUME)[0], 0)
        self.assertEqual(self.c.wr(DEV_SRC, DEV_RESET)[0], 0)
        self.assertEqual(self.c.wr(DEV_SRC, DEV_REMOVE)[0], 0)
        self.c.pump(0.08)

        wire_tags = [e[1] for e in self.c.wire_events(n) if e[2] == DEV_SRC]
        self.assertEqual(wire_tags, [EV_DEV_SUSPENDED, EV_DEV_RESUMED, EV_DEV_RESET])
        self.assertNotIn(EV_DEV_REMOVED, wire_tags)


if __name__ == "__main__":
    unittest.main(verbosity=2)
