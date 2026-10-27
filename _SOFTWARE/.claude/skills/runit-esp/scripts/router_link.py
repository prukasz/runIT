"""Host side of the runIT router protocol over the console UART ("#R:<hex>" lines).

Works with a real board (COM port) and with QEMU (socket://localhost:5555).
Requests:  [seq][flags][key][dir][keys...][payload]  (ROUTER.MD; `frame` adds dir from the descriptors)
Responses: [0x05][seq][status][data]            status 1: data = error record
Streams:   [stream][payload]  0x02 telemetry, 0x03 logs, 0x04 errors (error record)

Error tag names are read from the firmware headers (X-macro maps), so they
never go stale.
"""

from __future__ import annotations

import json
import re
import struct
import threading
import time
from dataclasses import dataclass, field
from pathlib import Path

import serial  # pyserial (in the ESP-IDF python env)

ROOT = Path(__file__).resolve().parents[4]  # _SOFTWARE
ESP = ROOT / "runit-esp"  # the firmware project: components/, sdkconfig
PREFIX = "#R:"
STREAM_RESPONSE = 0x05
STREAM_TELEMETRY = 0x02
STREAM_LOGS = 0x03
STREAM_ERRORS = 0x04
FLAG_URGENT = 0x01
ANSI = re.compile(r"\x1b\[[0-9;]*[A-Za-z]")

# Root keys (router_ids.h) and transports (transport_e in sys_data_connector_config.h)
KEY_SYS, KEY_I2C, KEY_BLE, KEY_CONN, KEY_DEVICE, KEY_ACTIONS = 0x01, 0x02, 0x03, 0x04, 0x05, 0x06
TRANSPORT_UART, TRANSPORT_BLE = 1, 2
SEV_NAMES = ["NONE", "LOW", "MEDIUM", "HIGH", "CRITICAL"]


def load_tags() -> dict[int, tuple[str, int]]:
    """tag value -> (name, severity), from err_modules.h and every ERR_*_MAP in runit-esp/components/."""
    mods = {}
    for m in re.finditer(r"#define\s+(ERR_MOD_\w+)\s+(\d+)u", (ESP / "components/core/errors/err_modules.h").read_text()):
        mods[m.group(1)] = int(m.group(2))
    tags = {}
    pat = re.compile(r"X\(\s*(ERR_\w+)\s*,\s*ERR_TAG\(\s*(ERR_MOD_\w+)\s*,\s*(\d+)\s*\)\s*,\s*ERR_SEV_(\w+)")
    for h in (ESP / "components").rglob("*.h"):
        for m in pat.finditer(h.read_text(errors="ignore")):
            if m.group(2) in mods:
                tags[(mods[m.group(2)] << 16) | int(m.group(3))] = (m.group(1), SEV_NAMES.index(m.group(4)))
    return tags


TAGS = load_tags()

# ---- the direction byte: [key][dir][...] in every request (ROUTER.MD) ----

GENERATED = ROOT / "runit-app" / "generated"
READ, WRITE = 0, 1  # cmd_dir_e
SIDE_READ, SIDE_WRITE = 1, 2


def _sides(cmd: dict) -> int:
    return (SIDE_READ if "read" in cmd else 0) | (SIDE_WRITE if "write" in cmd else 0)


def load_sides() -> tuple[dict[int, dict[int, int]], dict[int, int]]:
    """The sides of every command, from the descriptors: {router key: {op: sides}} for the module routers and
    {u16 op: sides} for the device router (a contract's `(contract_id << 8) | op`, a device's own `0xFF00 | op`, the
    sides of every device that has it)."""
    modules: dict[int, dict[int, int]] = {}
    for f in (GENERATED / "services").glob("contract_*.json"):
        c = json.loads(f.read_text(encoding="utf8"))
        modules[c["router_key"]] = {x["op"]: _sides(x) for x in c["commands"]}
    device: dict[int, int] = {}
    for f in (GENERATED / "contracts").glob("contract_*.json"):
        c = json.loads(f.read_text(encoding="utf8"))
        for x in c.get("commands", []):
            device[(c["contract_id"] << 8) | x["op"]] = _sides(x)
    for f in (GENERATED / "devices").glob("device_*.json"):
        for x in json.loads(f.read_text(encoding="utf8")).get("commands", []):
            op = 0xFF00 | x["op"]
            device[op] = device.get(op, 0) | _sides(x)
    return modules, device


