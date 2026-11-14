#!/usr/bin/env python3
"""A client of the rework firmware over its console: a session (SYS_SESSION.MD), commands, their answers.

    python runit-esp-rework/tools/session_client.py COM10                 # the hello, PING and PEERS
    python runit-esp-rework/tools/session_client.py COM10 28 00 01        # one command by hand: [key][dir][op][args]
    python runit-esp-rework/tools/session_client.py COM10 --dst 9 0e 00 04  # to a slave of this board, through it
    python runit-esp-rework/tools/session_client.py COM10 --listen 2      # subscribe / run cmd, then print PUSH frames
    python runit-esp-rework/tools/session_client.py COM10 --stage5        # verify Stage 5 (sys_stream + sys_log)
    python runit-esp-rework/tools/session_client.py COM10 --silence 4     # opens, then says nothing: the board ends it

Line: 00 cobs([frame][crc16 LE]) 00, CRC16-CCITT (poly 0x1021, init 0xFFFF) (SYS_UART.MD "The line").
Frame: [frame byte][dst][src][seq u16][body] (SYS_SESSION.MD "Frames"); the client's id is the app's, 0xF0.
Bytes that are no frame (the boot's text, the log) are shown as text. Exit 0 when every step answered as it should.
"""
import argparse
import struct
import sys
import time

import serial  # pyserial (in the ESP-IDF python env)

CLIENT = 0xF0
REQUEST, ANSWER, PUSH, CONTROL = 0, 1, 2, 3
HELLO, PING, NO_SESSION, PEER = 1, 2, 3, 4
KEY_SYS, KEY_SESSION, KEY_STREAM, KEY_LOG, KEY_WATCH = 0x28, 0x0E, 0x29, 0x2A, 0x2B
STREAM_TELEMETRY, STREAM_LOGS, STREAM_ERRORS, STREAM_EVENTS = 0x02, 0x03, 0x04, 0x06
CH_WATCHES = 0x80
F_URGENT, F_ERROR, F_UNCOUNTED = 0x01, 0x04, 0x08


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

    def send(self, kind: int, dst: int, body: bytes, seq: int = 0, flags: int = 0):
        frame = bytes([(kind << 6) | flags, dst, CLIENT]) + struct.pack("<H", seq) + body
        c = crc16(frame)
        self.s.write(b"\x00" + cobs_encode(frame + bytes([c & 0xFF, c >> 8])) + b"\x00")

    def frames(self, wait: float):
        """The frames that come in `wait` seconds, as (kind, flags, dst, src, seq, body)."""
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

    def hello(self, timeout_ms: int = 3000) -> bool:
        body = bytes([HELLO, CLIENT]) + struct.pack("<IIH", self.boot_id, 0, timeout_ms)
        self.send(CONTROL, 0xFF, body)
        for kind, _, _, src, _, b in self.frames(2.0):
            if kind == CONTROL and b[:1] == bytes([HELLO]) and len(b) >= 12:
                bid, boot, build, tmo = struct.unpack_from("<BIIH", b, 1)
                self.board = bid
                print(f"<- hello: board {bid:#04x}, boot {boot:#010x}, build {build:#010x}, timeout {tmo} ms")
                return True
        return False

    def _record_async(self, kind: int, flags: int, src: int, seq: int, b: bytes):
        if kind == PUSH:
            urgent = bool(flags & F_URGENT)
            if not (flags & F_UNCOUNTED):
                key = (src, urgent)
                prev = self.push_seq.get(key)
                if prev is not None and seq != ((prev + 1) & 0xFFFF):
                    self.push_gaps.append((key, prev, seq))
                    print(f"!! PUSH GAP from {src:#04x} urgent={int(urgent)}: expected {(prev + 1) & 0xFFFF}, got {seq}")
                self.push_seq[key] = seq
            sid, payload = (b[0], b[1:]) if b else (0, b"")
            self.pushes.append((flags, src, seq, sid, payload))
            tag = f"push {src:#04x} stream={sid:#04x} seq={seq}" + (" URGENT" if urgent else "")
            if sid == STREAM_LOGS:
                print(f"<- {tag}: {payload.decode('utf-8', 'replace')}")
            elif sid == CH_WATCHES and len(payload) >= 2:
                print(f"<- watch id={payload[0]} status={payload[1]} seq={seq}: {payload[2:].hex(' ')}")
            else:
                print(f"<- {tag}: {payload.hex(' ')}")
        elif kind == CONTROL and b[:1] == bytes([PEER]) and len(b) >= 3:
            print(f"<- peer {b[1]:#04x} {'up' if b[2] else 'gone'}")

    def listen(self, wait: float):
        for kind, flags, _, src, seq, b in self.frames(wait):
            self._record_async(kind, flags, src, seq, b)

    def call(self, dst: int, cmd: bytes, wait: float = 2.0):
        self.seq = (self.seq + 1) & 0xFFFF
        self.send(REQUEST, dst, cmd, self.seq)
        print(f"-> {dst:#04x} seq {self.seq}: {cmd.hex(' ')}")
        for kind, flags, _, src, seq, b in self.frames(wait):
            if kind == ANSWER and seq == self.seq:
                status = 1 if flags & F_ERROR else 0
                print(f"<- {src:#04x} seq {seq}: status {status} ({'OK' if status == 0 else 'ERROR'}) {b.hex(' ')}")
                return status, b
            self._record_async(kind, flags, src, seq, b)
        print("<- no answer")
        return None, b""


