#!/usr/bin/env python3
"""Standalone, thorough BLE hardware test suite for runit-esp (sys_ble + provider_ble + data_matrix).

    python runit-esp/tools/test_ble.py                          # full suite (incl. PC BT radio OFF/ON & throughput)
    python runit-esp/tools/test_ble.py -v BleRuntimeGattLifecycle
    RUNIT_BLE_NO_TOGGLE=1 python runit-esp/tools/test_ble.py    # skip PC Bluetooth adapter OFF/ON test

Separate from the fast serial test suites because BLE GATT table rebuilds, reconnects,
Windows radio OFF/ON recovery, and throughput bursts take ~30-60 s total.

All module IDs, router keys, op codes, event tags, UUIDs, and limits are loaded dynamically
from `runit-app/generated/` via `runit_defs.py` (never hardcoded).

If a console serial port (e.g. COM10 / RUNIT_PORT) is also connected, the radio OFF/ON test
additionally verifies the board's internal BLE STATUS and SESSION PEERS state while the PC's
Bluetooth radio is powered off.
"""
from __future__ import annotations

import asyncio
import contextlib
import io
import os
import struct
import sys
import time
import unittest
import zlib

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import runit_defs as defs  # noqa: E402
import session_client as sc  # noqa: E402

from bleak import BleakClient, BleakScanner  # noqa: E402

try:
    from winrt.windows.devices.radios import Radio, RadioKind, RadioState  # noqa: E402
except ImportError:
    Radio = RadioKind = RadioState = None

# ---- Synced protocol definitions (from runit-app/generated/) -----------------------------------

READ = defs.sym("CMD_DIR_READ")
WRITE = defs.sym("CMD_DIR_WRITE")
REQUEST = defs.sym("CMD_REQUEST")
ANSWER = defs.sym("CMD_ANSWER")
PUSH = defs.sym("CMD_PUSH")
CONTROL = defs.sym("CMD_CONTROL")
F_URGENT = defs.sym("ROUTER_FLAG_URGENT")
F_ERROR = defs.sym("CMD_TYPE_STATUS")
CMD_FRAME_MAX = defs.sym("CMD_FRAME_MAX")
CMD_SEND_NOWAIT = defs.sym("CMD_SEND_NOWAIT")

CLIENT_BLE = defs.sym("CMD_SRC_CLIENT_FIRST") + 1
HELLO = defs.sym("SYS_SESSION_HELLO")

KEY_SYS = defs.sym("MODULE_ID_SYS")
KEY_SESSION = defs.sym("MODULE_ID_SESSION")
KEY_WATCH = defs.sym("MODULE_ID_WATCH")
KEY_DEVICE = defs.sym("MODULE_ID_SYS_DEVICE")
KEY_MATRIX = defs.sym("MODULE_ID_DATA_MATRIX")
KEY_BLE = defs.sym("MODULE_ID_BLE")
KEY_PROVIDER = defs.sym("MODULE_ID_PROVIDER")
KEY_ERRORS = defs.sym("MODULE_ID_ERRORS")
KEY_EVENTS = defs.sym("MODULE_ID_SYS_EVENT")

SYS_PING = defs.service_op("SYS", "PING")
SYS_STOP = defs.service_op("SYS", "STOP")
SYS_SAFE = defs.service_op("SYS", "SAFE")
SYS_SAFE_LEAVE = defs.service_op("SYS", "SAFE_LEAVE")
SESSION_PEERS = defs.service_op("SESSION", "PEERS")
BLE_STATUS = defs.service_op("BLE", "STATUS")
BLE_LIST = defs.service_op("BLE", "LIST")
WATCH_ADD = defs.service_op("WATCH", "ADD")
WATCH_REMOVE = defs.service_op("WATCH", "REMOVE")
MATRIX_CONNECT = defs.service_op("DATA_MATRIX", "CONNECT")
MATRIX_DISCONNECT = defs.service_op("DATA_MATRIX", "DISCONNECT")
MATRIX_LIST = defs.service_op("DATA_MATRIX", "LIST")

PROV_CREATE = (KEY_PROVIDER << 8) | defs.service_op("PROVIDER", "CREATE")
PROV_REMOVE = (KEY_PROVIDER << 8) | defs.service_op("PROVIDER", "REMOVE")
PROV_LIST = (KEY_PROVIDER << 8) | defs.service_op("PROVIDER", "LIST")
PROV_TYPE_BLE = defs.sym("SYS_DATA_PROVIDER_TYPE_BLE")
PROV_BLE_FRAMED = 0x01
PROV_ID_NONE = 0xFF
PROV_SYSTEM_BLE = 3  # RUNIT_PROVIDER_BLE

TRIG_PERIODIC = defs.sym("SYS_WATCH_PERIODIC")

SVC_RUNIT = defs.sym("RUNIT_BLE_SERVICE")
CHR_SESSION = defs.sym("RUNIT_BLE_SESSION")
CHR_LOGS = defs.sym("RUNIT_BLE_LOGS")
CHR_TELEM = defs.sym("RUNIT_BLE_TELEMETRY")

FRAG_FIRST = defs.sym("SYS_BLE_FRAG_FIRST")
FRAG_LAST = defs.sym("SYS_BLE_FRAG_LAST")
FRAG_MASK = defs.sym("SYS_BLE_FRAG_COUNT_MASK")

DEV_CREATE = defs.contract_op("DEVICE", "CREATE")
DEV_REMOVE = defs.contract_op("DEVICE", "REMOVE")
TESTDEV_TYPE = defs.device_type("testdev")
PMON_VOLTAGE = defs.contract_op("POWER_MONITOR", "VOLTAGE")
SUPPLY_SOURCE = defs.contract_op("POWER_SUPPLY", "SOURCE")
I2C_RAW = defs.contract_op("I2C", "RAW")
OWN_FAKE_CHANNEL = defs.device_own_op("testdev", "fake_channel")

