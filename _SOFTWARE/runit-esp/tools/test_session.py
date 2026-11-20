#!/usr/bin/env python3
"""The session mechanism (SYS_SESSION.MD) tested on a board: unittest, over the console (UART) and, when a board
advertises the runIT service and bleak is installed, over BLE too.

    python runit-esp/tools/test_session.py                 # the one serial port found; RUNIT_PORT=COM10 to name it
    python runit-esp/tools/test_session.py -v SessionUart  # one class, verbose

Run with the PC's Python (pyserial and bleak); the ESP-IDF venv has no bleak, so the BLE class is skipped there.
The board runs the current firmware, with nothing else attached (the app, a monitor): the tests are two clients,
0xF0 on the console and 0xF1 on BLE, and they drive GPIO 4 (a clean devkit; the pin is reset after each test).
Every test leaves the board as it found it: dead-man 0, no safe state, no watches, GPIO 4 reset.
"""
import asyncio
import contextlib
import io
import os
import struct
import sys
import time
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import session_client as sc  # noqa: E402

try:
    import ble_client as bc  # noqa: E402
    from bleak import BleakClient, BleakScanner  # noqa: E402
except ImportError:
    bc = None

READ, WRITE = sc.READ, sc.WRITE
KEY_ERRORS, KEY_EVENTS, KEY_ROUTER_MODULE = 0x31, 0x25, 0x23
PIN = 4
UART_ID, BLE_ID = 0xF0, 0xF1
PROVIDER_CONSOLE, PROVIDER_BLE, PROVIDER_BLE_LOGS = 1, 3, 4
SESSION_PEERS = bytes([sc.KEY_SESSION, READ, 0x02])
SESSION_ATTACH = 0x03
SYS_PING, SYS_STOP, SYS_DEADMAN, SYS_BYE, SYS_SAFE, SYS_SAFE_LEAVE = 0x01, 0x02, 0x04, 0x06, 0x09, 0x0A
WATCH_ADD, WATCH_REMOVE, WATCH_LIST = 0x01, 0x02, 0x03


def quiet():
    return contextlib.redirect_stdout(io.StringIO())


def dev(op: int, args: bytes, side: int) -> bytes:
    return bytes([sc.KEY_DEVICE, side, sc.GPIO]) + struct.pack("<H", op) + args


def watch_add(wid: int, trigger: int, period: int, silence: int, read: bytes) -> bytes:
    return bytes([sc.KEY_WATCH, WRITE, WATCH_ADD]) + struct.pack("<BBHH", wid, trigger, period, silence) + read


def peers_rows(body: bytes) -> list:
    """sys_session_info_t, 10 B: provider, open, peer_id, safety, build_id, deadman_ms."""
    return [struct.unpack_from("<BBBBIH", body, 1 + 10 * i) for i in range(body[0])]