def _decode_streams(buf: bytes) -> dict[int, tuple[int, int, list[tuple[int, int, int, int]]]]:
    out, pos = {}, 1
    for _ in range(buf[0]):
        sid, flags, state, ntargets = struct.unpack_from("<BBBB", buf, pos)
        pos += 4
        targets = []
        for _ in range(ntargets):
            targets.append(struct.unpack_from("<BBBH", buf, pos))
            pos += 5
        out[sid] = (flags, state, targets)
    assert pos == len(buf), f"LIST trailing bytes: {pos} != {len(buf)}"
    return out


def run_stage5(link: Link, dst: int) -> bool:
    def expect_ok(cmd: bytes) -> bytes | None:
        st, body = link.call(dst, cmd)
        return body if st == 0 else None

    def expect_err(cmd: bytes) -> bytes | None:
        st, body = link.call(dst, cmd)
        return body if st == 1 else None

    cfg = expect_ok(bytes([KEY_LOG, 0, 0x01]))
    if cfg != bytes([3, 0, 1, 1, 1]):
        print(f"FAIL: unexpected default LOG CONFIG: {cfg!r}")
        return False

    raw_list = expect_ok(bytes([KEY_STREAM, 0, 0x07]))
    if raw_list is None:
        return False
    streams = _decode_streams(raw_list)
    for sid, exp_flags in [(STREAM_TELEMETRY, 0), (STREAM_LOGS, 0), (STREAM_ERRORS, 1), (STREAM_EVENTS, 1)]:
        if sid not in streams or streams[sid][:2] != (exp_flags, 1):
            print(f"FAIL: system stream {sid:#04x} missing or bad metadata: {streams.get(sid)}")
            return False

    if expect_ok(bytes([KEY_STREAM, 1, 0x08, STREAM_LOGS])) is None:
        return False
    if expect_ok(bytes([KEY_STREAM, 1, 0x08, STREAM_ERRORS])) is None:
        return False

    # Valid LOG CONFIG write (level=2 WARN <= CONFIG_LOG_MAXIMUM_LEVEL) + restore + out-of-range write (ERR_LOG_BAD_LEVEL) + system stream remove (ERR_STREAM_SYSTEM).
    if expect_ok(bytes([KEY_LOG, 1, 0x01, 2, 0, 1, 1, 1])) is None:
        return False
    if expect_ok(bytes([KEY_LOG, 0, 0x01])) != bytes([2, 0, 1, 1, 1]):
        print("FAIL: LOG CONFIG did not update")
        return False
    if expect_ok(bytes([KEY_LOG, 1, 0x01, 3, 0, 1, 1, 1])) is None:
        return False
    if expect_err(bytes([KEY_LOG, 1, 0x01, 99, 0, 1, 1, 1])) is None:
        return False
    if expect_err(bytes([KEY_STREAM, 1, 0x02, STREAM_ERRORS])) is None:
        return False

    link.listen(0.4)
    err_pushes = [p for p in link.pushes if p[3] == STREAM_ERRORS]
    log_pushes = [p for p in link.pushes if p[3] == STREAM_LOGS]
    if len(err_pushes) < 2 or not all(p[0] & F_URGENT for p in err_pushes):
        print(f"FAIL: expected >=2 urgent ERRORS pushes, got {len(err_pushes)}: {err_pushes}")
        return False
    if not log_pushes or not any(b"ERR_" in p[4] for p in log_pushes):
        print(f"FAIL: expected traced ERR_* line on LOGS stream, got {len(log_pushes)}")
        return False
    if link.push_gaps:
        print(f"FAIL: PUSH sequence gaps: {link.push_gaps}")
        return False

    hist = expect_ok(bytes([KEY_LOG, 0, 0x03, 0]))
    if hist is None or len(hist) < 2 or hist[0] < 2:
        print(f"FAIL: expected >=2 stored error records, got {hist!r}")
        return False
    past = expect_ok(bytes([KEY_LOG, 0, 0x03, 250]))
    if past != bytes([hist[0]]):
        print(f"FAIL: expected past-end LOG ERRORS [total], got {past!r}")
        return False

    # User stream 0x10 lifecycle: CREATE -> SUBSCRIBE -> SINK (remote) -> LIST -> UNBIND_ALL -> REMOVE.
    link.call(dst, bytes([KEY_STREAM, 1, 0x02, 0x10]))
    if expect_ok(bytes([KEY_STREAM, 1, 0x01, 0x10, 0x00])) is None:
        return False
    if expect_ok(bytes([KEY_STREAM, 1, 0x08, 0x10])) is None:
        return False
    if expect_ok(bytes([KEY_STREAM, 1, 0x04, 0x10, 0x22, 0x01])) is None:
        return False
    raw_after = expect_ok(bytes([KEY_STREAM, 0, 0x07]))
    if raw_after is None:
        return False
    st_after = _decode_streams(raw_after)
    targets = st_after.get(0x10, (0, 0, []))[2]
    if not any(t[0] == 2 and t[1] == CLIENT for t in targets) or not any(t[0] == 1 and t[1] == 0x22 and t[2] == 0x01 for t in targets):
        print(f"FAIL: user stream 0x10 missing expected PEER/SINK targets in LIST: {st_after.get(0x10)}")
        return False
    if expect_ok(bytes([KEY_STREAM, 1, 0x06, 0x10])) is None:
        return False
    if expect_ok(bytes([KEY_STREAM, 1, 0x02, 0x10])) is None:
        return False
    if expect_err(bytes([KEY_STREAM, 1, 0x02, 0x10])) is None:
        return False

    expect_ok(bytes([KEY_STREAM, 1, 0x09, STREAM_LOGS]))
    expect_ok(bytes([KEY_STREAM, 1, 0x09, STREAM_ERRORS]))
    print("Stage 5 verification PASSED")
    return True