PORT_KIND_MODULE = 0
PORT_KIND_PROVIDER = 1
UUID_USER_DESC = 0x2901

# Runtime test provider / device IDs (cleaned up in tearDown)
DEV_TEST = 28
PROV_P1, PROV_P2 = 20, 21
PROV_MON = 22
PROV_RAW_IN, PROV_RAW_OUT = 23, 24
PROV_FRM_IN, PROV_FRM_OUT = 25, 26


def uuid16(u: int) -> str:
    return f"0000{u:04x}-0000-1000-8000-00805f9b34fb"


def port_id(kind: int, ident: int) -> int:
    return ((kind & 0xFF) << 8) | (ident & 0xFF)


def quiet():
    return contextlib.redirect_stdout(io.StringIO())


def prov_cmd(prov_id: int, side: int, op: int, args: bytes = b"") -> bytes:
    return bytes([KEY_PROVIDER, side, prov_id]) + struct.pack("<H", op) + args


def prov_ble_create(
    prov_id: int,
    svc: int,
    chr: int,
    flags: int = 0,
    rx_flags: int = CMD_SEND_NOWAIT,
    name: str = "",
) -> bytes:
    raw_name = name.encode("ascii")[:15].ljust(16, b"\x00")
    cfg = struct.pack("<HHBB16s", svc, chr, flags, rx_flags, raw_name)
    return prov_cmd(prov_id, WRITE, PROV_CREATE, struct.pack("<BB", PROV_TYPE_BLE, 0) + cfg)


def prov_remove(prov_id: int) -> bytes:
    return prov_cmd(prov_id, WRITE, PROV_REMOVE)


def dev_cmd(dev_id: int, side: int, op: int, args: bytes = b"") -> bytes:
    return bytes([KEY_DEVICE, side, dev_id]) + struct.pack("<H", op) + args


def testdev_cfg(pins: int = 16, channels: int = 4, uart_mode: int = 1, uart_baud: int = 115200) -> bytes:
    return struct.pack("<HB", TESTDEV_TYPE, 0) + struct.pack("<BBBI", pins, channels, uart_mode, uart_baud)


def matrix_link(op: int, src_port: int, dst_port: int) -> bytes:
    return bytes([KEY_MATRIX, WRITE, op]) + struct.pack("<HH", src_port, dst_port)


def parse_ble_list(body: bytes) -> dict[int, dict]:
    """Parse BLE LIST answer into {svc_uuid: {'flags': int, 'chars': {chr_uuid: (flags, tx, name)}}}."""
    pos = 1
    svc_count = body[0]
    out: dict[int, dict] = {}
    for _ in range(svc_count):
        suuid, sflags, n = struct.unpack_from("<HBB", body, pos)
        pos += 4
        chars: dict[int, tuple[int, int, str]] = {}
        for _ in range(n):
            cuuid, cflags, tx = struct.unpack_from("<HBH", body, pos)
            pos += 5
            nul = body.index(0, pos)
            cname = body[pos:nul].decode("utf-8", "replace")
            pos = nul + 1
            chars[cuuid] = (cflags, tx, cname)
        out[suuid] = {"flags": sflags, "chars": chars}
    return out


def parse_ble_status(body: bytes) -> tuple[int, int]:
    return struct.unpack("<BH", body)


def parse_peers(body: bytes) -> list[tuple[int, int, int, int, int, int, int]]:
    return [struct.unpack_from("<BBBBIHB", body, 1 + 11 * i) for i in range(body[0])]  # sys_session_info_t, 11 B


def frag_encode(payload: bytes, step: int) -> list[bytes]:
    """Encode a framed BLE payload into packets of at most `step` data bytes (+ 1 header byte)."""
    parts = [payload[i : i + step] for i in range(0, len(payload), step)] or [b""]
    pkts = []
    for i, part in enumerate(parts):
        hdr = (FRAG_FIRST if i == 0 else 0) | (FRAG_LAST if i == len(parts) - 1 else 0) | (i & FRAG_MASK)
        pkts.append(bytes([hdr]) + part)
    return pkts


class FragDecoder:
    """Reassembles framed BLE notifications into whole messages."""

    def __init__(self):
        self.buf = bytearray()
        self.active = False
        self.expected = 0
        self.errors = 0
        self.frames: asyncio.Queue[bytes] = asyncio.Queue()

    def feed(self, data: bytes | bytearray):
        if not data:
            return
        hdr = data[0]
        if hdr & FRAG_FIRST:
            self.buf = bytearray()
            self.active = True
            self.expected = 0
        if not self.active or (hdr & FRAG_MASK) != self.expected:
            self.errors += 1
            self.active = False
            return
        self.buf += data[1:]
        self.expected = (self.expected + 1) & FRAG_MASK
        if hdr & FRAG_LAST:
            self.active = False
            self.frames.put_nowait(bytes(self.buf))


