"""Host side of the runIT router protocol over the console UART ("#R:<hex>" lines).

Works with a real board (COM port) and with QEMU (socket://localhost:5555).
Requests:  [seq][flags][keys...][payload]       (ROUTER.MD)
Responses: [0x05][seq][status][data]            status 1: data = error record
Streams:   [stream][payload]  0x02 telemetry, 0x03 logs, 0x04 errors (error record)

Error tag names are read from the firmware headers (X-macro maps), so they
never go stale.
"""

from __future__ import annotations

import re
import struct
import threading
import time
from dataclasses import dataclass, field
from pathlib import Path

import serial  # pyserial (in the ESP-IDF python env)

ROOT = Path(__file__).resolve().parents[4]  # _SOFTWARE
PREFIX = "#R:"
STREAM_RESPONSE = 0x05
STREAM_TELEMETRY = 0x02
STREAM_LOGS = 0x03
STREAM_ERRORS = 0x04
FLAG_URGENT = 0x01
ANSI = re.compile(r"\x1b\[[0-9;]*[A-Za-z]")

# Root keys (router_ids.h) and transports (transport_e in sys_connector.h)
KEY_SYS, KEY_I2C, KEY_BLE, KEY_CONN, KEY_DEVICE, KEY_ACTIONS = 0x01, 0x02, 0x03, 0x04, 0x05, 0x06
TRANSPORT_UART, TRANSPORT_BLE = 1, 2
SEV_NAMES = ["NONE", "LOW", "MEDIUM", "HIGH", "CRITICAL"]


def load_tags() -> dict[int, str]:
    """tag value -> name, from err_modules.h and every ERR_*_MAP in components/."""
    mods = {}
    for m in re.finditer(r"#define\s+(ERR_MOD_\w+)\s+(\d+)u", (ROOT / "components/core/errors/err_modules.h").read_text()):
        mods[m.group(1)] = int(m.group(2))
    tags = {}
    pat = re.compile(r"X\(\s*(ERR_\w+)\s*,\s*ERR_TAG\(\s*(ERR_MOD_\w+)\s*,\s*(\d+)\s*\)")
    for h in (ROOT / "components").rglob("*.h"):
        for m in pat.finditer(h.read_text(errors="ignore")):
            if m.group(2) in mods:
                tags[(mods[m.group(2)] << 16) | int(m.group(3))] = m.group(1)
    return tags


TAGS = load_tags()


@dataclass
class Node:
    tag: int
    sev: int
    data: bytes

    @property
    def name(self) -> str:
        return TAGS.get(self.tag, f"0x{self.tag:08x}")

    def __str__(self) -> str:
        sev = SEV_NAMES[self.sev] if self.sev < len(SEV_NAMES) else str(self.sev)
        return f"{self.name} {sev} {self.data.hex(' ')}".rstrip()


@dataclass
class Record:
    """router_err_record(): request (if any) + error chain."""

    src: int
    time_ms: int = 0  # the board's clock when it reported (UTC; before 2020: since the start)
    link: int = 0
    peer: int = 0
    seq: int = 0
    flags: int = 0
    request: bytes = b""
    request_cut: bool = False
    nodes: list[Node] = field(default_factory=list)
    chain_cut: bool = False

    @property
    def cause(self) -> Node | None:
        return self.nodes[-1] if self.nodes else None

    def names(self) -> list[str]:
        return [n.name for n in self.nodes]

    def __str__(self) -> str:
        head = (
            f"cmd link {self.link} seq {self.seq} flags {self.flags:02x} req {self.request.hex(' ')}"
            + (" ..." if self.request_cut else "")
            if self.src == 1
            else "internal"
        )
        return head + "".join(f"\n    [{i}] {n}" for i, n in enumerate(self.nodes)) + (" (cut)" if self.chain_cut else "")


def decode_chain(b: bytes) -> tuple[list[Node], bool]:
    if not b:
        return [], False
    count, cut = b[0] & 0x7F, bool(b[0] & 0x80)
    nodes, pos = [], 1
    for _ in range(count):
        tag, sev, ln = struct.unpack_from("<IBB", b, pos)
        pos += 6
        nodes.append(Node(tag, sev, b[pos : pos + ln]))
        pos += ln
    return nodes, cut


def decode_record(b: bytes) -> Record:
    src = b[0]
    (time_ms,) = struct.unpack_from("<Q", b, 1)
    if src == 0:
        nodes, cut = decode_chain(b[9:])
        return Record(src=0, time_ms=time_ms, nodes=nodes, chain_cut=cut)
    link, peer, seq, flags, rl = struct.unpack_from("<BIBBB", b, 9)
    n = rl & 0x7F
    req = b[17 : 17 + n]
    nodes, cut = decode_chain(b[17 + n :])
    return Record(1, time_ms, link, peer, seq, flags, req, bool(rl & 0x80), nodes, cut)


@dataclass
class Response:
    seq: int
    status: int
    data: bytes
    t: float

    @property
    def ok(self) -> bool:
        return self.status == 0

    @property
    def record(self) -> Record | None:
        return decode_record(self.data) if self.status == 1 else None

    def __str__(self) -> str:
        return f"seq {self.seq} OK {self.data.hex(' ')}" if self.ok else f"seq {self.seq} ERROR {self.record}"