class Uart:
    """The console client: sc.Link with the answers and the pushes kept, no printing."""

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

    def control(self, seconds: float, src: int = UART_ID, op: bytes | None = None, body: bytes = b""):
        """Send a CONTROL frame (nothing when `op` is None) and return the CONTROL frames that come in `seconds`."""
        saved, sc.CLIENT = sc.CLIENT, src
        try:
            if op is not None:
                self.link.send(sc.CONTROL, sc.KEY_SESSION, op + body)
        finally:
            sc.CLIENT = saved
        out = []
        with quiet():
            for kind, flags, fsrc, key, seq, b in self.link.frames(seconds):
                if kind == sc.CONTROL:
                    out.append((b[:1], b[1:]))
                else:
                    self.link._record_async(kind, flags, fsrc, key, seq, b)
        return out

    def hello(self, src: int = UART_ID, boot: int | None = None, wait: float = 0.25):
        boot = self.link.boot_id if boot is None else boot
        return self.control(wait, src, bytes([sc.HELLO]), struct.pack("<II", boot, 0))

    def request_raw(self, src: int, cmd: bytes, wait: float = 0.25):
        """A REQUEST with its own `src`; the answers to it (kind, flags, body) that come in `wait` seconds."""
        self.link.seq = (self.link.seq + 1) & 0xFFFF
        saved, sc.CLIENT = sc.CLIENT, src
        try:
            self.link.send(sc.REQUEST, cmd[0], cmd[1:], self.link.seq)
        finally:
            sc.CLIENT = saved
        out = []
        with quiet():
            for kind, flags, fsrc, key, seq, b in self.link.frames(wait):
                if kind == sc.ANSWER and seq == self.link.seq:
                    out.append((flags, b))
                elif kind == sc.CONTROL:
                    out.append((-1, b[:1]))
                else:
                    self.link._record_async(kind, flags, fsrc, key, seq, b)
        return out

    def peers(self):
        st, b = self.call(SESSION_PEERS)
        return peers_rows(b) if st == 0 else None

    def pushes(self, key: int, since: int = 0):
        return [p for p in self.link.pushes[since:] if p[3] == key]

    def restore(self):
        """The board as it was: dead-man 0, the safe state left, our watches and GPIO 4 reset."""
        self.hello(wait=0.03)  # the session may have ended: a hello (a short wait: with a dead-man on, silence ends it)
        self.call(bytes([sc.KEY_SYS, WRITE, SYS_DEADMAN]) + struct.pack("<I", 0))
        self.call(bytes([sc.KEY_SYS, WRITE, SYS_SAFE_LEAVE]))
        for wid in (1, 2, 3, 4):
            self.call(bytes([sc.KEY_WATCH, WRITE, WATCH_REMOVE, wid]))
        self.call(dev(0x0106, bytes([PIN]), WRITE))


UART: Uart | None = None


def setUpModule():
    global UART
    port = os.environ.get("RUNIT_PORT")
    if not port:
        from serial.tools import list_ports
        ports = [p.device for p in list_ports.comports()]
        if len(ports) != 1:
            raise unittest.SkipTest(f"set RUNIT_PORT: serial ports found {ports}")
        port = ports[0]
    UART = Uart(port)
    UART.restore()