class BleSession:
    """Async BLE session over the board's runIT service (0xFFE0)."""

    def __init__(self, client: BleakClient):
        self.client = client
        self.seq = 0
        self.boot_id = int(time.time() * 1000) & 0xFFFFFFFF
        self.board: int | None = None
        self.rx_q: asyncio.Queue = asyncio.Queue()
        self.pushes: list[tuple[int, int, int, int, bytes]] = []  # (flags, src, key, seq, body)
        self.telem: list[tuple[int, int, int, bytes]] = []  # (owner, wid, status, data)
        self.logs: bytearray = bytearray()
        self._dec = FragDecoder()

    def _on_tx(self, _char, data: bytearray):
        self._dec.feed(data)
        while not self._dec.frames.empty():
            f = self._dec.frames.get_nowait()
            if len(f) >= 5:
                kind = f[0] >> 6
                flags = f[0] & 0x3F
                src, key = f[1], f[2]
                seq = struct.unpack_from("<H", f, 3)[0]
                self.rx_q.put_nowait((kind, flags, src, key, seq, f[5:]))

    def _on_telem(self, _char, data: bytearray):
        if len(data) >= 8 and (data[0] >> 6) == PUSH:
            body = bytes(data[5:])
            self.telem.append((body[0], body[1], body[2], body[3:]))

    def _on_logs(self, _char, data: bytearray):
        self.logs.extend(data)

    async def start(self):
        await self.client.start_notify(uuid16(CHR_SESSION), self._on_tx)
        await self.client.start_notify(uuid16(CHR_TELEM), self._on_telem)
        await self.client.start_notify(uuid16(CHR_LOGS), self._on_logs)

    async def send(self, kind: int, key: int, body: bytes, seq: int = 0, flags: int = 0):
        frame = bytes([(kind << 6) | flags, CLIENT_BLE, key]) + struct.pack("<H", seq) + body
        step = max(1, self.client.mtu_size - 4)
        for pkt in frag_encode(frame, step):
            await self.client.write_gatt_char(uuid16(CHR_SESSION), pkt, response=True)

    async def hello(self, timeout: float = 4.0) -> bool:
        await self.send(CONTROL, KEY_SESSION, bytes([HELLO]) + struct.pack("<II", self.boot_id, 0))
        end = time.monotonic() + timeout
        while (left := end - time.monotonic()) > 0:
            try:
                kind, _fl, src, _k, _s, b = await asyncio.wait_for(self.rx_q.get(), left)
            except asyncio.TimeoutError:
                return False
            if kind == CONTROL and b[:1] == bytes([HELLO]) and len(b) >= 9:
                self.board = src
                return True
        return False

    async def call(self, cmd: bytes, timeout: float = 3.0) -> tuple[int | None, bytes]:
        self.seq = (self.seq + 1) & 0xFFFF
        await self.send(REQUEST, cmd[0], cmd[1:], self.seq)
        end = time.monotonic() + timeout
        while (left := end - time.monotonic()) > 0:
            try:
                kind, flags, src, key, seq, b = await asyncio.wait_for(self.rx_q.get(), left)
            except asyncio.TimeoutError:
                return None, b""
            if kind == ANSWER and seq == self.seq:
                return (1 if (flags & F_ERROR) else 0), b
            if kind == PUSH:
                self.pushes.append((flags, src, key, seq, b))
        return None, b""

    async def pump(self, seconds: float):
        end = time.monotonic() + seconds
        while (left := end - time.monotonic()) > 0:
            try:
                kind, flags, src, key, seq, b = await asyncio.wait_for(self.rx_q.get(), left)
            except asyncio.TimeoutError:
                return
            if kind == PUSH:
                self.pushes.append((flags, src, key, seq, b))


async def find_board(timeout: float = 12.0):
    target_svc = uuid16(SVC_RUNIT).lower()
    return await BleakScanner.find_device_by_filter(
        lambda d, a: target_svc in [u.lower() for u in a.service_uuids] or (d.name or "").lower() == "runit",
        timeout=timeout,
    )


async def open_ble(retries: int = 3) -> tuple[BleakClient, BleSession]:
    """Scan, connect, subscribe, and open a BLE session (retrying transient Windows GATT glitches)."""
    last_err: Exception | None = None
    for attempt in range(retries):
        dev = await find_board(timeout=12.0)
        if dev is None:
            last_err = RuntimeError("runit board not found in BLE scan")
            await asyncio.sleep(0.5)
            continue
        client = BleakClient(dev, timeout=15.0)
        try:
            await client.connect()
            sess = BleSession(client)
            await sess.start()
            if not await sess.hello(timeout=4.0):
                raise RuntimeError("BLE HELLO timed out")
            return client, sess
        except Exception as e:
            last_err = e
            with contextlib.suppress(Exception):
                await client.disconnect()
            await asyncio.sleep(0.6)
    raise RuntimeError(f"failed to open BLE session after {retries} attempts: {last_err}")


async def close_ble(client: BleakClient | None):
    if client is not None:
        with contextlib.suppress(Exception):
            await client.disconnect()
    await asyncio.sleep(0.35)


async def set_pc_bluetooth(on: bool) -> bool:
    """Toggle Windows Bluetooth radio via WinRT. Returns True if switched, False if unavailable."""
    if Radio is None:
        return False
    radios = await Radio.get_radios_async()
    bt = next((r for r in radios if r.kind == RadioKind.BLUETOOTH), None)
    if bt is None:
        return False
    want = RadioState.ON if on else RadioState.OFF
    if bt.state != want:
        await bt.set_state_async(want)
    # Wait until radio actually reaches target state
    for _ in range(40):
        radios = await Radio.get_radios_async()
        bt = next((r for r in radios if r.kind == RadioKind.BLUETOOTH), None)
        if bt is not None and bt.state == want:
            return True
        await asyncio.sleep(0.15)
    return False


class OptionalConsole:
    """Optional serial console observer (COM10) for checking board state while PC BT is OFF."""

    def __init__(self):
        self.link: sc.Link | None = None
        port = os.environ.get("RUNIT_PORT")
        if not port:
            try:
                from serial.tools import list_ports
                ports = [p.device for p in list_ports.comports()]
                if len(ports) == 1:
                    port = ports[0]
            except Exception:
                port = None
        if port:
            try:
                link = sc.Link(port, 115200)
                with quiet():
                    if link.hello():
                        self.link = link
                    else:
                        link.s.close()
            except Exception:
                self.link = None

    def call(self, cmd: bytes, wait: float = 2.0):
        assert self.link is not None
        with quiet():
            return self.link.call(self.link.board, cmd, wait)

    def close(self):
        if self.link is not None:
            with contextlib.suppress(Exception):
                self.link.s.close()
            self.link = None


async def cleanup_runtime(sess: BleSession):
    """Remove any runtime test providers, watches, and testdev left over from a test."""
    for wid in range(1, 10):
        await sess.call(bytes([KEY_WATCH, WRITE, WATCH_REMOVE, wid]))
    await sess.call(bytes([KEY_SYS, WRITE, SYS_SAFE_LEAVE]))
    await sess.call(dev_cmd(DEV_TEST, WRITE, DEV_REMOVE))
    for pid in (PROV_FRM_OUT, PROV_FRM_IN, PROV_RAW_OUT, PROV_RAW_IN, PROV_MON, PROV_P2, PROV_P1):
        await sess.call(prov_remove(pid))