class Link:
    def __init__(self, url: str, baud: int = 115200, echo: bool = False):
        # Both reset lines low before the port opens, so opening does not pulse EN / IO0 through the auto-reset circuit
        # (the board keeps running, with its outputs); close() releases them in the matching order.
        self.ser = serial.serial_for_url(url, baudrate=baud, timeout=0.05, do_not_open=True)
        try:
            self.ser.rts = False
            self.ser.dtr = False
        except (AttributeError, ValueError, OSError):
            pass  # a socket:// target (QEMU) has no modem lines
        self.ser.open()
        self.echo = echo
        self.logs: list[tuple[float, str]] = []
        self.streams: list[tuple[float, int, bytes]] = []
        self.responses: dict[int, Response] = {}
        self._parts: dict[int, list[Response]] = {}  # request_many: every answer of a seq, in order
        self._seq = 0
        self._lock = threading.Lock()
        self._stop = False
        self._thread = threading.Thread(target=self._reader, daemon=True)
        self._thread.start()

    def close(self) -> None:
        self._stop = True
        self._thread.join(1)
        try:
            self.ser.rts = False  # RTS first: (DTR, RTS) = (1, 0) only pulls IO0; DTR first would pull EN (a reset)
            self.ser.dtr = False
        except (AttributeError, ValueError, OSError):
            pass
        self.ser.close()

    def __enter__(self) -> "Link":
        return self

    def __exit__(self, *exc) -> None:
        self.close()

    # ---- reader -------------------------------------------------------------

    def _reader(self) -> None:
        buf = b""
        while not self._stop:
            try:
                chunk = self.ser.read(256)
            except Exception:
                time.sleep(0.05)
                continue
            if not chunk:
                continue
            buf += chunk
            while b"\n" in buf:
                raw, buf = buf.split(b"\n", 1)
                self._line(raw.decode(errors="replace").strip("\r"))

    def _line(self, line: str) -> None:
        now = time.time()
        clean = ANSI.sub("", line)
        # A frame can follow a cut-off log line on the same line: opening the port resets a USB-Serial-JTAG board,
        # the boot log is dropped mid-line while nobody reads it, and the answer to the first command lands right
        # after the fragment ("...size#R:0501..."). The text before the prefix is log, the rest is the frame.
        at = clean.rfind(PREFIX)
        if at >= 0:
            if at > 0:
                with self._lock:
                    self.logs.append((now, clean[:at]))
            try:
                frame = bytes.fromhex(clean[at + len(PREFIX) :])
            except ValueError:
                return
            with self._lock:
                if frame and frame[0] == STREAM_RESPONSE and len(frame) >= 3:
                    resp = Response(frame[1], frame[2], frame[3:], now)
                    self.responses[frame[1]] = resp
                    if frame[1] in self._parts:
                        self._parts[frame[1]].append(resp)
                elif frame:
                    self.streams.append((now, frame[0], frame[1:]))
            return
        with self._lock:
            self.logs.append((now, clean))
        if self.echo:
            print("  |", clean)

    # ---- requests -----------------------------------------------------------

    def next_seq(self) -> int:
        self._seq = (self._seq + 1) & 0xFF
        return self._seq

    def send_frame(self, frame: bytes) -> None:
        self.ser.write((PREFIX + frame.hex().upper() + "\n").encode())

    def request(self, keys: bytes, payload: bytes = b"", urgent: bool = False, timeout: float = 2.0) -> Response | None:
        seq = self.next_seq()
        with self._lock:
            self.responses.pop(seq, None)
        self.send_frame(bytes([seq, FLAG_URGENT if urgent else 0]) + keys + payload)
        return self.wait_response(seq, timeout)

    def request_many(self, keys: bytes, payload: bytes, count: int, timeout: float = 2.0) -> list[Response]:
        """A command answered with `count` packets (actions READ): all of them in order, or what came (an error
        answer ends it early)."""
        seq = self.next_seq()
        with self._lock:
            self._parts[seq] = []
        self.send_frame(bytes([seq, 0]) + keys + payload)
        end = time.time() + timeout
        while time.time() < end:
            with self._lock:
                got = self._parts[seq]
                if len(got) >= count or any(not r.ok for r in got):
                    break
            time.sleep(0.01)
        with self._lock:
            return self._parts.pop(seq)

    def wait_response(self, seq: int, timeout: float = 2.0) -> Response | None:
        end = time.time() + timeout
        while time.time() < end:
            with self._lock:
                if seq in self.responses:
                    return self.responses.pop(seq)
            time.sleep(0.01)
        return None

    # ---- logs / streams -----------------------------------------------------

    def mark(self) -> float:
        return time.time()

    def logs_since(self, t: float) -> list[str]:
        with self._lock:
            return [l for (ts, l) in self.logs if ts >= t]

    def wait_log(self, needle: str, timeout: float, since: float = 0.0) -> bool:
        end = time.time() + timeout
        while time.time() < end:
            if any(needle in l for l in self.logs_since(since)):
                return True
            time.sleep(0.05)
        return False

    def stream_since(self, stream: int, t: float) -> list[bytes]:
        with self._lock:
            return [p for (ts, s, p) in self.streams if s == stream and ts >= t]

    def error_records_since(self, t: float) -> list[Record]:
        return [decode_record(p) for p in self.stream_since(STREAM_ERRORS, t)]
