"""Host side of the runIT protocol over the console UART: one client session (SYS_SESSION.MD) on a binary line.

Works with a real board (COM port) and with QEMU (socket://localhost:5555).
Line:      00 cobs([frame][crc16 LE]) 00   (SYS_DATA_PROVIDER.MD "UART"); bytes that are no frame are text (the ROM,
           a panic), kept as log lines
Frame:     [frame byte][dst][src][seq u16][body]   (sys_session_hdr_t, 5 B)
Request:   kind 0, flags in bits 0-1; body [key][dir][keys...][payload]  (`frame` adds dir from the descriptors)
Answer:    kind 1, status in bit 2; body = data, or the error record (ERROR)
Stream:    kind 2; body [connector][payload]  0x02 telemetry, 0x03 logs, 0x04 errors (error record), 0x06 events
Control:   kind 3; body [op][data]  HELLO, PING, NO_SESSION

The link opens the session (HELLO: the board gives this client its id), keeps it alive (PING every third of the
board's timeout), opens it again when the board restarts (a new boot_id, NO_SESSION) and subscribes the streams, so
logs, errors, telemetry and events come as they did on a console.

Error tag names are read from the firmware headers (X-macro maps), so they never go stale.
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
STREAM_TELEMETRY = 0x02
STREAM_LOGS = 0x03
STREAM_ERRORS = 0x04
STREAM_EVENTS = 0x06
SUBSCRIBED = (STREAM_LOGS, STREAM_ERRORS, STREAM_TELEMETRY, STREAM_EVENTS)
FLAG_URGENT = 0x01
FLAG_NOREPLY = 0x02
ANSI = re.compile(r"\x1b\[[0-9;]*[A-Za-z]")

# Root keys (router_ids.h) and provider links (sys_data_provider_link_e)
KEY_SYS, KEY_I2C, KEY_BLE, KEY_CONN, KEY_DEVICE, KEY_ACTIONS = 0x01, 0x02, 0x03, 0x04, 0x05, 0x06
KEY_PROVIDER, KEY_SESSION = 0x0D, 0x0E
LINK_UART, LINK_BLE = 1, 2
SEV_NAMES = ["NONE", "LOW", "MEDIUM", "HIGH", "CRITICAL"]

# The session (sys_session_config.h, sys_session_frame.h)
SESSION_VERSION = 1
KIND_REQUEST, KIND_ANSWER, KIND_STREAM, KIND_CONTROL = 0, 1, 2, 3
CTRL_HELLO, CTRL_PING, CTRL_NO_SESSION = 1, 2, 3
ID_NONE, ID_SELF, ID_BROADCAST = 0xFD, 0xFE, 0xFF
HDR = struct.Struct("<BBBH")
HELLO = struct.Struct("<BBB6sIIHHHB")
CLIENT_TIMEOUT_MS = 60000  # the host never ends the session itself
CONN_SUBSCRIBE = 0x09


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


# ---- the line: COBS + CRC16 ----


def crc16(data: bytes) -> int:
    """CRC16-CCITT: poly 0x1021, init 0xFFFF, no reflection (sys_uart.c)."""
    crc = 0xFFFF
    for b in data:
        crc ^= b << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) & 0xFFFF if crc & 0x8000 else (crc << 1) & 0xFFFF
    return crc


def cobs_encode(data: bytes) -> bytes:
    out, block = bytearray(), bytearray()
    for b in data:
        if b == 0:
            out += bytes([len(block) + 1]) + block
            block = bytearray()
        else:
            block.append(b)
            if len(block) == 254:
                out += b"\xff" + block
                block = bytearray()
    return bytes(out + bytes([len(block) + 1]) + block)


def cobs_decode(data: bytes) -> bytes | None:
    out, i = bytearray(), 0
    while i < len(data):
        code = data[i]
        if code == 0 or i + code > len(data):
            return None
        out += data[i + 1 : i + code]
        i += code
        if code != 0xFF and i < len(data):
            out.append(0)
    return bytes(out)


def encode_line(frame_bytes: bytes) -> bytes:
    crc = crc16(frame_bytes)
    return b"\x00" + cobs_encode(frame_bytes + bytes([crc & 0xFF, crc >> 8])) + b"\x00"


def decode_candidate(data: bytes) -> bytes | None:
    """One candidate (the bytes between two 0x00): the frame, or None when it is no frame."""
    raw = cobs_decode(data)
    if raw is None or len(raw) <= 2:
        return None
    if crc16(raw[:-2]) != raw[-2] | (raw[-1] << 8):
        return None
    return raw[:-2]


# ---- error records ----


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
    """err_record(): the command (if any) + error chain."""

    src: int
    time_ms: int = 0  # the board's clock when it reported (UTC; before 2020: since the start)
    sender: int = 0  # err_record_cmd_t.from: the peer id that sent the command; 0xFE the board itself
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
            f"cmd from {self.sender:02x} seq {self.seq} flags {self.flags:02x} req {self.request.hex(' ')}"
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


RECORD_CMD = struct.Struct("<BHBB")  # err_record_cmd_t: from, seq, flags, req_len


def decode_record(b: bytes) -> Record:
    src = b[0]
    (time_ms,) = struct.unpack_from("<Q", b, 1)
    if src == 0:
        nodes, cut = decode_chain(b[9:])
        return Record(src=0, time_ms=time_ms, nodes=nodes, chain_cut=cut)
    sender, seq, flags, rl = RECORD_CMD.unpack_from(b, 9)
    n = rl & 0x7F
    at = 9 + RECORD_CMD.size
    req = b[at : at + n]
    nodes, cut = decode_chain(b[at + n :])
    return Record(1, time_ms, sender, seq, flags, req, bool(rl & 0x80), nodes, cut)


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
    def __init__(self, url: str, baud: int = 115200, echo: bool = False, open_timeout: float = 3.0):
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
        self.logs: list[tuple[float, str]] = []  # the LOGS stream's lines and the console's text
        self.console: list[tuple[float, str]] = []  # only the text on the line that is no frame
        self.streams: list[tuple[float, int, bytes]] = []
        self.responses: dict[int, Response] = {}
        self._seq = 0
        self._lock = threading.Lock()
        self._write_lock = threading.Lock()
        self._stop = False
        # The session: this client's id and the board's, its boot id and timeout (from its hello).
        self.my_id = ID_NONE
        self.board_id = 0
        self.boot_id: int | None = None
        self.board_timeout_ms = 3000
        self.opened = threading.Event()
        self.opens = 0  # sessions opened so far: a restart of the board opens a new one
        self._last_tx = 0.0
        self._resubscribe = False
        self.silent = False  # True: no PING (a test of the board's session timeout, the dead-man)
        self._thread = threading.Thread(target=self._reader, daemon=True)
        self._thread.start()
        self._pinger = threading.Thread(target=self._keepalive, daemon=True)
        self._pinger.start()
        self.hello()
        self.wait_open(open_timeout)

    def close(self) -> None:
        self._stop = True
        self._thread.join(1)
        self._pinger.join(1)
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

    # ---- the session --------------------------------------------------------

    def _write(self, data: bytes) -> None:
        with self._write_lock:
            try:
                self.ser.write(data)
            except (serial.SerialException, OSError):
                pass  # the port is coming back (a reset): the reader reopens it, the session opens again
            self._last_tx = time.time()

    def send_session(self, kind: int, flags: int, dst: int, seq: int, body: bytes) -> None:
        self._write(encode_line(HDR.pack((kind << 6) | flags, dst, self.my_id, seq) + body))

    def hello(self) -> None:
        h = HELLO.pack(SESSION_VERSION, ID_NONE, ID_NONE, bytes(6), 0, 0, 0, 0, CLIENT_TIMEOUT_MS, 0)
        self.send_session(KIND_CONTROL, 0, self.board_id, 0, bytes([CTRL_HELLO]) + h)

    def wait_open(self, timeout: float) -> bool:
        """Waits for the session; asks again (HELLO) every 0.5 s: a board still booting has not heard the first."""
        end = time.time() + timeout
        while time.time() < end:
            if self.opened.wait(0.5):
                return True
            self.hello()
        return self.opened.is_set()

    def wait_reopen(self, opens_before: int, timeout: float = 10.0) -> bool:
        """After a restart of the board: waits until a new session opened (and its streams subscribed)."""
        end = time.time() + timeout
        while time.time() < end:
            if self.opens > opens_before and self.opened.is_set():
                return True
            time.sleep(0.05)
        return False

    def _subscribe(self) -> None:
        for c in SUBSCRIBED:
            seq = self.next_seq()
            body = frame(bytes([KEY_CONN, CONN_SUBSCRIBE]), WRITE) + bytes([c])
            self.send_session(KIND_REQUEST, FLAG_NOREPLY, self.board_id, seq, body)

    def _keepalive(self) -> None:
        while not self._stop:
            time.sleep(0.05)
            if self._resubscribe:
                self._resubscribe = False
                self._subscribe()
            if self.silent:
                continue
            if self.opened.is_set() and time.time() - self._last_tx >= self.board_timeout_ms / 3000.0 * 0.8:
                try:
                    self.send_session(KIND_CONTROL, 0, self.board_id, 0, bytes([CTRL_PING]))
                except Exception:
                    pass

    def _on_control(self, body: bytes) -> None:
        if not body:
            return
        if body[0] == CTRL_HELLO and len(body) >= 1 + HELLO.size:
            version, board, your_id, _mac, boot_id, _h, _t, _fw, timeout, _n = HELLO.unpack_from(body, 1)
            if version != SESSION_VERSION:
                return
            self.board_timeout_ms = timeout or 3000
            if your_id != ID_NONE and (not self.opened.is_set() or boot_id != self.boot_id or your_id != self.my_id):
                self.my_id, self.board_id, self.boot_id = your_id, board, boot_id
                self.opens += 1
                self.opened.set()
                self._resubscribe = True
            elif your_id == ID_NONE and boot_id != self.boot_id:
                # The board restarted (its hello at boot): open the session again.
                self.opened.clear()
                self.my_id = ID_NONE
                self.boot_id = boot_id
                self.hello()
        elif body[0] == CTRL_NO_SESSION:
            self.opened.clear()
            self.my_id = ID_NONE
            self.hello()

    # ---- reader -------------------------------------------------------------

    def _reader(self) -> None:
        buf = bytearray()
        while not self._stop:
            try:
                chunk = self.ser.read(512)
            except (serial.SerialException, OSError):
                self._reopen_port()  # the native USB port re-enumerates on a reset: wait for it
                continue
            if not chunk:
                continue
            for b in chunk:
                if b != 0:
                    buf.append(b)
                    continue
                if buf:
                    self._candidate(bytes(buf))
                buf.clear()

    def _reopen_port(self) -> None:
        try:
            self.ser.close()
        except (serial.SerialException, OSError):
            pass
        while not self._stop:
            time.sleep(0.2)
            try:
                self.ser.rts = False
                self.ser.dtr = False
                self.ser.open()
                return
            except (serial.SerialException, OSError, AttributeError, ValueError):
                pass

    def _text(self, now: float, data: bytes, console: bool) -> None:
        for line in data.decode(errors="replace").replace("\r", "").split("\n"):
            clean = ANSI.sub("", line)
            if clean.strip():
                with self._lock:
                    self.logs.append((now, clean))
                    if console:
                        self.console.append((now, clean))
                if self.echo:
                    print("  |", clean)

    def _candidate(self, data: bytes) -> None:
        now = time.time()
        f = decode_candidate(data)
        if f is None or len(f) < HDR.size:
            self._text(now, data, True)  # no frame: the ROM's text, a panic, the mirrored log
            return
        fb, dst, src, seq = HDR.unpack_from(f)
        body = f[HDR.size :]
        kind = fb >> 6
        if kind == KIND_CONTROL:
            self._on_control(body)
        elif kind == KIND_ANSWER:
            with self._lock:
                self.responses[seq] = Response(seq, (fb >> 2) & 1, body, now)
        elif kind == KIND_STREAM and body:
            with self._lock:
                self.streams.append((now, body[0], body[1:]))
            if body[0] == STREAM_LOGS:
                self._text(now, body[1:], False)

    # ---- requests -----------------------------------------------------------

    def next_seq(self) -> int:
        with self._lock:
            self._seq = (self._seq + 1) & 0xFFFF
            return self._seq

    def send_raw(self, seq: int, flags: int, body: bytes, dst: int | None = None) -> None:
        """One request as it is: `body` from the root key (a test of the router's own checks)."""
        self.send_session(KIND_REQUEST, flags, self.board_id if dst is None else dst, seq, body)

    def request(self, keys: bytes, payload: bytes = b"", urgent: bool = False, timeout: float = 2.0,
                dir: int | None = None, raw: bool = False, dst: int | None = None) -> Response | None:
        """`keys` without the direction byte (`frame` adds it; `dir` for a command with both sides). `raw`: `keys` go
        as they are (a test of the direction byte itself). `dst`: another board of the tree (the root forwards it)."""
        seq = self.next_seq()
        with self._lock:
            self.responses.pop(seq, None)
        self.send_raw(seq, FLAG_URGENT if urgent else 0, (keys if raw else frame(keys, dir)) + payload, dst)
        return self.wait_response(seq, timeout)

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

    def console_since(self, t: float) -> list[str]:
        with self._lock:
            return [l for (ts, l) in self.console if ts >= t]

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


class LineSplitter:
    """Splits raw console bytes into text and frames, for the capture tools: `feed(bytes)` returns what it can say now."""

    KINDS = {KIND_REQUEST: "request", KIND_ANSWER: "answer", KIND_STREAM: "stream", KIND_CONTROL: "control"}

    def __init__(self) -> None:
        self.buf = bytearray()

    def feed(self, data: bytes) -> str:
        out = []
        for b in data:
            if b != 0:
                self.buf.append(b)
                continue
            if self.buf:
                out.append(self._say(bytes(self.buf)))
            self.buf.clear()
        return "".join(out)

    def _say(self, data: bytes) -> str:
        f = decode_candidate(data)
        if f is None or len(f) < HDR.size:
            return data.decode("utf-8", errors="replace")
        fb, dst, src, seq = HDR.unpack_from(f)
        body = f[HDR.size :]
        kind = self.KINDS[fb >> 6]
        if kind == "stream" and body and body[0] == STREAM_LOGS:
            return body[1:].decode("utf-8", errors="replace").rstrip("\n") + "\n"
        return f"<frame {kind} {src:02x}->{dst:02x} seq {seq} {body[:24].hex(' ')}{' ...' if len(body) > 24 else ''}>\n"