def run_async(coro):
    return asyncio.run(coro)


class BleConnectAndReconnect(unittest.TestCase):
    """Connection, GATT verification, repeated reconnects, and session RPC/events/errors over BLE."""

    def test_01_scan_connect_status_list_and_gatt_match(self):
        async def go():
            client, sess = await open_ble()
            try:
                st, b = await sess.call(bytes([KEY_SYS, READ, SYS_PING]))
                self.assertEqual((st, len(b)), (0, 4))

                st, b = await sess.call(bytes([KEY_BLE, READ, BLE_STATUS]))
                self.assertEqual(st, 0)
                connected, mtu = parse_ble_status(b)
                self.assertEqual(connected, 1)
                self.assertGreaterEqual(mtu, 23)
                print(f"\n  [BLE] connected=1, board_mtu={mtu}, bleak_mtu={client.mtu_size}")

                # Verify BLE LIST matches real Bleak GATT discovery for the runIT service
                st, b = await sess.call(bytes([KEY_BLE, READ, BLE_LIST]))
                self.assertEqual(st, 0)
                svcs = parse_ble_list(b)
                self.assertIn(SVC_RUNIT, svcs)
                runit_svc = svcs[SVC_RUNIT]
                self.assertEqual(runit_svc["flags"] & 0x03, 0x03)  # primary + locked
                for cuuid in (CHR_SESSION, CHR_LOGS, CHR_TELEM):
                    self.assertIn(cuuid, runit_svc["chars"])

                # Cross-check against Bleak's discovered GATT services
                bleak_svc = client.services.get_service(uuid16(SVC_RUNIT))
                self.assertIsNotNone(bleak_svc)
                bleak_chars = {int(c.uuid.split("-")[0], 16) for c in bleak_svc.characteristics}
                self.assertTrue({CHR_SESSION, CHR_LOGS, CHR_TELEM}.issubset(bleak_chars))
            finally:
                await close_ble(client)

        run_async(go())

    def test_02_repeated_disconnect_and_reconnect(self):
        async def go():
            for cycle in range(1, 4):
                client, sess = await open_ble()
                try:
                    st, b = await sess.call(bytes([KEY_SYS, READ, SYS_PING]))
                    self.assertEqual((st, len(b)), (0, 4), f"cycle {cycle} PING failed")
                    st, b = await sess.call(bytes([KEY_SESSION, READ, SESSION_PEERS]))
                    self.assertEqual(st, 0)
                    peers = parse_peers(b)
                    ble_row = next((r for r in peers if r[0] == PROV_SYSTEM_BLE), None)
                    self.assertIsNotNone(ble_row)
                    self.assertEqual((ble_row[1], ble_row[2]), (1, CLIENT_BLE))
                finally:
                    await close_ble(client)
            print("\n  [BLE] 3/3 disconnect+reconnect cycles OK")

        run_async(go())

    def test_03_commands_errors_and_events_over_ble_session(self):
        async def go():
            client, sess = await open_ble()
            try:
                sess.pushes.clear()
                # Unknown router key -> ERROR answer + error record PUSH on KEY_ERRORS
                st, b = await sess.call(bytes([0x66, READ, 0x01]))
                self.assertEqual(st, 1)
                self.assertGreater(len(b), 0)
                await sess.pump(0.15)
                self.assertTrue(any(k == KEY_ERRORS for _, _, k, _, _ in sess.pushes), "expected error PUSH over BLE")

                # STOP -> SAFE -> SAFE_LEAVE emits SYS_EVENT PUSH frames
                sess.pushes.clear()
                self.assertEqual((await sess.call(bytes([KEY_SYS, WRITE, SYS_STOP])))[0], 0)
                st, b = await sess.call(bytes([KEY_SYS, READ, SYS_SAFE]))
                self.assertEqual(st, 0)
                self.assertEqual(b[0], defs.sym("SYS_SAFE_STOP"))
                self.assertEqual((await sess.call(bytes([KEY_SYS, WRITE, SYS_SAFE_LEAVE])))[0], 0)
                await sess.pump(0.2)
                self.assertTrue(any(k == KEY_EVENTS for _, _, k, _, _ in sess.pushes), "expected event PUSH over BLE")
            finally:
                await cleanup_runtime(sess)
                await close_ble(client)

        run_async(go())


