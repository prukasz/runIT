"""Host side of the console UART command link (sys_uart_provider).

Frames travel as "#R:<hex>" lines next to the text log. This module sends
commands packed from the published descriptors (data-structures/*.generated.json),
matches responses by seq, and decodes the error / telemetry streams.

Library use:
    from runit_link import Link
    with Link("COM3") as link:
        r = link.call("packet_sys_io_get_level_t", device_id=0, pin=0)
        print(r)

CLI:
    python runit_link.py COM3|BLE [--reset] [--listen SECONDS] [--raw HEX ...] [--call PACKET key=value ...]
"""
from __future__ import annotations

import json
import queue
import re
import struct
import sys
import threading
import time
from dataclasses import dataclass, field
from pathlib import Path

import serial

ROOT = Path(__file__).resolve().parents[4]  # _SOFTWARE
DS = ROOT / "data-structures"
PREFIX = "#R:"

STREAM_TELEMETRY = 0x02
STREAM_LOGS = 0x03
STREAM_ERRORS = 0x04
STREAM_INTERFACE = 0x05

_TYPES = {
    "uint8_t": "B", "int8_t": "b", "uint16_t": "H", "int16_t": "h",
    "uint32_t": "I", "int32_t": "i", "float": "f", "char": "B", "bool": "B",
}


# ---------------------------------------------------------------------------
# Descriptors
# ---------------------------------------------------------------------------

def _load_packets() -> dict[str, dict]:
    """Every command packet by id, with its class byte attached."""
    out: dict[str, dict] = {}
    contracts = json.loads((DS / "contracts" / "contracts.generated.json").read_text(encoding="utf-8"))
    for cat in contracts["catalogs"]:
        for p in cat["contracts"]:
            out[p["id"]] = {**p, "class": int(cat["class_header"], 16)}
    settings = json.loads((DS / "settings" / "settings.generated.json").read_text(encoding="utf-8"))
    for cat in settings["settings"]:
        for p in cat["packets"]:
            out[p["id"]] = {**p, "class": int(cat["class_header"], 16)}
    return out


def _load_error_names() -> tuple[dict[int, str], dict[int, str]]:
    """Tag and owner names parsed from the error map headers (X(NAME, 0xNNNN, ...))."""
    tags: dict[int, str] = {}
    owners: dict[int, str] = {}
    rx = re.compile(r"X\(\s*((?:ERR|OWNER)_\w+)\s*,\s*(0x[0-9A-Fa-f]+)")
    for h in list((ROOT / "components").rglob("errors/*.h")) + list((ROOT / "components" / "sys_errors" / "codes").rglob("*.h")):
        for name, val in rx.findall(h.read_text(encoding="utf-8", errors="replace")):
            (tags if name.startswith("ERR_") else owners)[int(val, 16)] = name
    return tags, owners


PACKETS = _load_packets()
TAGS, OWNERS = _load_error_names()


def tag_name(v: int) -> str:
    return TAGS.get(v, f"0x{v:04X}")


def owner_name(v: int) -> str:
    return OWNERS.get(v, f"0x{v:04X}")


def pack_fields(desc: dict, values: dict) -> bytes:
    """Pack a descriptor's fields in field_order (little-endian, packed).
    Every field is always sent: "required": false means the value may be 0 /
    the sentinel, not that it can be left off the wire (decoders need the whole struct)."""
    order = desc["field_order"]
    unknown = set(values) - set(order)
    if unknown:
        raise KeyError(f"{desc['id']}: unknown fields {sorted(unknown)}")
    out = bytearray()
    for name in order:
        f = desc["fields"][name]
        if f.get("flexible_array") and f["type"] == "char":
            s = values.get(name, "")
            out += s.encode(f.get("encoding", "utf-8")) + b"\0"
            continue
        if name not in values and f.get("required", True):
            raise KeyError(f"{desc['id']}: required field '{name}' missing")
        fmt = _TYPES[f["type"]]
        v = values.get(name, f.get("sentinel", 0))
        if "array_len" in f:
            v = list(v) + [0] * (f["array_len"] - len(v))
            out += struct.pack("<" + fmt * f["array_len"], *v)
        else:
            out += struct.pack("<" + fmt, v)
    return bytes(out)


def unpack_fields(layout: dict, data: bytes) -> dict:
    out: dict = {}
    off = 0
    for name in layout["field_order"]:
        f = layout["fields"][name]
        fmt = _TYPES[f["type"]]
        n = f.get("array_len", 1)
        size = struct.calcsize("<" + fmt * n)
        if off + size > len(data):
            out[name] = None
            continue
        vals = struct.unpack_from("<" + fmt * n, data, off)
        out[name] = list(vals) if "array_len" in f else vals[0]
        off += size
    if off < len(data):
        out["_extra"] = data[off:].hex()
    return out


