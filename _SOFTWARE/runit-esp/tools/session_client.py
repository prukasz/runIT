#!/usr/bin/env python3
"""A client of the rework firmware over its console: a session (SYS_SESSION.MD), commands, their answers.

    python runit-esp/tools/session_client.py COM10                 # the hello, PING and PEERS
    python runit-esp/tools/session_client.py COM10 28 00 01        # one command by hand: [key][dir][op][args]
    python runit-esp/tools/session_client.py COM10 --smoke         # PING, data matrix LIST, GPIO 4 level, a watch
    python runit-esp/tools/session_client.py COM10 --listen 2      # subscribe / run cmd, then print PUSH frames
    python runit-esp/tools/session_client.py COM10 --silence 4     # opens, then says nothing: the board ends it

Line: 00 cobs([frame][crc16 LE]) 00, CRC16-CCITT (poly 0x1021, init 0xFFFF) (PROVIDER_CONSOLE.MD "The line" / sys_uart_codec.h).
Frame: [type][src][key][seq u16][body] (CMD.MD "The header"); the client's id is 0xF0, the first of the clients'.
Bytes that are no frame (the boot's text, the log) are shown as text. Exit 0 when every step answered as it should.
"""
import argparse
import struct
import sys
import time

import serial  # pyserial (in the ESP-IDF python env)

CLIENT = 0xF0
REQUEST, ANSWER, PUSH, CONTROL = 0, 1, 2, 3
HELLO, PING, NO_SESSION = 1, 2, 3
KEY_SYS, KEY_SESSION, KEY_LOG, KEY_WATCH, KEY_DEVICE, KEY_MATRIX = 0x28, 0x0E, 0x2A, 0x2B, 0x20, 0x30
F_URGENT, F_ERROR = 0x01, 0x04
READ, WRITE = 0, 1
GPIO = 0x00  # RUNIT_DEV_GPIO
IO_LEVEL, IO_MODE = 0x0100, 0x0105


def crc16(data: bytes) -> int:
    crc = 0xFFFF
    for b in data:
        crc ^= b << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) & 0xFFFF if crc & 0x8000 else (crc << 1) & 0xFFFF
    return crc


def cobs_encode(data: bytes) -> bytes:
    out = bytearray([0])
    code_at, code = 0, 1
    for b in data:
        if b != 0:
            out.append(b)
            code += 1
        if b == 0 or code == 0xFF:
            out[code_at] = code
            code_at = len(out)
            out.append(0)
            code = 1
    out[code_at] = code
    return bytes(out)


def cobs_decode(data: bytes) -> bytes | None:
    out = bytearray()
    i = 0
    while i < len(data):
        code = data[i]
        i += 1
        if code == 0 or i + code - 1 > len(data):
            return None
        out += data[i:i + code - 1]
        i += code - 1
        if code != 0xFF and i < len(data):
            out.append(0)
    return bytes(out)


class Link:
    """One session to the board on a serial port."""

    def __init__(self, port: str, baud: int):
        s = serial.Serial()
        s.port, s.baudrate, s.timeout = port, baud, 0.02
        s.dtr = s.rts = False  # set before open: a devkit's DTR / RTS reset the chip
        s.open()
        s.reset_input_buffer()
        self.s, self.buf, self.seq, self.board, self.boot_id = s, bytearray(), 0, None, int(time.time()) & 0xFFFFFFFF
        self.pushes, self.push_seq, self.push_gaps = [], {}, []

    def send(self, kind: int, key: int, body: bytes, seq: int = 0, flags: int = 0):
        frame = bytes([(kind << 6) | flags, CLIENT, key]) + struct.pack("<H", seq) + body
        c = crc16(frame)
        self.s.write(b"\x00" + cobs_encode(frame + bytes([c & 0xFF, c >> 8])) + b"\x00")

    def frames(self, wait: float):
        """The frames that come in `wait` seconds, as (kind, flags, src, key, seq, body)."""
        end = time.monotonic() + wait
        while time.monotonic() < end:
            self.buf += self.s.read(512)
            while b"\x00" in self.buf:
                cut = self.buf.index(0)
                cand, self.buf = bytes(self.buf[:cut]), self.buf[cut + 1:]
                raw = cobs_decode(cand) if cand else None
                if raw is None or len(raw) < 7 or crc16(raw[:-2]) != (raw[-2] | raw[-1] << 8):
                    text = cand.decode("utf-8", "replace").strip()
                    if text:
                        print(f"   text: {text}")
                    continue
                f = raw[:-2]
                yield f[0] >> 6, f[0] & 0x3F, f[1], f[2], struct.unpack_from("<H", f, 3)[0], f[5:]

    def hello(self) -> bool:
        self.send(CONTROL, KEY_SESSION, bytes([HELLO]) + struct.pack("<II", self.boot_id, 0))  # a client has no build
        for kind, _, src, _, _, b in self.frames(2.0):
            if kind == CONTROL and b[:1] == bytes([HELLO]) and len(b) >= 9:
                boot, build = struct.unpack_from("<II", b, 1)
                bid = self.board = src
                print(f"<- hello: board {bid:#04x}, boot {boot:#010x}, build {build:#010x}")
                return True
        return False

    def _record_async(self, kind: int, flags: int, src: int, key: int, seq: int, b: bytes):
        if kind == PUSH:
            urgent = bool(flags & F_URGENT)
            cls = (src, urgent)
            prev = self.push_seq.get(cls)
            if prev is not None and seq != ((prev + 1) & 0xFFFF):
                self.push_gaps.append((cls, prev, seq))
                print(f"!! PUSH GAP from {src:#04x} urgent={int(urgent)}: expected {(prev + 1) & 0xFFFF}, got {seq}")
            self.push_seq[cls] = seq
            sid, payload = key, b
            self.pushes.append((flags, src, seq, sid, payload))
            tag = f"push {src:#04x} stream={sid:#04x} seq={seq}" + (" URGENT" if urgent else "")
            if sid == KEY_LOG:
                print(f"<- {tag}: {payload.decode('utf-8', 'replace')}")
            elif sid == KEY_WATCH and len(payload) >= 3:
                print(f"<- watch owner={payload[0]:#04x} id={payload[1]} status={payload[2]} seq={seq}: {payload[3:].hex(' ')}")
            else:
                print(f"<- {tag}: {payload.hex(' ')}")

    def listen(self, wait: float):
        for kind, flags, src, key, seq, b in self.frames(wait):
            self._record_async(kind, flags, src, key, seq, b)

    def call(self, dst: int, cmd: bytes, wait: float = 2.0):
        self.seq = (self.seq + 1) & 0xFFFF
        self.send(REQUEST, cmd[0], cmd[1:], self.seq)  # cmd is [key][dir][op][args]: the key goes in the header
        print(f"-> {dst:#04x} seq {self.seq}: {cmd.hex(' ')}")
        for kind, flags, src, key, seq, b in self.frames(wait):
            if kind == ANSWER and seq == self.seq:
                status = 1 if flags & F_ERROR else 0
                print(f"<- {src:#04x} seq {seq}: status {status} ({'OK' if status == 0 else 'ERROR'}) {b.hex(' ')}")
                return status, b
            self._record_async(kind, flags, src, key, seq, b)
        print("<- no answer")
        return None, b""