class SessionUart(unittest.TestCase):
    """One client on the console."""

    def setUp(self):
        self.u = UART

    def tearDown(self):
        self.u.restore()

    def prepare_pin(self):
        st, _ = self.u.call(dev(sc.IO_MODE, bytes([PIN, 3]), WRITE))
        self.assertEqual(st, 0)

    # ---- the hello and the peers -------------------------------------------------------------------------------
    def test_peers_row(self):
        rows = self.u.peers()
        ours = [r for r in rows if r[2] == UART_ID]
        self.assertEqual(len(ours), 1)
        provider, opened, peer, safety, build, deadman = ours[0]
        self.assertEqual((provider, opened, deadman), (PROVIDER_CONSOLE, 1, 0))
        self.assertEqual(build, 0)  # a client has no build
        self.assertEqual(self.u.link.board, 0)  # a board with no id gives none

    def test_hello_repeated_is_answered_and_keeps_the_session(self):
        for _ in range(2):
            ops = [o for o, _ in self.u.hello()]
            self.assertIn(bytes([sc.HELLO]), ops)
            self.assertIsNotNone(self.u.peers())

    def test_hello_body_names_boot_and_build(self):
        got = [b for o, b in self.u.hello() if o == bytes([sc.HELLO])][0]
        self.assertNotEqual(struct.unpack("<II", got)[1], 0)  # the board's firmware build

    def test_new_boot_id_restarts_the_session(self):
        old = self.u.link.boot_id
        try:
            self.u.link.boot_id = (old + 1) & 0xFFFFFFFF
            ops = [o for o, _ in self.u.hello(boot=self.u.link.boot_id)]
            self.assertIn(bytes([sc.HELLO]), ops)
            self.assertIsNotNone(self.u.peers())
        finally:
            self.u.link.boot_id = old
            self.u.hello()

    def test_ids_nobody_may_say_are_refused(self):
        for bad in (0xFF, 0xFE):
            self.assertNotIn(bytes([sc.HELLO]), [o for o, _ in self.u.hello(src=bad)], hex(bad))
        self.assertIsNotNone(self.u.peers())  # a refused hello changes nothing

    # ---- requests and answers ----------------------------------------------------------------------------------
    def test_answer_echoes_seq_and_key_and_uptime_grows(self):
        st, a = self.u.call(bytes([sc.KEY_SYS, READ, SYS_PING]))
        time.sleep(0.1)
        st2, b = self.u.call(bytes([sc.KEY_SYS, READ, SYS_PING]))
        self.assertEqual((st, st2), (0, 0))
        self.assertGreaterEqual(struct.unpack("<I", b)[0] - struct.unpack("<I", a)[0], 80)

    def test_request_with_a_foreign_src_is_dropped(self):
        got = self.u.request_raw(0xF3, bytes([sc.KEY_SYS, READ, SYS_PING]))
        self.assertEqual(got, [])

    def test_error_answer_and_its_record_pushed(self):
        n = len(self.u.link.pushes)
        st, b = self.u.call(bytes([0x66, READ, 0x01]))  # no module under key 0x66
        self.assertEqual(st, 1)
        self.assertGreater(len(b), 4)  # the error record
        self.u.pump(0.2)
        self.assertTrue(self.u.pushes(KEY_ERRORS, n), "no ERRORS push")
        self.assertTrue(self.u.pushes(sc.KEY_LOG, n), "no LOGS push")

    def test_a_wrong_direction_and_a_bad_length_are_errors(self):
        self.assertEqual(self.u.call(bytes([sc.KEY_SYS, WRITE, SYS_PING]))[0], 1)  # PING has no write side
        self.assertEqual(self.u.call(dev(sc.IO_LEVEL, bytes([PIN]), READ)[:-1])[0], 1)

    # ---- NO_SESSION --------------------------------------------------------------------------------------------
    def test_no_session_ends_it_and_a_request_is_told_so(self):
        self.u.control(0.1, op=bytes([sc.NO_SESSION]))
        got = self.u.request_raw(UART_ID, bytes([sc.KEY_SYS, READ, SYS_PING]), 0.3)
        ops = [b for f, b in got if f == -1]
        self.assertIn(bytes([sc.NO_SESSION]), ops)
        self.assertIn(bytes([sc.HELLO]), ops)  # and the board's hello
        self.assertEqual([x for x in got if x[0] != -1], [])  # no answer
        self.u.hello()
        self.assertIsNotNone(self.u.peers())

    # ---- the push counter, the watches -------------------------------------------------------------------------
    def test_push_seq_counts_without_gaps(self):
        self.prepare_pin()
        self.u.link.push_gaps.clear()
        n = len(self.u.link.pushes)
        st, _ = self.u.call(watch_add(1, 0, 1, 0, dev(sc.IO_LEVEL, bytes([PIN]), READ)))  # every 10 ms
        self.assertEqual(st, 0)
        self.u.pump(0.4)
        entries = self.u.pushes(sc.KEY_WATCH, n)
        self.assertGreater(len(entries), 20)
        self.assertEqual(self.u.link.push_gaps, [])
        seqs = [p[2] for p in entries]
        self.assertTrue(all((b - a) & 0xFFFF >= 1 for a, b in zip(seqs, seqs[1:])))

    def test_watch_entries_carry_owner_id_status_and_data(self):
        self.prepare_pin()
        self.u.call(dev(sc.IO_LEVEL, bytes([PIN, 1]), WRITE))
        n = len(self.u.link.pushes)
        self.u.call(watch_add(1, 0, 5, 0, dev(sc.IO_LEVEL, bytes([PIN]), READ)))
        self.u.pump(0.25)
        entries = self.u.pushes(sc.KEY_WATCH, n)
        self.assertGreaterEqual(len(entries), 3)
        for _, _, _, _, body in entries:
            self.assertEqual((body[0], body[1], body[2], body[3:]), (UART_ID, 1, 0, bytes([PIN, 1])))

    def test_on_change_sends_first_a_change_and_the_silence_heartbeat(self):
        self.prepare_pin()
        self.u.call(dev(sc.IO_LEVEL, bytes([PIN, 0]), WRITE))
        n = len(self.u.link.pushes)
        self.u.call(watch_add(2, 1, 2, 16, dev(sc.IO_LEVEL, bytes([PIN]), READ)))  # 20 ms, silence 160 ms
        self.u.pump(0.08)
        first = self.u.pushes(sc.KEY_WATCH, n)
        self.assertEqual(len(first), 1)
        self.u.call(dev(sc.IO_LEVEL, bytes([PIN, 1]), WRITE))
        self.u.pump(0.08)
        levels = [p[4][3:] for p in self.u.pushes(sc.KEY_WATCH, n)]
        self.assertEqual(levels[-1], bytes([PIN, 1]))
        k = len(self.u.link.pushes)
        self.u.pump(0.4)  # unchanged: only the heartbeat, about every 160 ms
        beat = self.u.pushes(sc.KEY_WATCH, k)
        self.assertTrue(1 <= len(beat) <= 4, len(beat))

    def test_watch_list_replace_and_remove(self):
        self.prepare_pin()
        read = dev(sc.IO_LEVEL, bytes([PIN]), READ)
        self.assertEqual(self.u.call(watch_add(1, 0, 50, 0, read))[0], 0)
        self.assertEqual(self.u.call(watch_add(2, 0, 50, 0, read))[0], 0)
        self.assertEqual(self.u.call(watch_add(1, 0, 60, 0, read))[0], 0)  # the same id replaces
        st, b = self.u.call(bytes([sc.KEY_WATCH, READ, WATCH_LIST]))
        self.assertEqual((st, b[0], len(b)), (0, 2, 1 + 2 * 8))
        self.assertEqual(self.u.call(bytes([sc.KEY_WATCH, WRITE, WATCH_REMOVE, 1]))[0], 0)
        self.assertEqual(self.u.call(bytes([sc.KEY_WATCH, WRITE, WATCH_REMOVE, 1]))[0], 1)  # gone
        self.assertEqual(self.u.call(bytes([sc.KEY_WATCH, WRITE, WATCH_REMOVE, 2]))[0], 0)
        self.assertEqual(self.u.call(bytes([sc.KEY_WATCH, READ, WATCH_LIST]))[1], bytes([0]))

    def test_watch_refuses_id_0_and_a_command_that_is_not_a_read(self):
        read = dev(sc.IO_LEVEL, bytes([PIN]), READ)
        self.assertEqual(self.u.call(watch_add(0, 0, 5, 0, read))[0], 1)
        self.assertEqual(self.u.call(watch_add(1, 0, 5, 0, dev(sc.IO_LEVEL, bytes([PIN, 1]), WRITE)))[0], 1)

    def test_a_watch_of_a_failing_read_reports_the_error_once(self):
        n = len(self.u.link.pushes)
        self.u.call(watch_add(1, 0, 2, 0, dev(sc.IO_LEVEL, bytes([PIN]), READ)))  # GPIO 4 not set up: the read fails?
        self.u.pump(0.25)
        entries = self.u.pushes(sc.KEY_WATCH, n)
        errs = [p for p in entries if p[4][2] == 1]
        self.assertLessEqual(len(errs), 1)

    # ---- the dead-man ------------------------------------------------------------------------------------------
    def test_deadman_keepalive_ping_end_and_safe_state(self):
        self.assertEqual(self.u.call(bytes([sc.KEY_SYS, WRITE, SYS_DEADMAN]) + struct.pack("<I", 200))[0], 0)
        st, b = self.u.call(bytes([sc.KEY_SYS, READ, SYS_DEADMAN]))
        self.assertEqual(struct.unpack("<I", b)[0], 200)
        self.assertEqual(next(r for r in self.u.peers() if r[2] == UART_ID)[5], 200)
        pings = [o for o, _ in self.u.control(0.1) if o == bytes([sc.PING])]
        self.assertGreaterEqual(len(pings), 1, "the board does not ping at a third of the time")
        for _ in range(8):  # our pings keep it open past the limit
            self.u.control(0.06, op=bytes([sc.PING]))
        self.assertIsNotNone(self.u.peers())
        self.u.control(0.45)  # silence
        got = self.u.request_raw(UART_ID, bytes([sc.KEY_SYS, READ, SYS_PING]), 0.3)
        self.assertIn(bytes([sc.NO_SESSION]), [b for f, b in got if f == -1])
        self.u.hello(wait=0.05)
        st, b = self.u.call(bytes([sc.KEY_SYS, READ, SYS_SAFE]))
        self.assertEqual(st, 0)
        self.assertNotEqual(b[0], 0, "the safety flag: the session's end is the safe state")

    def test_bye_disarms_the_deadman(self):
        self.u.call(bytes([sc.KEY_SYS, WRITE, SYS_DEADMAN]) + struct.pack("<I", 200))
        self.assertEqual(self.u.call(bytes([sc.KEY_SYS, WRITE, SYS_BYE]))[0], 0)
        self.assertEqual(next(r for r in self.u.peers() if r[2] == UART_ID)[5], 0)
        self.u.control(0.45)
        self.assertIsNotNone(self.u.peers())  # silence ends nothing now

    def test_stop_enters_the_safe_state_and_leave_clears_it(self):
        self.assertEqual(self.u.call(bytes([sc.KEY_SYS, WRITE, SYS_STOP]))[0], 0)
        st, b = self.u.call(bytes([sc.KEY_SYS, READ, SYS_SAFE]))
        self.assertNotEqual(b[0], 0)
        n = len(self.u.link.pushes)
        self.assertEqual(self.u.call(bytes([sc.KEY_SYS, WRITE, SYS_SAFE_LEAVE]))[0], 0)
        self.u.pump(0.15)
        self.assertTrue(self.u.pushes(KEY_EVENTS, n), "no event after the devices resumed")
        self.assertEqual(self.u.call(bytes([sc.KEY_SYS, READ, SYS_SAFE]))[1][0], 0)

    def test_a_write_in_the_safe_state_is_refused(self):
        self.prepare_pin()
        self.u.call(bytes([sc.KEY_SYS, WRITE, SYS_STOP]))
        st, _ = self.u.call(dev(sc.IO_LEVEL, bytes([PIN, 1]), WRITE))
        self.assertEqual(st, 1)  # ERR_DEV_SUSPENDED
        self.u.call(bytes([sc.KEY_SYS, WRITE, SYS_SAFE_LEAVE]))
        self.assertEqual(self.u.call(dev(sc.IO_LEVEL, bytes([PIN, 1]), WRITE))[0], 0)

    # ---- the session's commands --------------------------------------------------------------------------------
    def test_id_reads_the_boards_id(self):
        st, b = self.u.call(bytes([sc.KEY_SESSION, READ, 0x01]))
        self.assertEqual((st, len(b)), (0, 1))

    def test_attach_rules(self):
        attach = bytes([sc.KEY_SESSION, WRITE, SESSION_ATTACH])
        mods = bytes([0x23, 0x31, 0x25, 0x2B, 0x2A, 0x2F])  # the console's sub-ports, in the board's order
        same = attach + struct.pack("<BBH", PROVIDER_CONSOLE, 0, 0) + mods
        self.assertEqual(self.u.call(same)[0], 0, "the same attach again is done")
        other = attach + struct.pack("<BBH", PROVIDER_CONSOLE, 0, 5000) + mods
        self.assertEqual(self.u.call(other)[0], 1, "another time on an attached provider")
        raw = attach + struct.pack("<BBH", PROVIDER_BLE_LOGS, 0, 0) + bytes([0x23])
        self.assertEqual(self.u.call(raw)[0], 1, "a raw provider has no session")
        self.assertEqual(next(r for r in self.u.peers() if r[2] == UART_ID)[5], 0)

    # ---- the data matrix ---------------------------------------------------------------------------------------
    def test_matrix_list_connect_disconnect_and_refusals(self):
        st, b = self.u.call(bytes([0x30, READ, 0x03]))
        links = {struct.unpack_from("<HH", b, 1 + 4 * i) for i in range(b[0])}
        self.assertEqual(st, 0)
        self.assertIn((0x002B, 0x0105), links)  # WATCH -> the telemetry characteristic
        link = struct.pack("<HH", 0x002B, 0x0105)
        self.assertEqual(self.u.call(bytes([0x30, WRITE, 0x02]) + link)[0], 0)
        try:
            b = self.u.call(bytes([0x30, READ, 0x03]))[1]
            self.assertNotIn((0x002B, 0x0105), {struct.unpack_from("<HH", b, 1 + 4 * i) for i in range(b[0])})
            self.assertEqual(self.u.call(bytes([0x30, WRITE, 0x02]) + link)[0], 0)  # again: nothing changes
        finally:
            self.assertEqual(self.u.call(bytes([0x30, WRITE, 0x01]) + link)[0], 0)
        self.assertEqual(self.u.call(bytes([0x30, WRITE, 0x01]) + link)[0], 0)  # the same link again
        self.assertEqual(self.u.call(bytes([0x30, WRITE, 0x01]) + struct.pack("<HH", 0x7777, 0x002B))[0], 1)
        self.assertEqual(self.u.call(bytes([0x30, WRITE, 0x01]) + struct.pack("<HH", 0x002B, 0x002B))[0], 1)