class BleRuntimeGattLifecycle(unittest.TestCase):
    """Runtime creation & removal of GATT services and characteristics via provider_ble."""

    def test_01_refusals_locked_service_duplicate_uuid_bad_cfg(self):
        async def go():
            client, sess = await open_ble()
            try:
                await cleanup_runtime(sess)
                # Locked runIT service -> refused
                self.assertEqual((await sess.call(prov_ble_create(PROV_P1, SVC_RUNIT, 0xEE90, name="bad")))[0], 1)
                # No characteristic -> refused
                self.assertEqual((await sess.call(prov_ble_create(PROV_P1, 0xEE00, 0, name="bad")))[0], 1)
                # Service UUID 0 -> refused
                self.assertEqual((await sess.call(prov_ble_create(PROV_P1, 0, 0xEE90, name="bad")))[0], 1)
                # Removing system BLE provider -> refused
                self.assertEqual((await sess.call(prov_remove(PROV_SYSTEM_BLE)))[0], 1)

                # Create P1, then attempt duplicate characteristic UUID in P2 -> refused
                self.assertEqual((await sess.call(prov_ble_create(PROV_P1, 0xEE00, 0xEE01, name="dup")))[0], 0)
                self.assertEqual((await sess.call(prov_ble_create(PROV_P2, 0xEE05, 0xEE01, name="dup2")))[0], 1)
                self.assertEqual((await sess.call(prov_remove(PROV_P1)))[0], 0)
                await asyncio.sleep(0.4)
            finally:
                await cleanup_runtime(sess)
                await close_ble(client)

        run_async(go())

    def test_02_create_join_remove_characteristics_and_service_live(self):
        async def go():
            # Step 1: Create service 0xEE00 via P1 (characteristic 0xEE01) and join via P2 (0xEE03)
            client, sess = await open_ble()
            try:
                await cleanup_runtime(sess)
                await asyncio.sleep(0.35)

                self.assertEqual((await sess.call(prov_ble_create(PROV_P1, 0xEE00, 0xEE01, name="p1")))[0], 0)
                st, b = await sess.call(bytes([KEY_BLE, READ, BLE_LIST]))
                self.assertEqual(st, 0)
                svcs = parse_ble_list(b)
                self.assertIn(0xEE00, svcs)
                self.assertEqual(set(svcs[0xEE00]["chars"].keys()), {0xEE01})
                self.assertEqual(svcs[0xEE00]["chars"][0xEE01][2], "p1")

                # Join existing service 0xEE00 with second provider P2
                self.assertEqual((await sess.call(prov_ble_create(PROV_P2, 0xEE00, 0xEE03, name="p2")))[0], 0)
                st, b = await sess.call(bytes([KEY_BLE, READ, BLE_LIST]))
                svcs = parse_ble_list(b)
                self.assertEqual(set(svcs[0xEE00]["chars"].keys()), {0xEE01, 0xEE03})
                self.assertEqual(svcs[0xEE00]["chars"][0xEE03][2], "p2")

                # Wait for CONFIG_SYS_BLE_APPLY_DELAY_MS (200 ms) so NimBLE commits the table
                await asyncio.sleep(0.5)
            finally:
                await close_ble(client)

            # Step 2: Reconnect and verify Bleak sees service 0xEE00 and both characteristics + user descriptions
            client, sess = await open_ble()
            try:
                svc = client.services.get_service(uuid16(0xEE00))
                self.assertIsNotNone(svc, "new runtime service 0xEE00 not visible in GATT")
                chars = {int(c.uuid.split("-")[0], 16): c for c in svc.characteristics}
                self.assertEqual(set(chars.keys()), {0xEE01, 0xEE03})

                # Check User Description descriptor (0x2901) on 0xEE03
                desc = chars[0xEE03].get_descriptor(uuid16(UUID_USER_DESC))
                self.assertIsNotNone(desc)
                val = await client.read_gatt_descriptor(desc.handle)
                self.assertEqual(val.decode("utf-8"), "p2")

                # Remove P2 only -> 0xEE03 disappears, 0xEE00 + 0xEE01 stay
                self.assertEqual((await sess.call(prov_remove(PROV_P2)))[0], 0)
                st, b = await sess.call(bytes([KEY_BLE, READ, BLE_LIST]))
                svcs = parse_ble_list(b)
                self.assertIn(0xEE00, svcs)
                self.assertEqual(set(svcs[0xEE00]["chars"].keys()), {0xEE01})
                await asyncio.sleep(0.5)
            finally:
                await close_ble(client)

            # Step 3: Reconnect and verify 0xEE03 is gone while 0xEE01 remains, then remove P1 (whole service)
            client, sess = await open_ble()
            try:
                svc = client.services.get_service(uuid16(0xEE00))
                self.assertIsNotNone(svc, "service 0xEE00 should still exist after removing P2")
                chars = {int(c.uuid.split("-")[0], 16) for c in svc.characteristics}
                self.assertEqual(chars, {0xEE01})

                self.assertEqual((await sess.call(prov_remove(PROV_P1)))[0], 0)
                st, b = await sess.call(bytes([KEY_BLE, READ, BLE_LIST]))
                svcs = parse_ble_list(b)
                self.assertNotIn(0xEE00, svcs)
                await asyncio.sleep(0.5)
            finally:
                await close_ble(client)

            # Step 4: Reconnect and verify service 0xEE00 is completely gone from GATT
            client, sess = await open_ble()
            try:
                self.assertIsNone(client.services.get_service(uuid16(0xEE00)), "service 0xEE00 still present in GATT")
                print("\n  [BLE] runtime service + char create / join / remove verified in GATT")
            finally:
                await close_ble(client)

        run_async(go())


