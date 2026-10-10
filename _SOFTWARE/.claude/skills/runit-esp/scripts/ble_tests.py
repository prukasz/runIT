"""BLE client tests for the runIT link (sys_ble_link.c): fragment header, commands,
streams, dead-man on disconnect.

  ..\..\..\..\.venv\Scripts\python ble_tests.py --uart COM10      (venv with bleak, see build.md §6)

runIT service 0xFFE0: RX 0xFFE2 (write, framed) -> router; TX 0xFFE1 (notify,
framed) <- responses + telemetry; LOGS 0xFFE3 (notify, framed) <- logs + errors.
Every packet on a framed characteristic starts with one header byte:
bit7 LAST, bit6 FIRST, bits 0-5 counter (mod 64, 0 at FIRST).

--uart watches the console log at the same time (needed for the dead-man check).
"""

from __future__ import annotations

import argparse
import asyncio
import struct
import sys
import time

from bleak import BleakClient, BleakScanner

from router_link import (
    FLAG_URGENT,
    KEY_CONN,
    KEY_SYS,
    STREAM_ERRORS,
    STREAM_LOGS,
    STREAM_RESPONSE,
    Link,
    Response,
    decode_record,
)


def uuid16(u: int) -> str:
    return f"0000{u:04x}-0000-1000-8000-00805f9b34fb"


CHR_TX, CHR_RX, CHR_LOGS, CHR_STATUS = uuid16(0xFFE1), uuid16(0xFFE2), uuid16(0xFFE3), uuid16(0xFFE4)
FRAG_LAST, FRAG_FIRST, FRAG_COUNT = 0x80, 0x40, 0x3F

results: list[tuple[str, bool]] = []


def check(name: str, ok: bool, detail: str = "") -> bool:
    results.append((name, ok))
    print(f"{'PASS' if ok else 'FAIL'}  {name}" + (f"\n      {detail}" if detail and not ok else ""))
    return ok


def fragments(frame: bytes, chunk: int) -> list[bytes]:
    out, count = [], 0
    for off in range(0, max(len(frame), 1), chunk):
        part = frame[off : off + chunk]
        hdr = (FRAG_FIRST if off == 0 else 0) | (FRAG_LAST if off + chunk >= len(frame) else 0) | (count & FRAG_COUNT)
        out.append(bytes([hdr]) + part)
        count += 1
    return out


class Reassembler:
    """Same rules as the firmware's RX side; counts packets per frame."""

    def __init__(self, name: str, on_frame):
        self.name, self.on_frame = name, on_frame
        self.buf, self.expected, self.active, self.packets = b"", 0, False, 0
        self.errors: list[str] = []
        self.max_packets = 0

    def feed(self, data: bytes) -> None:
        hdr = data[0]
        if hdr & FRAG_FIRST:
            self.buf, self.expected, self.active, self.packets = b"", 0, True, 0
        if not self.active or (hdr & FRAG_COUNT) != self.expected:
            self.errors.append(f"hdr {hdr:02x} expected {self.expected}")
            self.active = False
            return
        self.buf += data[1:]
        self.packets += 1
        self.expected = (self.expected + 1) & FRAG_COUNT
        if hdr & FRAG_LAST:
            self.active = False
            self.max_packets = max(self.max_packets, self.packets)
            self.on_frame(self.buf, self.packets)