# ---------------------------------------------------------------------------
# Stream decoders
# ---------------------------------------------------------------------------

@dataclass
class ErrorNode:
    tag: int
    owner: int
    payload: bytes

    def __str__(self) -> str:
        p = f" payload={self.payload.hex()}" if self.payload else ""
        return f"{tag_name(self.tag)} @ {owner_name(self.owner)}{p}"


@dataclass
class ErrorPacket:
    node_count: int
    depth: int
    schema_id: int
    nodes: list[ErrorNode]

    def __str__(self) -> str:
        return " <- ".join(str(n) for n in self.nodes) + (f" (depth {self.depth}/{self.node_count})" if self.depth != self.node_count else "")


def decode_error_packet(body: bytes) -> ErrorPacket:
    node_count, depth, schema_id = struct.unpack_from("<BBI", body, 0)
    off = 6
    nodes = []
    while off + 5 <= len(body):
        ln, tag, owner = struct.unpack_from("<BHH", body, off)
        off += 5
        nodes.append(ErrorNode(tag, owner, body[off:off + ln]))
        off += ln
    return ErrorPacket(node_count, depth, schema_id, nodes)


@dataclass
class Response:
    seq: int
    cls: int
    packet: int
    status: int
    data: bytes
    fields: dict | None = None
    t: float = 0.0

    @property
    def ok(self) -> bool:
        return self.status == 0

    @property
    def err(self) -> tuple[str, str] | None:
        if self.ok or len(self.data) < 4:
            return None
        tag, owner = struct.unpack_from("<HH", self.data, 0)
        return tag_name(tag), owner_name(owner)

    def __str__(self) -> str:
        head = f"seq={self.seq} {self.cls:02X}/{self.packet:02X}"
        if self.ok:
            body = self.fields if self.fields is not None else (self.data.hex() or "-")
            return f"{head} OK {body}"
        return f"{head} ERROR {self.err or self.data.hex()}"


# ---------------------------------------------------------------------------
# Link
# ---------------------------------------------------------------------------

@dataclass
class Frame:
    t: float
    stream: int
    body: bytes