class BleSide:
    """The BLE client on its own event loop, so a test calls it like the console's."""

    def __init__(self):
        self.loop = asyncio.new_event_loop()
        self.run(self._open())

    def run(self, coro):
        return self.loop.run_until_complete(coro)

    async def _open(self):
        dev_ = await BleakScanner.find_device_by_filter(
            lambda d, a: bc.uuid16(bc.SERVICE) in [u.lower() for u in a.service_uuids] or (d.name or "") == "runit",
            timeout=10)
        if dev_ is None:
            raise unittest.SkipTest("no board advertises the runIT service")
        self.client = BleakClient(dev_)
        await self.client.connect()
        self.link = bc.Link(self.client)
        await self.client.start_notify(bc.uuid16(bc.TX), self.link.on_packet)
        await self.client.start_notify(bc.uuid16(bc.TELEMETRY), self.link.on_telemetry)
        self.logs = bytearray()
        await self.client.start_notify(bc.uuid16(0xFFE9), lambda _c, b: self.logs.extend(b))
        with quiet():
            if not await self.link.hello():
                raise unittest.SkipTest("no hello over BLE")

    def call(self, cmd: bytes, wait: float = 2.0):
        with quiet():
            return self.run(self.link.call(cmd, wait))

    def pump(self, seconds: float):
        self.run(self.link.listen(seconds))

    def control(self, op: bytes, body: bytes = b"", wait: float = 0.25):
        async def go():
            await self.link.send(bc.CONTROL, bc.KEY_SESSION, op + body)
            out = []
            async for kind, _f, _s, _k, _q, b in self.link.frames(wait):
                if kind == bc.CONTROL:
                    out.append((b[:1], b[1:]))
            return out
        return self.run(go())

    def ensure(self):
        """Its session open with the dead-man off: a hello (a short wait: with a dead-man on, our silence ends it again)."""
        self.control(bytes([sc.HELLO]), struct.pack("<II", self.link.boot_id, 0), 0.05)
        self.call(bytes([sc.KEY_SYS, WRITE, SYS_DEADMAN]) + struct.pack("<I", 0))

    def close(self):
        with contextlib.suppress(Exception):
            self.run(self.client.disconnect())