class BleRoutingAndMatrix(unittest.TestCase):
    """Routing through data_matrix to/from runtime BLE characteristics (raw & framed)."""

    def test_01_route_watch_to_custom_ble_characteristic(self):
        async def go():
            client, sess = await open_ble()
            try:
                await cleanup_runtime(sess)
                self.assertEqual((await sess.call(prov_ble_create(PROV_MON, 0xEE10, 0xEE11, name="mon")))[0], 0)
                await asyncio.sleep(0.5)
            finally:
                await close_ble(client)

            client, sess = await open_ble()
            try:
                custom_rx: list[bytes] = []
                await client.start_notify(uuid16(0xEE11), lambda _c, d: custom_rx.append(bytes(d)))

                # Create testdev and route MODULE_ID_WATCH -> PROV_MON
                self.assertEqual((await sess.call(dev_cmd(DEV_TEST, WRITE, DEV_CREATE, testdev_cfg())))[0], 0)
                self.assertEqual(
                    (await sess.call(matrix_link(MATRIX_CONNECT, port_id(PORT_KIND_MODULE, KEY_WATCH), port_id(PORT_KIND_PROVIDER, PROV_MON))))[0],
                    0,
                )
                read_cmd = dev_cmd(DEV_TEST, READ, SUPPLY_SOURCE)
                head = struct.pack("<BBHH", 1, TRIG_PERIODIC, 2, 0)  # every 20 ms
                self.assertEqual((await sess.call(bytes([KEY_WATCH, WRITE, WATCH_ADD]) + head + read_cmd))[0], 0)

                await asyncio.sleep(0.45)
                got_before = len(custom_rx)
                self.assertGreaterEqual(got_before, 8, f"expected >=8 routed watch packets on 0xEE11, got {got_before}")
                # Every routed watch packet carries the 5-byte PUSH header + [owner][id][status][data]
                for pkt in custom_rx:
                    self.assertGreaterEqual(len(pkt), 8)
                    self.assertEqual(pkt[0] >> 6, PUSH)
                    self.assertEqual(pkt[2], KEY_WATCH)
                    self.assertEqual(pkt[6], 1)  # watch id 1
                    self.assertEqual(pkt[7], 0)  # status OK

                # Disconnect WATCH -> PROV_MON; packets must stop on 0xEE11 while continuing on TELEMETRY
                self.assertEqual(
                    (await sess.call(matrix_link(MATRIX_DISCONNECT, port_id(PORT_KIND_MODULE, KEY_WATCH), port_id(PORT_KIND_PROVIDER, PROV_MON))))[0],
                    0,
                )
                await asyncio.sleep(0.15)
                custom_rx.clear()
                sess.telem.clear()
                await asyncio.sleep(0.35)
                self.assertEqual(len(custom_rx), 0, "packets kept arriving on 0xEE11 after DISCONNECT")
                self.assertGreaterEqual(len(sess.telem), 5, "TELEMETRY should still receive watches")
                print(f"\n  [BLE] routed WATCH -> custom 0xEE11: {got_before} pkts, then 0 after DISCONNECT")
            finally:
                await cleanup_runtime(sess)
                await asyncio.sleep(0.4)
                await close_ble(client)

        run_async(go())

    def test_02_raw_and_framed_provider_bridges_and_frag_recovery(self):
        async def go():
            client, sess = await open_ble()
            try:
                await cleanup_runtime(sess)
                self.assertEqual((await sess.call(prov_ble_create(PROV_RAW_IN, 0xEE20, 0xEE21, 0, CMD_SEND_NOWAIT, "rin")))[0], 0)
                self.assertEqual((await sess.call(prov_ble_create(PROV_RAW_OUT, 0xEE20, 0xEE22, 0, CMD_SEND_NOWAIT, "rout")))[0], 0)
                self.assertEqual((await sess.call(prov_ble_create(PROV_FRM_IN, 0xEE20, 0xEE23, PROV_BLE_FRAMED, CMD_SEND_NOWAIT, "fin")))[0], 0)
                self.assertEqual((await sess.call(prov_ble_create(PROV_FRM_OUT, 0xEE20, 0xEE24, PROV_BLE_FRAMED, CMD_SEND_NOWAIT, "fout")))[0], 0)
                await asyncio.sleep(0.5)
            finally:
                await close_ble(client)

            client, sess = await open_ble()
            try:
                # Connect bridges in data_matrix: each provider is both ways, a bridge is one link (a side with no link is not wired)
                self.assertEqual(
                    (await sess.call(matrix_link(MATRIX_CONNECT, port_id(PORT_KIND_PROVIDER, PROV_RAW_IN), port_id(PORT_KIND_PROVIDER, PROV_RAW_OUT))))[0],
                    0,
                )
                self.assertEqual(
                    (await sess.call(matrix_link(MATRIX_CONNECT, port_id(PORT_KIND_PROVIDER, PROV_FRM_IN), port_id(PORT_KIND_PROVIDER, PROV_FRM_OUT))))[0],
                    0,
                )

                raw_q: asyncio.Queue[bytes] = asyncio.Queue()
                frm_dec = FragDecoder()
                await client.start_notify(uuid16(0xEE22), lambda _c, d: raw_q.put_nowait(bytes(d)))
                await client.start_notify(uuid16(0xEE24), lambda _c, d: frm_dec.feed(d))

                # 1. Raw bridge check
                for msg in (b"hello-ble", bytes(range(64)), b"\x00\xff\xaa\x55" * 20):
                    await client.write_gatt_char(uuid16(0xEE21), msg, response=True)
                    got = await asyncio.wait_for(raw_q.get(), timeout=2.0)
                    self.assertEqual(got, msg)

                # 2. Framed bridge check with forced multi-fragment splits (19 B chunks -> up to 26 fragments!)
                for length, step in ((12, 19), (120, 19), (480, 64), (CMD_FRAME_MAX, 128)):
                    payload = bytes((i * 37 + length) & 0xFF for i in range(length))
                    for pkt in frag_encode(payload, step):
                        await client.write_gatt_char(uuid16(0xEE23), pkt, response=True)
                    got = await asyncio.wait_for(frm_dec.frames.get(), timeout=3.0)
                    self.assertEqual(got, payload, f"framed bridge mismatch for len={length} step={step}")

                # 3. Bad fragment sequence (continuation without FIRST) -> dropped cleanly, next valid frame works
                await client.write_gatt_char(uuid16(0xEE23), bytes([0x05, 1, 2, 3, 4]), response=True)
                await asyncio.sleep(0.1)
                self.assertTrue(frm_dec.frames.empty(), "broken fragment should have been dropped")
                recovery = b"recovered-after-bad-frag"
                for pkt in frag_encode(recovery, 10):
                    await client.write_gatt_char(uuid16(0xEE23), pkt, response=True)
                got = await asyncio.wait_for(frm_dec.frames.get(), timeout=2.0)
                self.assertEqual(got, recovery)
                self.assertEqual(frm_dec.errors, 0)
                print("\n  [BLE] raw bridge, multi-fragment framed bridge (up to 512 B), and frag error recovery OK")
            finally:
                await cleanup_runtime(sess)
                await asyncio.sleep(0.4)
                await close_ble(client)

        run_async(go())


