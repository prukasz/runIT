#!/usr/bin/env python3
"""One command to the rework firmware over its console, with the command binding (SYS_DATA_PROVIDER.MD "Command
binding"): no session, no header. The command goes out as a framed UART frame, its answer comes back the same way.

    python runit-esp-rework/tools/console_command.py COM5                 # PROVIDER LIST, the default
    python runit-esp-rework/tools/console_command.py COM5 0d 00 02        # the same, by hand: [key][dir][op][args]
    python runit-esp-rework/tools/console_command.py COM5 --baud 115200 0d 00 02

Line: 00 cobs([frame][crc16 LE]) 00, CRC16-CCITT (poly 0x1021, init 0xFFFF) (SYS_UART.MD "Operation").
Answer: [status][data]: status 0 OK, 1 ERROR (data the error record). Bytes that are no frame (the boot's text, the
log) are shown as text. Exit 0 on an OK answer, 1 on ERROR or none.
"""
import argparse
import sys
import time

import serial  # pyserial (in the ESP-IDF python env)


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


def frame(payload: bytes) -> bytes:
    c = crc16(payload)
    return b"\x00" + cobs_encode(payload + bytes([c & 0xFF, c >> 8])) + b"\x00"


def unframe(candidate: bytes) -> bytes | None:
    raw = cobs_decode(candidate)
    if raw is None or len(raw) <= 2:
        return None
    body, crc = raw[:-2], raw[-2] | (raw[-1] << 8)
    return body if crc16(body) == crc else None


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("port")
    ap.add_argument("bytes", nargs="*", help="the command in hex bytes; default 0d 00 02 (PROVIDER LIST)")
    ap.add_argument("--baud", type=int, default=115200, help="CONFIG_ESP_CONSOLE_UART_BAUDRATE")
    ap.add_argument("--wait", type=float, default=2.0, help="seconds to wait for the answer")
    args = ap.parse_args()
    cmd = bytes(int(b, 16) for b in args.bytes) if args.bytes else bytes([0x0D, 0x00, 0x02])

    with serial.Serial(args.port, args.baud, timeout=0.05) as s:
        s.reset_input_buffer()
        s.write(frame(cmd))
        print(f"-> {cmd.hex(' ')}")
        buf = bytearray()
        end = time.monotonic() + args.wait
        while time.monotonic() < end:
            buf += s.read(512)
            while b"\x00" in buf:
                cut = buf.index(0)
                candidate, buf = bytes(buf[:cut]), buf[cut + 1:]
                answer = unframe(candidate) if candidate else None
                if answer is None:
                    text = candidate.decode("utf-8", "replace").strip()
                    if text:
                        print(f"   text: {text}")
                    continue
                status, data = answer[0], answer[1:]
                print(f"<- status {status} ({'OK' if status == 0 else 'ERROR'}) data {data.hex(' ')}")
                return 0 if status == 0 else 1
    print("<- no answer")
    return 1


if __name__ == "__main__":
    sys.exit(main())