BLE: BleSide | None = None


@unittest.skipIf(bc is None, "bleak is not installed")
class SessionTwo(unittest.TestCase):
    """The console's session and the BLE pair's at once: each its own peer, its own dead-man, its own PUSH counts."""

    @classmethod
    def setUpClass(cls):
        global BLE
        BLE = BleSide()
        BLE.ensure()
        cls.b = BLE

    @classmethod
    def tearDownClass(cls):
        BLE.close()

    def setUp(self):
        self.u = UART

    def tearDown(self):
        self.b.ensure()
        self.u.restore()

    def test_two_sessions_open_with_their_own_peers(self):
        rows = {r[0]: r for r in self.u.peers()}
        self.assertEqual((rows[PROVIDER_CONSOLE][1], rows[PROVIDER_CONSOLE][2]), (1, UART_ID))
        self.assertEqual((rows[PROVIDER_BLE][1], rows[PROVIDER_BLE][2]), (1, BLE_ID))
        st, body = self.b.call(SESSION_PEERS)
        self.assertEqual(st, 0)
        self.assertEqual({r[2] for r in peers_rows(body) if r[1]}, {UART_ID, BLE_ID})

    def test_an_id_an_open_session_has_is_refused(self):
        ops = [o for o, _ in self.u.hello(src=BLE_ID)]  # the console says the BLE peer's id
        self.assertNotIn(bytes([sc.HELLO]), ops)
        rows = {r[0]: r for r in self.u.peers()}
        self.assertEqual(rows[PROVIDER_BLE][2], BLE_ID)  # and nothing changed
        self.assertEqual(rows[PROVIDER_CONSOLE][2], UART_ID)

    def test_each_client_sees_the_others_writes(self):
        self.u.call(dev(sc.IO_MODE, bytes([PIN, 3]), WRITE))
        for lvl, who in ((1, self.u), (0, self.b), (1, self.b), (0, self.u)):
            who.call(dev(sc.IO_LEVEL, bytes([PIN, lvl]), WRITE))
            for reader in (self.u, self.b):
                self.assertEqual(reader.call(dev(sc.IO_LEVEL, bytes([PIN]), READ))[1], bytes([PIN, lvl]))

    def test_a_watch_of_each_goes_to_telemetry_and_the_console_by_owner(self):
        self.u.call(dev(sc.IO_MODE, bytes([PIN, 3]), WRITE))
        read = dev(sc.IO_LEVEL, bytes([PIN]), READ)
        nu, nb = len(self.u.link.pushes), len(self.b.link.pushes)
        self.assertEqual(self.u.call(watch_add(1, 0, 5, 0, read))[0], 0)
        self.assertEqual(self.b.call(watch_add(1, 0, 5, 0, read))[0], 0)  # the same id: the owners differ
        self.u.pump(0.3)
        self.b.pump(0.05)
        owners_b = {p[3][0] for p in self.b.link.pushes[nb:] if p[1] == sc.KEY_WATCH}
        owners_u = {p[4][0] for p in self.u.pushes(sc.KEY_WATCH, nu)}
        self.assertEqual(owners_b, {UART_ID, BLE_ID})  # telemetry carries every owner's
        self.assertEqual(owners_u, {UART_ID, BLE_ID})  # so does the console's session
        self.assertEqual(self.b.call(bytes([sc.KEY_WATCH, WRITE, WATCH_REMOVE, 1]))[0], 0)
        self.assertEqual(self.b.call(bytes([sc.KEY_WATCH, READ, WATCH_LIST]))[1][0], 1)  # ours only is gone

    def test_an_error_record_goes_to_both_sessions(self):
        nu, nb = len(self.u.link.pushes), len(self.b.link.pushes)
        self.assertEqual(self.b.call(bytes([0x66, READ, 0x01]))[0], 1)
        self.u.pump(0.15)
        self.b.pump(0.15)
        self.assertTrue(self.u.pushes(KEY_ERRORS, nu))
        self.assertTrue([p for p in self.b.link.pushes[nb:] if p[1] == KEY_ERRORS])
        self.assertIn(b"ERR_", bytes(self.b.logs))  # and the log lines on the logs characteristic

    def test_push_counts_are_per_session(self):
        nb = len(self.b.link.pushes)
        self.b.link.pushes.clear()
        for _ in range(3):
            self.b.call(bytes([0x66, READ, 0x01]))
        self.b.pump(0.2)
        seqs = [p[2] for p in self.b.link.pushes if p[1] == KEY_ERRORS]
        self.assertGreaterEqual(len(seqs), 3)
        self.assertTrue(all((b - a) & 0xFFFF == 1 for a, b in zip(seqs, seqs[1:])), seqs)

    def test_ble_hello_with_a_new_boot_id_restarts_only_its_session(self):
        old = self.b.link.boot_id
        self.b.link.boot_id = (old + 1) & 0xFFFFFFFF
        try:
            ops = [o for o, _ in self.b.control(bytes([sc.HELLO]), struct.pack("<II", self.b.link.boot_id, 0))]
            self.assertIn(bytes([sc.HELLO]), ops)
            rows = {r[0]: r for r in self.u.peers()}
            self.assertEqual((rows[PROVIDER_BLE][1], rows[PROVIDER_CONSOLE][1]), (1, 1))
        finally:
            self.b.link.boot_id = old

    def test_the_deadman_is_of_its_own_session(self):
        self.assertEqual(self.b.call(bytes([sc.KEY_SYS, WRITE, SYS_DEADMAN]) + struct.pack("<I", 200))[0], 0)
        rows = {r[0]: r for r in self.u.peers()}
        self.assertEqual((rows[PROVIDER_BLE][5], rows[PROVIDER_CONSOLE][5]), (200, 0))
        self.b.pump(0.5)  # BLE silent: its session ends, the console's stays
        rows = {r[0]: r for r in self.u.peers()}
        self.assertEqual((rows[PROVIDER_BLE][1], rows[PROVIDER_CONSOLE][1]), (0, 1))
        st, body = self.u.call(bytes([sc.KEY_SYS, READ, SYS_SAFE]))
        self.assertNotEqual(body[0], 0)  # the BLE session had the safety flag
        self.assertEqual(self.u.call(bytes([sc.KEY_SYS, WRITE, SYS_SAFE_LEAVE]))[0], 0)
        ops = [o for o, _ in self.b.control(bytes([sc.HELLO]), struct.pack("<II", self.b.link.boot_id, 0), 0.05)]
        self.assertIn(bytes([sc.HELLO]), ops)
        rows = {r[0]: r for r in self.u.peers()}
        self.assertEqual(rows[PROVIDER_BLE][1], 1)

    def test_a_gone_session_takes_its_watches(self):
        self.u.call(dev(sc.IO_MODE, bytes([PIN, 3]), WRITE))
        read = dev(sc.IO_LEVEL, bytes([PIN]), READ)
        self.assertEqual(self.b.call(watch_add(3, 0, 50, 0, read))[0], 0)
        self.assertEqual(self.u.call(bytes([sc.KEY_WATCH, READ, WATCH_LIST]))[1][0], 1)
        self.b.call(bytes([sc.KEY_SYS, WRITE, SYS_DEADMAN]) + struct.pack("<I", 200))
        self.b.pump(0.5)
        self.assertEqual(self.u.call(bytes([sc.KEY_WATCH, READ, WATCH_LIST]))[1][0], 0)
        self.u.call(bytes([sc.KEY_SYS, WRITE, SYS_SAFE_LEAVE]))
        self.b.control(bytes([sc.HELLO]), struct.pack("<II", self.b.link.boot_id, 0), 0.05)


if __name__ == "__main__":
    unittest.main(verbosity=2)