def run_stage7(link: Link, dst: int) -> bool:
    def expect_ok(cmd: bytes) -> bytes | None:
        st, body = link.call(dst, cmd)
        return body if st == 0 else None

    def expect_err(cmd: bytes) -> bytes | None:
        st, body = link.call(dst, cmd)
        return body if st == 1 else None

    def watch_head(wid: int, trigger: int, period: int, silence: int = 0) -> bytes:
        # sys_watch_head_t (14 B): u8 id, u8 trigger, u16 period, u16 max_silence, u32 tag, u16 source, u16 channel
        return struct.pack("<BBHHIHH", wid, trigger, period, silence, 0, 0, 0)

    # Validation: bad id (0) and non-read command refused.
    ping_cmd = bytes([KEY_SYS, 0, 0x01])
    log_cfg_r = bytes([KEY_LOG, 0, 0x01])
    log_cfg_w2 = bytes([KEY_LOG, 1, 0x01, 2, 0, 1, 1, 1])
    log_cfg_w3 = bytes([KEY_LOG, 1, 0x01, 3, 0, 1, 1, 1])
    if expect_err(bytes([KEY_WATCH, 1, 0x01]) + watch_head(0, 0, 5) + ping_cmd) is None:
        return False
    if expect_err(bytes([KEY_WATCH, 1, 0x01]) + watch_head(1, 0, 5) + log_cfg_w2) is None:
        return False

    # 1. PERIODIC watch id=1 on SYS PING (u32 uptime every 50 ms) -> check [0x80][id][status][u32] (NO owner byte!).
    link.pushes.clear()
    if expect_ok(bytes([KEY_WATCH, 1, 0x01]) + watch_head(1, 0, 5) + ping_cmd) is None:
        return False
    link.listen(0.22)
    w1 = [p for p in link.pushes if p[3] == CH_WATCHES and len(p[4]) >= 2 and p[4][0] == 1]
    if len(w1) < 2 or not all(p[4][1] == 0 and len(p[4]) == 6 for p in w1):
        print(f"FAIL: expected >=2 PERIODIC 6-byte [id,status,u32] entries for watch 1, got {w1}")
        return False
    t0 = struct.unpack_from("<I", w1[0][4], 2)[0]
    t1 = struct.unpack_from("<I", w1[-1][4], 2)[0]
    if t1 <= t0:
        print(f"FAIL: expected increasing uptime in watch 1 entries, got {t0} -> {t1}")
        return False

    # 2. ON_CHANGE watch id=2 on LOG CONFIG (period=20ms, max_silence=160ms).
    link.pushes.clear()
    if expect_ok(bytes([KEY_WATCH, 1, 0x01]) + watch_head(2, 1, 2, 16) + log_cfg_r) is None:
        return False
    link.listen(0.08)
    w2_init = [p for p in link.pushes if p[3] == CH_WATCHES and len(p[4]) >= 2 and p[4][0] == 2]
    if len(w2_init) != 1 or w2_init[0][4][1:] != bytes([0, 3, 0, 1, 1, 1]):
        print(f"FAIL: expected single initial ON_CHANGE entry for watch 2, got {w2_init}")
        return False

    # Mutate LOG CONFIG -> ON_CHANGE emits promptly; restore -> emits promptly.
    link.pushes.clear()
    if expect_ok(log_cfg_w2) is None:
        return False
    link.listen(0.06)
    w2_chg = [p for p in link.pushes if p[3] == CH_WATCHES and len(p[4]) >= 2 and p[4][0] == 2]
    if not w2_chg or w2_chg[-1][4][1:] != bytes([0, 2, 0, 1, 1, 1]):
        print(f"FAIL: ON_CHANGE did not push mutated config: {w2_chg}")
        return False
    link.pushes.clear()
    if expect_ok(log_cfg_w3) is None:
        return False
    link.listen(0.06)
    w2_rst = [p for p in link.pushes if p[3] == CH_WATCHES and len(p[4]) >= 2 and p[4][0] == 2]
    if not w2_rst or w2_rst[-1][4][1:] != bytes([0, 3, 0, 1, 1, 1]):
        print(f"FAIL: ON_CHANGE did not push restored config: {w2_rst}")
        return False

    # Wait past max_silence (160 ms) -> unchanged heartbeat entry arrives.
    link.pushes.clear()
    link.listen(0.24)
    w2_sil = [p for p in link.pushes if p[3] == CH_WATCHES and len(p[4]) >= 2 and p[4][0] == 2]
    if not w2_sil:
        print("FAIL: expected ON_CHANGE max_silence heartbeat for watch 2")
        return False

    # 3. Replace watch 1 with SESSION PEERS (0x0E 00 04), verify WATCH LIST (16 B rows), then REMOVE both.
    if expect_ok(bytes([KEY_WATCH, 1, 0x01]) + watch_head(1, 0, 20) + bytes([KEY_SESSION, 0, 0x04])) is None:
        return False
    raw_list = expect_ok(bytes([KEY_WATCH, 0, 0x03]))
    if raw_list is None or len(raw_list) != 1 + 2 * 16 or raw_list[0] != 2:
        print(f"FAIL: unexpected WATCH LIST reply: {raw_list!r}")
        return False
    if expect_ok(bytes([KEY_WATCH, 1, 0x02, 1])) is None:
        return False
    if expect_ok(bytes([KEY_WATCH, 1, 0x02, 2])) is None:
        return False
    if expect_err(bytes([KEY_WATCH, 1, 0x02, 2])) is None:
        return False
    raw_empty = expect_ok(bytes([KEY_WATCH, 0, 0x03]))
    if raw_empty != bytes([0]):
        print(f"FAIL: expected empty WATCH LIST after REMOVE, got {raw_empty!r}")
        return False
    if link.push_gaps:
        print(f"FAIL: PUSH sequence gaps during Stage 7: {link.push_gaps}")
        return False

    print("Stage 7 verification PASSED")
    return True


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("port")
    ap.add_argument("bytes", nargs="*", help="one command in hex bytes; default: PING and PEERS")
    ap.add_argument("--baud", type=int, default=115200, help="CONFIG_ESP_CONSOLE_UART_BAUDRATE")
    ap.add_argument("--dst", type=lambda v: int(v, 0), default=None, help="the board it goes to (default: this one)")
    ap.add_argument("--silence", type=float, default=0, help="after the hello, say nothing this many seconds")
    ap.add_argument("--listen", type=float, default=0, help="after commands, listen for PUSH frames this many seconds")
    ap.add_argument("--stage5", action="store_true", help="run Stage 5 (sys_stream + sys_log) verification")
    ap.add_argument("--stage7", action="store_true", help="run Stage 7 (sys_watch) verification")
    args = ap.parse_args()

    link = Link(args.port, args.baud)
    if not link.hello():
        print("<- no hello")
        return 1
    dst = link.board if args.dst is None else args.dst
    ok = True
    if args.silence:
        link.listen(args.silence)
        status, _ = link.call(link.board, bytes([KEY_SYS, 0, 0x01]))
        ok = status is None  # the session ended: the request is answered NO_SESSION, not with an answer
        print("session ended by the silence" if ok else "session still open")
        return 0 if ok else 1
    if args.stage5:
        return 0 if run_stage5(link, dst) else 1
    if args.stage7:
        return 0 if run_stage7(link, dst) else 1
    cmds = [bytes(int(b, 16) for b in args.bytes)] if args.bytes else [bytes([KEY_SYS, 0, 0x01]),
                                                                       bytes([KEY_SESSION, 0, 0x04])]
    for cmd in cmds:
        status, _ = link.call(dst, cmd)
        ok &= status == 0
    if args.listen > 0:
        link.listen(args.listen)
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
