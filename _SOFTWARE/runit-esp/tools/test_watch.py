#!/usr/bin/env python3
"""Fast, thorough tests of sys_watch (SYS_WATCH.MD) using device_testdev on a board over the console session.

    python runit-esp/tools/test_watch.py                 # the one serial port found; RUNIT_PORT=COM10 to name it
    python runit-esp/tools/test_watch.py -v WatchOnChange

Uses short tick periods (1..6 ticks = 10..60 ms) so the whole suite finishes in a few seconds.
Leaves the board clean in tearDown: all watches removed, testdev removed.
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
UART_ID = sc.CLIENT
DEV_ID = 32  # IDs no board device takes
TESTDEV_TYPE = defs.device_type("testdev")

WATCH_ADD = defs.service_op("WATCH", "ADD")
WATCH_REMOVE = defs.service_op("WATCH", "REMOVE")
WATCH_LIST = defs.service_op("WATCH", "LIST")
TRIG_PERIODIC = defs.sym("SYS_WATCH_PERIODIC")
TRIG_ON_CHANGE = defs.sym("SYS_WATCH_ON_CHANGE")
WATCH_MAX = defs.limit("SYS_WATCH_MAX")

# Device ops
DEV_CREATE = defs.contract_op("DEVICE", "CREATE")
DEV_SUSPEND = defs.contract_op("DEVICE", "SUSPEND")
DEV_RESUME = defs.contract_op("DEVICE", "RESUME")
DEV_REMOVE = defs.contract_op("DEVICE", "REMOVE")
IO_LEVEL = defs.contract_op("IO", "LEVEL")
IO_MODE = defs.contract_op("IO", "MODE")
IO_RESET = defs.contract_op("IO", "RESET")
IO_VOLTAGE = defs.contract_op("IO", "VOLTAGE")
PMON_VOLTAGE = defs.contract_op("POWER_MONITOR", "VOLTAGE")
PMON_CURRENT = defs.contract_op("POWER_MONITOR", "CURRENT")
VREG_VOLTAGE = defs.contract_op("POWER_VREG", "VOLTAGE")
VREG_CURRENT = defs.contract_op("POWER_VREG", "CURRENT")
PD_OPTIONS = defs.contract_op("POWER_USB_PD", "OPTIONS")
SUPPLY_SOURCE = defs.contract_op("POWER_SUPPLY", "SOURCE")
OWN_FAKE_PIN = defs.device_own_op("testdev", "fake_pin")
OWN_FAKE_CHANNEL = defs.device_own_op("testdev", "fake_channel")
OWN_VREG = defs.device_own_op("testdev", "vreg")

MODE_OUTPUT_PP = defs.sym("SYS_IO_MODE_OUTPUT_PUSH_PULL")
MODE_ADC = defs.sym("SYS_IO_MODE_ADC")


def quiet():
    return contextlib.redirect_stdout(io.StringIO())


def dev_cmd(side: int, op: int, args: bytes = b"", dev_id: int = DEV_ID) -> bytes:
    return bytes([sc.KEY_DEVICE, side, dev_id]) + struct.pack("<H", op) + args


def watch_add(wid: int, trigger: int, period: int, max_silence: int, read_cmd: bytes) -> bytes:
    return bytes([sc.KEY_WATCH, WRITE, WATCH_ADD]) + struct.pack("<BBHH", wid, trigger, period, max_silence) + read_cmd


def watch_remove(wid: int) -> bytes:
    return bytes([sc.KEY_WATCH, WRITE, WATCH_REMOVE, wid])


def watch_list_rows(body: bytes) -> list:
    """u8 count + count * sys_watch_info_t (8 B: id, trigger, period, max_silence, owner, read_len)."""
    return [struct.unpack_from("<BBHHBB", body, 1 + 8 * i) for i in range(body[0])]


def testdev_cfg(pins: int = 16, channels: int = 4, uart_mode: int = 1, uart_baud: int = 115200) -> bytes:
    return struct.pack("<HB", TESTDEV_TYPE, 0) + struct.pack("<BBBI", pins, channels, uart_mode, uart_baud)


class WatchClient:
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

    def hello(self, wait: float = 0.08):
        self.link.send(sc.CONTROL, sc.KEY_SESSION, bytes([sc.HELLO]) + struct.pack("<II", self.link.boot_id, 0))
        with quiet():
            for kind, flags, fsrc, key, seq, b in self.link.frames(wait):
                if kind == sc.CONTROL and b[:1] == bytes([sc.HELLO]):
                    return True
                self.link._record_async(kind, flags, fsrc, key, seq, b)
        return False

    def send_no_session(self):
        self.link.send(sc.CONTROL, sc.KEY_SESSION, bytes([sc.NO_SESSION]))
        self.pump(0.05)

    def watch_entries(self, since: int = 0, wid: int | None = None):
        """Returns list of (owner, id, status, data) from KEY_WATCH pushes since index `since`."""
        out = []
        for _flags, _src, _seq, sid, body in self.link.pushes[since:]:
            if sid == sc.KEY_WATCH and len(body) >= 3:
                row = (body[0], body[1], body[2], body[3:])
                if wid is None or row[1] == wid:
                    out.append(row)
        return out

    def clear_all_watches(self):
        st, body = self.call(bytes([sc.KEY_WATCH, READ, WATCH_LIST]))
        if st == 0 and body:
            for wid, _trig, _per, _sil, owner, _rlen in watch_list_rows(body):
                if owner == UART_ID:
                    self.call(watch_remove(wid))


CLI: WatchClient | None = None


def setUpModule():
    global CLI
    port = os.environ.get("RUNIT_PORT")
    if not port:
        from serial.tools import list_ports
        ports = [p.device for p in list_ports.comports()]
        if len(ports) != 1:
            raise unittest.SkipTest(f"set RUNIT_PORT: serial ports found {ports}")
        port = ports[0]
    CLI = WatchClient(port)
    CLI.clear_all_watches()
    CLI.call(dev_cmd(WRITE, DEV_REMOVE))


class WatchBase(unittest.TestCase):
    def setUp(self):
        self.c = CLI
        self.c.clear_all_watches()
        self.c.call(dev_cmd(WRITE, DEV_REMOVE))
        self.assertEqual(self.c.call(dev_cmd(WRITE, DEV_CREATE, testdev_cfg()))[0], 0)

    def tearDown(self):
        self.c.clear_all_watches()
        self.c.call(dev_cmd(WRITE, DEV_REMOVE))


class WatchManagement(WatchBase):
    """ADD, LIST, REMOVE, replace, validation errors, and full table."""

    def test_add_list_replace_and_remove(self):
        r1 = dev_cmd(READ, PMON_VOLTAGE, bytes([0]))
        r2 = dev_cmd(READ, OWN_VREG)
        self.assertEqual(self.c.call(watch_add(1, TRIG_PERIODIC, 50, 0, r1))[0], 0)
        self.assertEqual(self.c.call(watch_add(7, TRIG_ON_CHANGE, 20, 40, r2))[0], 0)

        st, body = self.c.call(bytes([sc.KEY_WATCH, READ, WATCH_LIST]))
        self.assertEqual(st, 0)
        rows = {r[0]: r for r in watch_list_rows(body)}
        self.assertEqual(rows[1], (1, TRIG_PERIODIC, 50, 0, UART_ID, len(r1)))
        self.assertEqual(rows[7], (7, TRIG_ON_CHANGE, 20, 40, UART_ID, len(r2)))

        # Replace id 1 with new parameters
        self.assertEqual(self.c.call(watch_add(1, TRIG_ON_CHANGE, 10, 30, r2))[0], 0)
        rows = {r[0]: r for r in watch_list_rows(self.c.call(bytes([sc.KEY_WATCH, READ, WATCH_LIST]))[1])}
        self.assertEqual(len(rows), 2)
        self.assertEqual(rows[1], (1, TRIG_ON_CHANGE, 10, 30, UART_ID, len(r2)))

        # Remove and double-remove
        self.assertEqual(self.c.call(watch_remove(1))[0], 0)
        self.assertEqual(self.c.call(watch_remove(1))[0], 1)  # ERR_WATCH_NOT_FOUND
        self.assertEqual(self.c.call(watch_remove(7))[0], 0)
        self.assertEqual(self.c.call(bytes([sc.KEY_WATCH, READ, WATCH_LIST]))[1], bytes([0]))

    def test_validation_rejects_bad_args(self):
        good_read = dev_cmd(READ, SUPPLY_SOURCE)
        bad_write = dev_cmd(WRITE, SUPPLY_SOURCE, bytes([1]))
        self.assertEqual(self.c.call(watch_add(0, TRIG_PERIODIC, 5, 0, good_read))[0], 1)     # id 0
        self.assertEqual(self.c.call(watch_add(1, 2, 5, 0, good_read))[0], 1)                 # invalid trigger
        self.assertEqual(self.c.call(watch_add(1, TRIG_PERIODIC, 0, 0, good_read))[0], 1)     # period 0
        self.assertEqual(self.c.call(watch_add(1, TRIG_PERIODIC, 5, 0, bad_write))[0], 1)     # not a read
        self.assertEqual(self.c.call(watch_add(1, TRIG_PERIODIC, 5, 0, bytes([sc.KEY_SYS])))[0], 1) # len < 2
        self.assertEqual(self.c.call(watch_add(1, TRIG_PERIODIC, 5, 0, bytes([sc.KEY_SYS, 0]) + bytes(48)))[0], 1)  # len > 48

    def test_table_full_and_freed_on_remove(self):
        read = dev_cmd(READ, SUPPLY_SOURCE)
        for wid in range(1, WATCH_MAX + 1):
            self.assertEqual(self.c.call(watch_add(wid, TRIG_PERIODIC, 500, 0, read))[0], 0, f"slot {wid}")
        self.assertEqual(self.c.call(watch_add(WATCH_MAX + 1, TRIG_PERIODIC, 500, 0, read))[0], 1)  # ERR_FULL
        self.assertEqual(self.c.call(watch_remove(1))[0], 0)
        self.assertEqual(self.c.call(watch_add(WATCH_MAX + 1, TRIG_PERIODIC, 500, 0, read))[0], 0)


class WatchPeriodic(WatchBase):
    """PERIODIC trigger: cadence, live updates, multi-watch, and larger payloads."""

    def test_periodic_tracks_live_fake_channel_updates(self):
        self.c.link.push_gaps.clear()
        n = len(self.c.link.pushes)
        self.assertEqual(self.c.call(watch_add(1, TRIG_PERIODIC, 2, 0, dev_cmd(READ, PMON_CURRENT, bytes([0]))))[0], 0)
        self.c.pump(0.09)
        before = self.c.watch_entries(n, wid=1)
        self.assertGreaterEqual(len(before), 2)
        self.assertTrue(all(e[2] == 0 and struct.unpack("<i", e[3])[0] == 0 for e in before))

        n2 = len(self.c.link.pushes)
        self.assertEqual(self.c.call(dev_cmd(WRITE, OWN_FAKE_CHANNEL, struct.pack("<Bii", 0, 5000, 420)))[0], 0)
        self.c.pump(0.08)
        after = self.c.watch_entries(n2, wid=1)
        self.assertGreaterEqual(len(after), 2)
        self.assertEqual(struct.unpack("<i", after[-1][3])[0], 420)
        self.assertEqual(self.c.link.push_gaps, [])

    def test_multiple_simultaneous_watches_different_periods(self):
        n = len(self.c.link.pushes)
        self.assertEqual(self.c.call(watch_add(1, TRIG_PERIODIC, 1, 0, dev_cmd(READ, SUPPLY_SOURCE)))[0], 0)        # 10 ms
        self.assertEqual(self.c.call(watch_add(2, TRIG_PERIODIC, 4, 0, dev_cmd(READ, PMON_VOLTAGE, bytes([0]))))[0], 0)  # 40 ms
        self.assertEqual(self.c.call(watch_add(3, TRIG_PERIODIC, 2, 0, dev_cmd(READ, OWN_VREG)))[0], 0)             # 20 ms
        self.c.pump(0.14)
        e1 = self.c.watch_entries(n, wid=1)
        e2 = self.c.watch_entries(n, wid=2)
        e3 = self.c.watch_entries(n, wid=3)
        self.assertGreater(len(e1), len(e2))
        self.assertGreaterEqual(len(e2), 2)
        self.assertGreaterEqual(len(e3), 4)
        self.assertEqual(e1[-1][3], bytes([0]))
        self.assertEqual(struct.unpack("<i", e2[-1][3])[0], 5000)
        self.assertEqual(struct.unpack("<BII", e3[-1][3]), (0, 5000, 1000))

    def test_large_payload_pd_options(self):
        n = len(self.c.link.pushes)
        self.assertEqual(self.c.call(watch_add(1, TRIG_PERIODIC, 2, 0, dev_cmd(READ, PD_OPTIONS)))[0], 0)
        self.c.pump(0.06)
        entries = self.c.watch_entries(n, wid=1)
        self.assertGreaterEqual(len(entries), 1)
        self.assertEqual((entries[-1][2], len(entries[-1][3])), (0, 32))


class WatchOnChange(WatchBase):
    """ON_CHANGE trigger: dedup, single-byte diff, max_silence=0, and heartbeat."""

    def test_on_change_no_heartbeat_fires_only_when_value_changes(self):
        self.assertEqual(self.c.call(dev_cmd(WRITE, IO_MODE, bytes([3, MODE_ADC])))[0], 0)
        self.assertEqual(self.c.call(dev_cmd(WRITE, OWN_FAKE_PIN, struct.pack("<BBi", 3, 0, 1000)))[0], 0)

        n = len(self.c.link.pushes)
        self.assertEqual(self.c.call(watch_add(1, TRIG_ON_CHANGE, 1, 0, dev_cmd(READ, IO_VOLTAGE, bytes([3]))))[0], 0)
        self.c.pump(0.06)
        e = self.c.watch_entries(n, wid=1)
        self.assertEqual(len(e), 1)
        self.assertEqual(struct.unpack("<Bi", e[0][3]), (3, 1000))

        # Write same value: no extra push
        self.assertEqual(self.c.call(dev_cmd(WRITE, OWN_FAKE_PIN, struct.pack("<BBi", 3, 0, 1000)))[0], 0)
        self.c.pump(0.05)
        self.assertEqual(len(self.c.watch_entries(n, wid=1)), 1)

        # Change value twice
        for mv in (1850, 2400):
            self.assertEqual(self.c.call(dev_cmd(WRITE, OWN_FAKE_PIN, struct.pack("<BBi", 3, 0, mv)))[0], 0)
            self.c.pump(0.04)
        vals = [struct.unpack("<Bi", x[3])[1] for x in self.c.watch_entries(n, wid=1)]
        self.assertEqual(vals, [1000, 1850, 2400])

    def test_on_change_heartbeat_with_max_silence(self):
        n = len(self.c.link.pushes)
        # period=10 ms, max_silence=60 ms -> initial + ~2-3 heartbeats in 200 ms
        self.assertEqual(self.c.call(watch_add(2, TRIG_ON_CHANGE, 1, 6, dev_cmd(READ, SUPPLY_SOURCE)))[0], 0)
        self.c.pump(0.20)
        e = self.c.watch_entries(n, wid=2)
        self.assertTrue(2 <= len(e) <= 5, f"expected 2..5 heartbeats, got {len(e)}")
        self.assertTrue(all(x[2] == 0 and x[3] == bytes([0]) for x in e))

    def test_on_change_detects_single_field_change_in_struct(self):
        n = len(self.c.link.pushes)
        self.assertEqual(self.c.call(watch_add(3, TRIG_ON_CHANGE, 1, 0, dev_cmd(READ, OWN_VREG)))[0], 0)
        self.c.pump(0.04)
        self.assertEqual(len(self.c.watch_entries(n, wid=3)), 1)
        self.assertEqual(self.c.call(dev_cmd(WRITE, VREG_CURRENT, struct.pack("<I", 2345)))[0], 0)
        self.c.pump(0.04)
        e = self.c.watch_entries(n, wid=3)
        self.assertEqual(len(e), 2)
        self.assertEqual(struct.unpack("<BII", e[-1][3]), (0, 5000, 2345))


class WatchErrorsAndLifecycle(WatchBase):
    """Error deduplication, OK recovery, replace, device suspend/remove, and session end."""

    def test_error_reported_once_then_recovers_on_ok_and_errors_again(self):
        # Pin 0 is unconfigured -> IO_LEVEL read fails (period=3 ticks = 30 ms to avoid flooding 115200 console logs)
        n = len(self.c.link.pushes)
        self.assertEqual(self.c.call(watch_add(1, TRIG_PERIODIC, 3, 0, dev_cmd(READ, IO_LEVEL, bytes([0]))))[0], 0)
        self.c.pump(0.12)
        errs = [e for e in self.c.watch_entries(n, wid=1) if e[2] == 1]
        self.assertEqual(len(errs), 1, "consecutive errors must be suppressed after the first")

        # Configure pin 0 -> watch recovers and pushes OK
        n2 = len(self.c.link.pushes)
        self.assertEqual(self.c.call(dev_cmd(WRITE, IO_MODE, bytes([0, MODE_OUTPUT_PP])))[0], 0)
        self.c.pump(0.09)
        oks = [e for e in self.c.watch_entries(n2, wid=1) if e[2] == 0]
        self.assertGreaterEqual(len(oks), 1)
        self.assertEqual(oks[-1][3], bytes([0, 0]))

        # Reset pin 0 -> fails again and reports error once more
        n3 = len(self.c.link.pushes)
        self.assertEqual(self.c.call(dev_cmd(WRITE, IO_RESET, bytes([0])))[0], 0)
        self.c.pump(0.12)
        errs2 = [e for e in self.c.watch_entries(n3, wid=1) if e[2] == 1]
        self.assertEqual(len(errs2), 1)

    def test_watch_survives_suspend_errors_on_remove_recovers_on_recreate(self):
        self.assertEqual(self.c.call(watch_add(1, TRIG_PERIODIC, 3, 0, dev_cmd(READ, PMON_VOLTAGE, bytes([0]))))[0], 0)
        self.c.pump(0.08)

        # Suspend: reads are still allowed -> OK continues
        n = len(self.c.link.pushes)
        self.assertEqual(self.c.call(dev_cmd(WRITE, DEV_SUSPEND))[0], 0)
        self.c.pump(0.10)
        self.assertTrue(any(e[2] == 0 for e in self.c.watch_entries(n, wid=1)))

        # Remove: device gone -> error once (period=3 = 30 ms avoids flooding 115200 console logs)
        n = len(self.c.link.pushes)
        self.assertEqual(self.c.call(dev_cmd(WRITE, DEV_REMOVE))[0], 0)
        self.c.pump(0.12)
        errs = [e for e in self.c.watch_entries(n, wid=1) if e[2] == 1]
        self.assertEqual(len(errs), 1)

        # Recreate: watch recovers automatically
        n = len(self.c.link.pushes)
        self.assertEqual(self.c.call(dev_cmd(WRITE, DEV_CREATE, testdev_cfg()))[0], 0)
        self.c.pump(0.10)
        self.assertTrue(any(e[2] == 0 and struct.unpack("<i", e[3])[0] == 5000 for e in self.c.watch_entries(n, wid=1)))

    def test_replace_switches_payload_format_cleanly(self):
        self.assertEqual(self.c.call(watch_add(1, TRIG_PERIODIC, 1, 0, dev_cmd(READ, SUPPLY_SOURCE)))[0], 0)
        self.c.pump(0.04)
        self.assertEqual(self.c.call(watch_add(1, TRIG_PERIODIC, 1, 0, dev_cmd(READ, PMON_VOLTAGE, bytes([0]))))[0], 0)
        n = len(self.c.link.pushes)
        self.c.pump(0.05)
        entries = self.c.watch_entries(n, wid=1)
        self.assertGreaterEqual(len(entries), 2)
        self.assertTrue(all(e[2] == 0 and len(e[3]) == 4 for e in entries))

    def test_session_end_removes_watches(self):
        self.assertEqual(self.c.call(watch_add(1, TRIG_PERIODIC, 5, 0, dev_cmd(READ, SUPPLY_SOURCE)))[0], 0)
        self.assertEqual(self.c.call(bytes([sc.KEY_WATCH, READ, WATCH_LIST]))[1][0], 1)
        self.c.send_no_session()
        self.assertTrue(self.c.hello())
        self.assertEqual(self.c.call(bytes([sc.KEY_WATCH, READ, WATCH_LIST]))[1], bytes([0]))


if __name__ == "__main__":
    unittest.main(verbosity=2)