class Link:
    def __init__(self, port: str, baud: int = 115200, reset: bool = False, echo_logs: bool = False):
        self.echo_logs = echo_logs
        self.s = serial.Serial()
        self.s.port, self.s.baudrate, self.s.timeout = port, baud, 0.05
        self.s.dtr = False
        self.s.rts = False
        self.s.open()
        self.logs: list[tuple[float, str]] = []
        self.errors: list[tuple[float, ErrorPacket]] = []
        self.telemetry: list[Frame] = []
        self.frames: list[Frame] = []
        self._responses: "queue.Queue[Response]" = queue.Queue()
        self._pending: dict[int, dict] = {}
        self._seq = 0
        self._lock = threading.Lock()
        self._port_lock = threading.Lock()
        self._stop = False
        self._t0 = time.time()
        self._rx = threading.Thread(target=self._reader, daemon=True)
        self._rx.start()
        if reset:
            self.reset()

    # -- lifecycle ---------------------------------------------------------
    def reset(self, boot_wait: float = 2.0) -> None:
        """Pulse EN through RTS (normal boot) and wait for the boot log to finish.
        On the native USB (USB-Serial-JTAG) the port re-enumerates: reopen it."""
        with self._port_lock:
            # usbser.sys applies an RTS change only together with DTR (esptool does the same)
            self.s.dtr = False
            self.s.rts = True
            self.s.dtr = self.s.dtr
            time.sleep(0.1)
            self.s.rts = False
            self.s.dtr = self.s.dtr
            time.sleep(0.1)
            self._reopen()
        self.wait_log("runIT boot sequence complete", boot_wait + 3.0)

    def _reopen(self, timeout: float = 5.0) -> None:
        """Close and reopen the port (caller holds _port_lock); waits for a re-enumerating USB port."""
        try:
            self.s.close()
        except serial.SerialException:
            pass
        deadline = time.time() + timeout
        while True:
            try:
                self.s.open()
                return
            except serial.SerialException:
                if time.time() > deadline:
                    raise
                time.sleep(0.05)

    def close(self) -> None:
        self._stop = True
        self._rx.join(timeout=1)
        self.s.close()

    def __enter__(self) -> "Link":
        return self

    def __exit__(self, *exc) -> None:
        self.close()

    # -- reader ------------------------------------------------------------
    def _reader(self) -> None:
        buf = b""
        while not self._stop:
            try:
                with self._port_lock:
                    chunk = self.s.read(4096) if self.s.is_open else b""
            except serial.SerialException:
                time.sleep(0.05)  # port re-enumerating after a reset
                with self._port_lock:
                    try:
                        self._reopen(timeout=0.3)
                    except serial.SerialException:
                        pass
                continue
            if not chunk:
                continue
            buf += chunk
            *lines, buf = buf.split(b"\n")
            for raw in lines:
                self._line(raw.decode("utf-8", errors="replace").rstrip("\r"))

    def _line(self, line: str) -> None:
        now = time.time() - self._t0
        i = line.find(PREFIX)
        if i < 0:
            with self._lock:
                self.logs.append((now, line))
            if self.echo_logs:
                print(f"  LOG {line}")
            return
        if i > 0:  # log text cut by a frame line
            with self._lock:
                self.logs.append((now, line[:i]))
        try:
            data = bytes.fromhex(line[i + len(PREFIX):].strip())
        except ValueError:
            with self._lock:
                self.logs.append((now, "<bad frame line> " + line))
            return
        if data:
            self._frame(now, data)

    def _frame(self, now: float, data: bytes) -> None:
        """One frame, stream byte first (both transports)."""
        if data[0] == STREAM_LOGS:  # text log (BLE; on UART the log is plain text)
            text = data[1:].decode("utf-8", errors="replace").rstrip("\r\n")
            with self._lock:
                self.logs.extend((now, t) for t in text.split("\n"))
            if self.echo_logs:
                print(f"  LOG {text}")
            return
        fr = Frame(now, data[0], data[1:])
        with self._lock:
            self.frames.append(fr)
        if fr.stream == STREAM_INTERFACE and len(fr.body) >= 4:
            seq, cls, pkt, status = fr.body[:4]
            r = Response(seq, cls, pkt, status, fr.body[4:], t=now)
            self._responses.put(r)
        elif fr.stream == STREAM_ERRORS:
            try:
                ep = decode_error_packet(fr.body)
            except struct.error:
                ep = ErrorPacket(0, 0, 0, [])
            with self._lock:
                self.errors.append((now, ep))
            if self.echo_logs:
                print(f"  ERR {ep}")
        elif fr.stream == STREAM_TELEMETRY:
            with self._lock:
                self.telemetry.append(fr)

    # -- commands ----------------------------------------------------------
    def next_seq(self) -> int:
        self._seq = (self._seq + 1) & 0xFF
        return self._seq

    def send_line(self, text: str) -> None:
        with self._port_lock:
            try:
                self.s.write(text.encode("ascii") + b"\n")
                return
            except serial.SerialException:
                # Native USB drops when the chip resets (brownout, crash): reopen once.
                self.logs.append((time.time() - self._t0, "<link: port lost, reopening>"))
                self._reopen()
            self.s.write(text.encode("ascii") + b"\n")

    def send_raw(self, frame: bytes) -> None:
        """Send one frame as-is (seq included by the caller)."""
        self.send_line(PREFIX + frame.hex().upper())

    def request(self, cls: int, packet: int | None, payload: bytes = b"", timeout: float = 2.0, seq: int | None = None) -> Response | None:
        seq = self.next_seq() if seq is None else seq
        frame = bytes([seq, cls]) + (bytes([packet]) if packet is not None else b"") + payload
        self.send_raw(frame)
        return self.wait_response(seq, timeout)

    def wait_response(self, seq: int, timeout: float = 2.0) -> Response | None:
        end = time.time() + timeout
        while time.time() < end:
            try:
                r = self._responses.get(timeout=max(0.0, end - time.time()))
            except queue.Empty:
                return None
            if r.seq == seq:
                return r
        return None

    def call(self, packet_id: str, timeout: float = 2.0, **values) -> Response | None:
        desc = PACKETS[packet_id]
        r = self.request(desc["class"], int(desc["packet_header"], 16), pack_fields(desc, values), timeout)
        if r is not None and r.ok and "response" in desc and r.data:
            r.fields = unpack_fields(desc["response"], r.data)
        return r

    # -- helpers -----------------------------------------------------------
    def mark(self) -> int:
        """Timestamp (ms since open) for the *_since() readers."""
        return int((time.time() - self._t0) * 1000)

    def logs_since(self, t_ms: int) -> list[str]:
        with self._lock:
            return [l for t, l in self.logs if t * 1000 >= t_ms]

    def errors_since(self, t_ms: int) -> list[ErrorPacket]:
        with self._lock:
            return [e for t, e in self.errors if t * 1000 >= t_ms]

    def telemetry_since(self, t_ms: int) -> list[Frame]:
        with self._lock:
            return [f for f in self.telemetry if f.t * 1000 >= t_ms]

    def wait_log(self, needle: str, timeout: float) -> bool:
        end = time.time() + timeout
        start = 0
        while time.time() < end:
            with self._lock:
                lines = self.logs[start:]
                start = len(self.logs)
            if any(needle in l for _, l in lines):
                return True
            time.sleep(0.05)
        return False

    def listen(self, seconds: float) -> None:
        time.sleep(seconds)