class BleLink:
    def __init__(self, client: BleakClient):
        self.c = client
        self.seq = 0
        self.responses: dict[int, tuple[Response, int]] = {}
        self.streams: list[tuple[float, int, bytes, int]] = []  # t, stream, payload, packets
        self.tx = Reassembler("TX", self._tx_frame)
        self.logs = Reassembler("LOGS", self._logs_frame)

    def _tx_frame(self, f: bytes, packets: int) -> None:
        if f and f[0] == STREAM_RESPONSE and len(f) >= 3:
            self.responses[f[1]] = (Response(f[1], f[2], f[3:], time.time()), packets)
        elif f:
            self.streams.append((time.time(), f[0], f[1:], packets))

    def _logs_frame(self, f: bytes, packets: int) -> None:
        if f:
            self.streams.append((time.time(), f[0], f[1:], packets))

    async def start(self) -> None:
        await self.c.start_notify(CHR_TX, lambda _, d: self.tx.feed(bytes(d)))
        await self.c.start_notify(CHR_LOGS, lambda _, d: self.logs.feed(bytes(d)))

    @property
    def chunk_max(self) -> int:
        return self.c.mtu_size - 3 - 1  # ATT header, fragment header

    async def write_packets(self, packets: list[bytes]) -> None:
        for p in packets:
            await self.c.write_gatt_char(CHR_RX, p, response=True)

    async def request(self, body: bytes, urgent: bool = False, chunk: int | None = None, timeout: float = 3.0):
        self.seq = (self.seq + 1) & 0xFF
        seq = self.seq
        self.responses.pop(seq, None)
        frame = bytes([seq, FLAG_URGENT if urgent else 0]) + body
        await self.write_packets(fragments(frame, chunk or self.chunk_max))
        end = time.time() + timeout
        while time.time() < end:
            if seq in self.responses:
                return self.responses.pop(seq)
            await asyncio.sleep(0.01)
        return None, 0

    def streams_since(self, t: float, stream: int) -> list[tuple[bytes, int]]:
        return [(p, n) for (ts, s, p, n) in self.streams if ts >= t and s == stream]

    async def wait_stream(self, t: float, stream: int, pred, timeout: float = 3.0):
        end = time.time() + timeout
        while time.time() < end:
            for p, n in self.streams_since(t, stream):
                if pred(p):
                    return p, n
            await asyncio.sleep(0.02)
        return None, 0


def cause(r: Response | None) -> str:
    rec = r.record if r is not None else None
    return rec.cause.name if rec and rec.cause else ""