class BleRadioOffOnRecovery(unittest.TestCase):
    """Turn PC Bluetooth adapter OFF and back ON; verify board detects disconnect and recovers."""

    def test_pc_bluetooth_off_then_on_recovery(self):
        if os.environ.get("RUNIT_BLE_NO_TOGGLE") == "1":
            self.skipTest("RUNIT_BLE_NO_TOGGLE=1")
        if Radio is None:
            self.skipTest("winrt.windows.devices.radios not installed")

        async def go():
            console = OptionalConsole()
            client = None
            try:
                client, sess = await open_ble()
                # Start a periodic watch so active traffic is flowing when BT drops
                self.assertEqual((await sess.call(dev_cmd(DEV_TEST, WRITE, DEV_CREATE, testdev_cfg())))[0], 0)
                head = struct.pack("<BBHH", 1, TRIG_PERIODIC, 3, 0)
                self.assertEqual(
                    (await sess.call(bytes([KEY_WATCH, WRITE, WATCH_ADD]) + head + dev_cmd(DEV_TEST, READ, SUPPLY_SOURCE)))[0],
                    0,
                )
                await asyncio.sleep(0.25)
                self.assertGreater(len(sess.telem), 0)

                if console.link is not None:
                    st, b = console.call(bytes([KEY_BLE, READ, BLE_STATUS]))
                    self.assertEqual((st, parse_ble_status(b)[0]), (0, 1))
                    st, b = console.call(bytes([KEY_SESSION, READ, SESSION_PEERS]))
                    ble_peer = next(r for r in parse_peers(b) if r[0] == PROV_SYSTEM_BLE)
                    self.assertEqual(ble_peer[1], 1)

                # Turn PC Bluetooth radio OFF abruptly (simulates dongle unplug / out-of-range / radio kill)
                print("\n  [BLE] Turning PC Bluetooth radio OFF...")
                self.assertTrue(await set_pc_bluetooth(False), "failed to turn PC Bluetooth radio OFF")

                # Wait past the board's 2 s BLE supervision timeout
                await asyncio.sleep(3.5)

                if console.link is not None:
                    # Verify board detected disconnect, cleared BLE connected flag, and closed BLE session
                    disconnected = False
                    for _ in range(15):
                        st, b = console.call(bytes([KEY_BLE, READ, BLE_STATUS]))
                        if st == 0 and parse_ble_status(b)[0] == 0:
                            disconnected = True
                            break
                        await asyncio.sleep(0.25)
                    self.assertTrue(disconnected, "board did not detect BLE disconnect after PC BT OFF")

                    st, b = console.call(bytes([KEY_SESSION, READ, SESSION_PEERS]))
                    self.assertEqual(st, 0)
                    ble_peer = next(r for r in parse_peers(b) if r[0] == PROV_SYSTEM_BLE)
                    self.assertEqual(ble_peer[1], 0, "BLE session should be closed after disconnect")
                    print("  [BLE] Console confirmed board detected disconnect (connected=0, session open=0)")

                # Turn PC Bluetooth radio back ON
                print("  [BLE] Turning PC Bluetooth radio ON...")
                self.assertTrue(await set_pc_bluetooth(True), "failed to turn PC Bluetooth radio ON")
                await asyncio.sleep(2.5)  # allow Windows BT stack to settle

                # Reconnect from scratch and verify full board recovery
                client2, sess2 = await open_ble(retries=5)
                try:
                    st, b = await sess2.call(bytes([KEY_SYS, READ, SYS_PING]))
                    self.assertEqual((st, len(b)), (0, 4))
                    st, b = await sess2.call(bytes([KEY_BLE, READ, BLE_STATUS]))
                    self.assertEqual((st, parse_ble_status(b)[0]), (0, 1))

                    # Verify a fresh watch streams cleanly after recovery
                    sess2.telem.clear()
                    head = struct.pack("<BBHH", 2, TRIG_PERIODIC, 3, 0)
                    self.assertEqual(
                        (await sess2.call(bytes([KEY_WATCH, WRITE, WATCH_ADD]) + head + dev_cmd(DEV_TEST, READ, SUPPLY_SOURCE)))[0],
                        0,
                    )
                    await asyncio.sleep(0.35)
                    self.assertGreaterEqual(len(sess2.telem), 5, "no telemetry after BT OFF/ON recovery")
                    print(f"  [BLE] Recovered cleanly after PC BT OFF->ON ({len(sess2.telem)} telem pkts)")
                finally:
                    await cleanup_runtime(sess2)
                    await close_ble(client2)
            finally:
                # ALWAYS ensure PC Bluetooth radio is turned back ON
                with contextlib.suppress(Exception):
                    await set_pc_bluetooth(True)
                await close_ble(client)
                console.close()

        run_async(go())