# ---------------------------------------------------------------------------
# BLE transport (bleak): same API as Link, frames on the runIT GATT service
# ---------------------------------------------------------------------------

BLE_NAME = "runit"
_U = "0000{:04x}-0000-1000-8000-00805f9b34fb".format
BLE_TX, BLE_RX, BLE_LOGS = _U(0xFFE1), _U(0xFFE2), _U(0xFFE3)


class BleLink(Link):
    """Commands over BLE: writes to 0xFFE2, responses / telemetry on 0xFFE1,
    text logs and error packets on 0xFFE3. Needs a Python with bleak."""

    def __init__(self, name: str = BLE_NAME, echo_logs: bool = False, connect_timeout: float = 15.0):
        import asyncio
        self.echo_logs = echo_logs
        self.logs, self.errors, self.telemetry, self.frames = [], [], [], []
        self._responses = queue.Queue()
        self._seq = 0
        self._lock = threading.Lock()
        self._t0 = time.time()
        self._asyncio = asyncio
        self._loop = asyncio.new_event_loop()
        self._client = None
        self.mtu = 0
        threading.Thread(target=self._loop.run_forever, daemon=True).start()
        self._run(self._connect(name, connect_timeout), connect_timeout + 10)

    def _run(self, coro, timeout: float = 10.0):
        return self._asyncio.run_coroutine_threadsafe(coro, self._loop).result(timeout)

    async def _connect(self, name: str, timeout: float) -> None:
        from bleak import BleakClient, BleakScanner
        dev = await BleakScanner.find_device_by_name(name, timeout=timeout)
        if dev is None:
            raise RuntimeError(f"BLE device '{name}' not found")
        self._client = BleakClient(dev)
        await self._client.connect()
        await self._client.start_notify(BLE_TX, self._on_notify)
        await self._client.start_notify(BLE_LOGS, self._on_notify)
        self.mtu = self._client.mtu_size

    def _on_notify(self, _char, data: bytearray) -> None:
        if data:
            self._frame(time.time() - self._t0, bytes(data))

    def send_raw(self, frame: bytes) -> None:
        self._run(self._client.write_gatt_char(BLE_RX, frame, response=True))

    def send_line(self, text: str) -> None:
        raise NotImplementedError("BLE carries binary frames, not #R: lines")

    def reset(self, boot_wait: float = 2.0) -> None:
        raise NotImplementedError("no reset line over BLE")

    def close(self) -> None:
        try:
            if self._client is not None:
                self._run(self._client.disconnect())
        finally:
            self._loop.call_soon_threadsafe(self._loop.stop)


def open_link(port: str, **kw) -> Link:
    """'BLE' (or 'BLE:<name>') opens BleLink, anything else is a serial port."""
    if port.upper().startswith("BLE"):
        name = port.split(":", 1)[1] if ":" in port else BLE_NAME
        return BleLink(name, echo_logs=kw.get("echo_logs", False))
    return Link(port, **kw)


# ---------------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------------

def _parse_value(v: str):
    if v.startswith("[") and v.endswith("]"):
        return [int(x, 0) for x in v[1:-1].split(",") if x]
    try:
        return int(v, 0)
    except ValueError:
        try:
            return float(v)
        except ValueError:
            return v


def main(argv: list[str]) -> int:
    if len(argv) < 2:
        print(__doc__)
        return 2
    port = argv[1]
    args = argv[2:]
    with open_link(port, reset="--reset" in args, echo_logs=True) as link:
        i = 0
        while i < len(args):
            a = args[i]
            if a == "--listen":
                link.listen(float(args[i + 1]))
                i += 2
            elif a == "--raw":
                frame = bytes.fromhex(args[i + 1])
                link.send_raw(frame)
                r = link.wait_response(frame[0]) if frame else None
                print(f"RAW {frame.hex()} -> {r}")
                i += 2
            elif a == "--call":
                pid = args[i + 1]
                i += 2
                values = {}
                while i < len(args) and not args[i].startswith("--"):
                    k, v = args[i].split("=", 1)
                    values[k] = _parse_value(v)
                    i += 1
                print(f"CALL {pid} {values} -> {link.call(pid, **values)}")
            else:
                i += 1
        time.sleep(0.3)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
