"""Send router packets by hand and show what comes back.

  python router_send.py --port COM10 "01 00 01" "02 00 02 01" "u 01 01 02"
  python router_send.py --port socket://localhost:5555 "03 00 06"

Each argument is one request: [key][dir][keys...] + payload in hex (spaces
optional; dir 00 read, 01 write, as typed); seq and flags are added. Prefix "u " for the urgent flag, "raw " to send the bytes
as-is (no seq/flags added). Prints the TX line, the response (decoded), and
every stream frame and log line that arrived meanwhile.
"""

from __future__ import annotations

import argparse
import time

from router_link import FLAG_URGENT, STREAM_ERRORS, STREAM_LOGS, STREAM_TELEMETRY, Link, decode_record

STREAM_NAMES = {STREAM_TELEMETRY: "telemetry", STREAM_LOGS: "logs", STREAM_ERRORS: "errors"}


def parse(arg: str) -> tuple[str, bytes]:
    words = arg.split()
    mode = "req"
    if words and words[0] in ("u", "raw"):
        mode = "urgent" if words[0] == "u" else "raw"
        words = words[1:]
    return mode, bytes.fromhex("".join(words))


def show_stream(stream: int, payload: bytes) -> str:
    name = STREAM_NAMES.get(stream, f"0x{stream:02x}")
    if stream == STREAM_ERRORS:
        return f"  << {name}: {decode_record(payload)}"
    if stream == STREAM_LOGS:
        return f"  << {name}: {payload.decode(errors='replace').rstrip()}"
    return f"  << {name}: {payload.hex(' ')}"


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", required=True)
    ap.add_argument("--wait", type=float, default=0.4, help="seconds to keep listening after the response")
    ap.add_argument("packets", nargs="+")
    a = ap.parse_args()

    with Link(a.port) as link:
        time.sleep(0.3)
        for arg in a.packets:
            mode, body = parse(arg)
            t0 = link.mark()
            if mode == "raw":
                frame, seq = body, body[0] if body else None
            else:
                seq = link.next_seq()
                frame = bytes([seq, FLAG_URGENT if mode == "urgent" else 0]) + body
            print(f">> #R:{frame.hex().upper()}" + ("   (urgent)" if mode == "urgent" else ""))
            link.send_frame(frame)
            resp = link.wait_response(seq, 2.0) if seq is not None else None
            if resp:
                print(f"  << {resp}  [{(resp.t - t0) * 1000:.0f} ms]")
            else:
                print("  << (no response)")
            time.sleep(a.wait)
            for ts, s, p in list(link.streams):
                if ts >= t0:
                    print(show_stream(s, p))
            for l in link.logs_since(t0):
                if l.strip():
                    print("  |", l)
            print()


if __name__ == "__main__":
    main()