class BleThroughputAndCompleteness(unittest.TestCase):
    """Throughput & packet completeness: framed bridge burst, RPC burst, and high-rate telemetry."""

    def test_01_framed_bridge_throughput_and_completeness(self):
        async def go():
            client, sess = await open_ble()
            try:
                await cleanup_runtime(sess)
                self.assertEqual((await sess.call(prov_ble_create(PROV_FRM_IN, 0xEE30, 0xEE31, PROV_BLE_FRAMED, CMD_SEND_NOWAIT, "tin")))[0], 0)
                self.assertEqual((await sess.call(prov_ble_create(PROV_FRM_OUT, 0xEE30, 0xEE32, PROV_BLE_FRAMED, CMD_SEND_NOWAIT, "tout")))[0], 0)
                await asyncio.sleep(0.5)
            finally:
                await close_ble(client)

            client, sess = await open_ble()
            try:
                self.assertEqual(
                    (await sess.call(matrix_link(MATRIX_CONNECT, port_id(PORT_KIND_PROVIDER, PROV_FRM_IN), port_id(PORT_KIND_PROVIDER, PROV_FRM_OUT))))[0],
                    0,
                )
                dec = FragDecoder()
                await client.start_notify(uuid16(0xEE32), lambda _c, d: dec.feed(d))

                # 80 frames x 480 bytes = 38,400 bytes round-trip (seq u32 + len u16 + crc32 u32 + pattern)
                frame_count = 80
                frame_size = 480
                body_len = frame_size - 10
                step = max(1, client.mtu_size - 4)
                expected: dict[int, bytes] = {}
                received: dict[int, bytes] = {}

                async def collector():
                    while len(received) < frame_count:
                        f = await asyncio.wait_for(dec.frames.get(), timeout=5.0)
                        seq, ln, crc = struct.unpack_from("<IHI", f, 0)
                        body = f[10:]
                        self.assertEqual(ln, len(body), f"length mismatch on seq {seq}")
                        self.assertEqual(crc, zlib.crc32(body) & 0xFFFFFFFF, f"CRC mismatch on seq {seq}")
                        self.assertNotIn(seq, received, f"duplicate frame seq {seq}")
                        received[seq] = f

                recv_task = asyncio.create_task(collector())
                t0 = time.perf_counter()
                for seq in range(frame_count):
                    body = bytes(((seq * 31 + i * 7) & 0xFF) for i in range(body_len))
                    crc = zlib.crc32(body) & 0xFFFFFFFF
                    frame = struct.pack("<IHI", seq, body_len, crc) + body
                    expected[seq] = frame
                    for pkt in frag_encode(frame, step):
                        await client.write_gatt_char(uuid16(0xEE31), pkt, response=True)
                await recv_task
                dt = time.perf_counter() - t0

                self.assertEqual(dec.errors, 0, f"fragmentation errors: {dec.errors}")
                self.assertEqual(set(received.keys()), set(expected.keys()))
                for seq, frame in expected.items():
                    self.assertEqual(received[seq], frame, f"payload mismatch on seq {seq}")

                total_bytes = frame_count * frame_size
                print(
                    f"\n  [BLE] Framed bridge round-trip: {frame_count}/{frame_count} frames "
                    f"({total_bytes} B each way, 100% intact, 0 frag errs) in {dt:.2f}s -> "
                    f"{total_bytes / dt / 1024:.1f} KB/s ({frame_count / dt:.1f} frames/s, MTU={client.mtu_size})"
                )
            finally:
                await cleanup_runtime(sess)
                await asyncio.sleep(0.4)
                await close_ble(client)

        run_async(go())

    def test_02_session_rpc_burst_and_telemetry_completeness(self):
        async def go():
            client, sess = await open_ble()
            try:
                await cleanup_runtime(sess)
                self.assertEqual((await sess.call(dev_cmd(DEV_TEST, WRITE, DEV_CREATE, testdev_cfg())))[0], 0)

                # 1. Back-to-back 60-byte I2C RAW write+read RPC burst (60 ops) -> 100% integrity
                rpc_count = 60
                chunk_len = 60
                t0 = time.perf_counter()
                for i in range(rpc_count):
                    pat = bytes(((i * 13 + j * 5) & 0xFF) for j in range(chunk_len))
                    st, _ = await sess.call(dev_cmd(DEV_TEST, WRITE, I2C_RAW, bytes([0x50, 0x00]) + pat))
                    self.assertEqual(st, 0, f"RPC write {i} failed")
                    st, rd = await sess.call(dev_cmd(DEV_TEST, READ, I2C_RAW, bytes([0x50, 0x00, chunk_len])))
                    self.assertEqual(st, 0, f"RPC read {i} failed")
                    self.assertEqual(rd, bytes([0x50, 0x00]) + pat, f"RPC data mismatch at iter {i}")
                dt = time.perf_counter() - t0
                total_rpcs = rpc_count * 2
                print(
                    f"\n  [BLE] Session RPC burst: {total_rpcs}/{total_rpcs} calls (60 B payloads, 100% intact) "
                    f"in {dt:.2f}s -> {total_rpcs / dt:.1f} RPC/s"
                )

                # 2. High-rate telemetry burst: 4 periodic watches at 20 ms for 2.0 s
                for ch in range(4):
                    self.assertEqual(
                        (await sess.call(dev_cmd(DEV_TEST, WRITE, OWN_FAKE_CHANNEL, struct.pack("<Bii", ch, 3300 + ch * 100, 250 + ch * 50))))[0],
                        0,
                    )
                sess.telem.clear()
                for ch in range(4):
                    wid = ch + 1
                    head = struct.pack("<BBHH", wid, TRIG_PERIODIC, 2, 0)  # 20 ms -> ~50 Hz each = ~200 pkt/s
                    self.assertEqual(
                        (await sess.call(bytes([KEY_WATCH, WRITE, WATCH_ADD]) + head + dev_cmd(DEV_TEST, READ, PMON_VOLTAGE, bytes([ch]))))[0],
                        0,
                    )
                t0 = time.perf_counter()
                await asyncio.sleep(2.0)
                dt_t = time.perf_counter() - t0
                pkts = list(sess.telem)
                for wid in range(1, 5):
                    await sess.call(bytes([KEY_WATCH, WRITE, WATCH_REMOVE, wid]))

                by_wid: dict[int, int] = {1: 0, 2: 0, 3: 0, 4: 0}
                for owner, wid, status, data in pkts:
                    if owner != CLIENT_BLE or wid not in by_wid:
                        continue
                    self.assertEqual(status, 0, f"watch {wid} returned error status {status}")
                    self.assertEqual(len(data), 4, f"watch {wid} truncated payload: {data.hex()}")
                    mv = struct.unpack("<i", data)[0]
                    self.assertEqual(mv, 3300 + (wid - 1) * 100, f"watch {wid} corrupted value {mv}")
                    by_wid[wid] += 1

                total_pkts = sum(by_wid.values())
                for wid, cnt in by_wid.items():
                    self.assertGreaterEqual(cnt, 40, f"watch {wid} starved: only {cnt} packets in 2s")
                print(
                    f"  [BLE] High-rate telemetry: {total_pkts} pkts in {dt_t:.2f}s "
                    f"({total_pkts / dt_t:.1f} pkt/s, per watch={by_wid}, 0 corrupted)"
                )
            finally:
                await cleanup_runtime(sess)
                await close_ble(client)

        run_async(go())


if __name__ == "__main__":
    unittest.main(verbosity=2)