MODULE_SIDES, DEVICE_SIDES = load_sides()


def frame(keys: bytes, dir: int | None = None) -> bytes:
    """`keys` as the router takes them: the direction byte after the root key. Without `dir` it is the side the
    command has; a command with both sides needs `dir` (READ or WRITE). A key or op no descriptor names (a test of
    the router's errors) goes as a write. `keys` is [key][op...] for a module router and [0x05][id u16][op u16] for the
    device router."""
    if dir is None:
        if keys[0] == KEY_DEVICE and len(keys) >= 5:
            sides = DEVICE_SIDES.get(struct.unpack_from("<H", keys, 3)[0], SIDE_WRITE)
        elif len(keys) >= 2:
            sides = MODULE_SIDES.get(keys[0], {}).get(keys[1], SIDE_WRITE)
        else:
            sides = SIDE_WRITE
        if sides == SIDE_READ | SIDE_WRITE:
            raise ValueError(f"{keys.hex(' ')}: the command has a read and a write side, pass dir=READ or dir=WRITE")
        dir = READ if sides == SIDE_READ else WRITE
    return keys[:1] + bytes([dir]) + keys[1:]


@dataclass
class Node:
    tag: int
    data: bytes

    @property
    def name(self) -> str:
        return TAGS[self.tag][0] if self.tag in TAGS else f"0x{self.tag:08x}"

    @property
    def sev(self) -> int:
        """The tag's severity (its ERR_*_MAP); NONE for an unknown tag."""
        return TAGS[self.tag][1] if self.tag in TAGS else 0

    def __str__(self) -> str:
        return f"{self.name} {SEV_NAMES[self.sev]} {self.data.hex(' ')}".rstrip()


@dataclass
class Record:
    """err_record(): request (if any) + error chain."""

    src: int
    time_ms: int = 0  # the board's clock when it reported (UTC; before 2020: since the start)
    link: int = 0
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
        tag, ln = struct.unpack_from("<IB", b, pos)
        pos += 5
        nodes.append(Node(tag, b[pos : pos + ln]))
        pos += ln
    return nodes, cut


def decode_record(b: bytes) -> Record:
    src = b[0]
    (time_ms,) = struct.unpack_from("<Q", b, 1)
    if src == 0:
        nodes, cut = decode_chain(b[9:])
        return Record(src=0, time_ms=time_ms, nodes=nodes, chain_cut=cut)
    link, seq, flags, rl = struct.unpack_from("<BBBB", b, 9)
    n = rl & 0x7F
    req = b[13 : 13 + n]
    nodes, cut = decode_chain(b[13 + n :])
    return Record(1, time_ms, link, seq, flags, req, bool(rl & 0x80), nodes, cut)


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

    def request(self, keys: bytes, payload: bytes = b"", urgent: bool = False, timeout: float = 2.0,
                dir: int | None = None, raw: bool = False) -> Response | None:
        """`keys` without the direction byte (`frame` adds it; `dir` for a command with both sides). `raw`: `keys` go
        as they are (a test of the direction byte itself)."""
        seq = self.next_seq()
        with self._lock:
            self.responses.pop(seq, None)
        self.send_frame(bytes([seq, FLAG_URGENT if urgent else 0]) + (keys if raw else frame(keys, dir)) + payload)
        return self.wait_response(seq, timeout)

    def request_many(self, keys: bytes, payload: bytes, count: int, timeout: float = 2.0) -> list[Response]:
        """A command answered with `count` packets (actions READ): all of them in order, or what came (an error
        answer ends it early)."""
        seq = self.next_seq()
        with self._lock:
            self._parts[seq] = []
        self.send_frame(bytes([seq, 0]) + frame(keys) + payload)
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