async def run(args) -> int:
    uart = Link(args.uart) if args.uart else None
    print(f"scanning for '{args.name}' ...")
    dev = await BleakScanner.find_device_by_name(args.name, timeout=15.0)
    if not check("found advertiser", dev is not None):
        return 1
    print(f"      {dev.address}")

    async with BleakClient(dev, timeout=20.0) as client:
        link = BleLink(client)
        await link.start()
        await asyncio.sleep(0.5)  # MTU exchange
        print(f"      MTU {client.mtu_size} -> {link.chunk_max} B per packet")
        check("connected, notifications on TX and LOGS", client.is_connected)

        # ---- commands, unfragmented ------------------------------------------------
        r, n = await link.request(bytes([KEY_SYS, 0x01]))
        check("PING over BLE", r is not None and r.ok and len(r.data) == 4, str(r))
        r, n = await link.request(bytes([KEY_SYS, 0x01]), urgent=True)
        check("PING urgent over BLE", r is not None and r.ok, str(r))
        r, n = await link.request(bytes([0x03, 0x06]))
        if check("BLE STATUS reports the connection", r is not None and r.ok and r.data[0] == 1, str(r)):
            conn, mtu, ovf = struct.unpack("<BHI", r.data)
            print(f"      device side: connected {conn} mtu {mtu} rx_overflow {ovf}")

        # ---- RX fragmentation (client -> device) ---------------------------------
        r, _ = await link.request(bytes([KEY_SYS, 0x01]), chunk=1)
        check("PING split into 1-byte packets (4 packets)", r is not None and r.ok, str(r))
        name = b"ble-frag-test\0"
        body = bytes([KEY_CONN, 0x01, 6, 0x02]) + name
        r, _ = await link.request(body, chunk=3)
        check("conn CREATE split into 3-byte packets (7 packets)", r is not None and r.ok, str(r))
        r, _ = await link.request(bytes([KEY_CONN, 0x02, 6]), chunk=2)
        check("conn REMOVE split into 2-byte packets", r is not None and r.ok, str(r))
        big = bytes([KEY_SYS, 0x01]) + bytes(range(256)) + bytes(100)  # 358 B + seq/flags: > one packet
        r, _ = await link.request(big)
        rec = r.record if r else None
        ok = cause(r) == "ERR_CMD_BAD_LEN" and rec is not None and rec.request_cut
        got = struct.unpack_from("<H", rec.cause.data)[0] if ok else None
        check(f"360-byte frame over {-(-360 // link.chunk_max)} packets -> reassembled (BAD_LEN got {got} = 356 args)", ok and got == 356, str(r))
        # 65+ packets: the counter wraps mod 64
        r, _ = await link.request(bytes([KEY_SYS, 0x01]) + bytes(68), chunk=1)
        rec = r.record if r else None
        got = struct.unpack_from("<H", rec.cause.data)[0] if rec and rec.cause else None
        check("72-byte frame in 72 packets (counter wraps 63 -> 0)", cause(r) == "ERR_CMD_BAD_LEN" and got == 68, str(r))

        # ---- fragment errors ---------------------------------------------------------
        t = time.time()
        await link.write_packets([bytes([FRAG_LAST | 5, 0x99, 0x99])])  # no FIRST
        p, _ = await link.wait_stream(t, STREAM_ERRORS, lambda p: "ERR_BLE_FRAG" in decode_record(p).names())
        check("packet without FIRST -> ERR_BLE_FRAG on errors stream (LOGS)", p is not None, str(p))
        t = time.time()
        await link.write_packets([bytes([FRAG_FIRST | 0, 0xAA, 0x00]), bytes([FRAG_LAST | 2, 0x01, 0x01])])  # skips 1
        p, _ = await link.wait_stream(t, STREAM_ERRORS, lambda p: "ERR_BLE_FRAG" in decode_record(p).names())
        check("counter gap -> ERR_BLE_FRAG, frame dropped", p is not None, str(p))
        r, _ = await link.request(bytes([KEY_SYS, 0x01]))
        check("next frame is fine after a dropped one", r is not None and r.ok, str(r))

        # ---- errors and logs streams on LOGS -----------------------------------------
        t = time.time()
        r, _ = await link.request(bytes([0x09, 0x01]))
        check("unknown key -> error response on TX", cause(r) == "ERR_ROUTER_UNKNOWN_KEY", str(r))
        p, _ = await link.wait_stream(
            t, STREAM_ERRORS, lambda p: (rc := decode_record(p)).src == 1 and rc.link == 2 and rc.seq == link.seq
        )
        check("same error record on LOGS (errors stream, link 2 = BLE)", p is not None)
        p, _ = await link.wait_stream(t, STREAM_LOGS, lambda p: b"ERR_ROUTER_UNKNOWN_KEY" in p)
        check("error log line on LOGS (logs stream)", p is not None)
        if p:
            print(f"      {p.decode(errors='replace').rstrip()}")

        # ---- TX fragmentation (device -> client): connector INFO grows per connector ----
        for i in range(3, 8):
            await link.request(bytes([KEY_CONN, 0x01, i, 0x02]) + f"long-name-{i:02d}-xx".encode() + b"\0")
            await link.request(bytes([KEY_CONN, 0x03, i, 1]) + struct.pack("<I", 0))
            await link.request(bytes([KEY_CONN, 0x03, i, 2]) + struct.pack("<I", 0xFFE1))
        r, n = await link.request(bytes([KEY_CONN, 0x07]))
        size = len(r.data) + 3 if r else 0
        need = -(-size // link.chunk_max)
        ok = r is not None and r.ok and r.data[0] == 8
        check(f"conn INFO {size} B response, {n} packet(s) (needs {need} at this MTU)", ok and n == need, str(r))
        if need == 1:
            print("      MTU fits it in one packet: device-side split not exercised by this response")
        for i in range(3, 8):
            await link.request(bytes([KEY_CONN, 0x02, i]))

        # ---- dead-man on link lost -------------------------------------------------
        r, _ = await link.request(bytes([KEY_SYS, 0x04]) + struct.pack("<IB", 60000, 1))
        check("DEADMAN_SET 60 s, on_link_lost", r is not None and r.ok, str(r))
        mark = uart.mark() if uart else 0.0
        check("no reassembly errors on the client", not link.tx.errors and not link.logs.errors, str(link.tx.errors + link.logs.errors))

    if uart:
        ok = uart.wait_log("safe state (reason 3)", 5.0, since=mark)
        check("disconnect -> safe state LINK_LOST (UART log)", ok)
        r = uart.request(bytes([KEY_SYS, 0x04]), payload=struct.pack("<IB", 0, 0))
        check("DEADMAN off again (UART)", r is not None and r.ok, str(r))
        uart.close()

    passed = sum(ok for _, ok in results)
    print(f"\n{passed}/{len(results)} passed")
    return 0 if passed == len(results) else 1


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--name", default="runit")
    ap.add_argument("--uart", help="console port to watch (dead-man check), e.g. COM10")
    sys.exit(asyncio.run(run(ap.parse_args())))


if __name__ == "__main__":
    main()