def run_smoke(link: Link, dst: int) -> bool:
    """PING, the data matrix's links, GPIO 4 as an output (level set and read back), a watch on its level."""
    pin = 4
    ok = True

    def dev(d: int, op: int, args: bytes = b"") -> bytes:
        return bytes([KEY_DEVICE, d, GPIO]) + struct.pack("<H", op) + args

    def step(name: str, cmd: bytes, want: bytes | None = None) -> bytes:
        nonlocal ok
        st, body = link.call(dst, cmd)
        good = st == 0 and (want is None or body == want)
        print(f"{'ok  ' if good else 'FAIL'} {name}")
        ok &= good
        return body

    step("PING", bytes([KEY_SYS, READ, 0x01]))
    links = step("data matrix LIST", bytes([KEY_MATRIX, READ, 0x03]))
    print(f"   {links[0] if links else 0} links")
    step("GPIO4 MODE output", dev(WRITE, IO_MODE, bytes([pin, 3])))
    for lvl in (1, 0, 1):
        step(f"GPIO4 LEVEL {lvl}", dev(WRITE, IO_LEVEL, bytes([pin, lvl])))
        step(f"GPIO4 LEVEL reads {lvl}", dev(READ, IO_LEVEL, bytes([pin])), bytes([pin, lvl]))

    # a PERIODIC watch (id 1, every 5 ticks = 50 ms) on GPIO4's level; the entries are PUSH frames of key WATCH
    link.pushes.clear()
    head = struct.pack("<BBHH", 1, 0, 5, 0)  # sys_watch_head_t: id, trigger, period, max_silence
    step("WATCH ADD", bytes([KEY_WATCH, WRITE, 0x01]) + head + dev(READ, IO_LEVEL, bytes([pin])))
    link.listen(0.5)
    entries = [p for p in link.pushes if p[3] == KEY_WATCH and len(p[4]) >= 3 and p[4][1] == 1]
    good = len(entries) >= 5 and all(p[4][2] == 0 and p[4][3:] == bytes([pin, 1]) for p in entries)
    print(f"{'ok  ' if good else 'FAIL'} WATCH entries: {len(entries)} in 0.5 s, level 1")
    ok &= good
    step("WATCH REMOVE", bytes([KEY_WATCH, WRITE, 0x02, 1]))
    step("GPIO4 RESET", dev(WRITE, 0x0106, bytes([pin])))
    print("smoke PASSED" if ok else "smoke FAILED")
    return ok


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("port")
    ap.add_argument("bytes", nargs="*", help="one command in hex bytes; default: PING and PEERS")
    ap.add_argument("--baud", type=int, default=115200, help="CONFIG_ESP_CONSOLE_UART_BAUDRATE")
    ap.add_argument("--silence", type=float, default=0, help="after the hello, say nothing this many seconds")
    ap.add_argument("--listen", type=float, default=0, help="after commands, listen for PUSH frames this many seconds")
    ap.add_argument("--smoke", action="store_true", help="PING, data matrix LIST, GPIO 4, a watch")
    args = ap.parse_args()

    link = Link(args.port, args.baud)
    if not link.hello():
        print("<- no hello")
        return 1
    dst = link.board
    ok = True
    if args.silence:
        link.listen(args.silence)
        status, _ = link.call(link.board, bytes([KEY_SYS, 0, 0x01]))
        ok = status is None  # the session ended: the request is answered NO_SESSION, not with an answer
        print("session ended by the silence" if ok else "session still open")
        return 0 if ok else 1
    if args.smoke:
        return 0 if run_smoke(link, dst) else 1
    cmds = [bytes(int(b, 16) for b in args.bytes)] if args.bytes else [bytes([KEY_SYS, 0, 0x01]),
                                                                       bytes([KEY_SESSION, 0, 0x02])]
    for cmd in cmds:
        status, _ = link.call(dst, cmd)
        ok &= status == 0
    if args.listen > 0:
        link.listen(args.listen)
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
