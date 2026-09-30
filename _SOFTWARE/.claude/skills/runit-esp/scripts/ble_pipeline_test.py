"""Pipelined commands over BLE: N commands written back-to-back before any answer.

Checks what the app's CommandClient relies on when maxInFlight > 1: every command
is answered once, with its own sequence byte, and seq 0 / 255 echo like any other.
Measures round-trip time per depth. Commands are read-only (power status).

Usage (a Python with bleak):
    python ble_pipeline_test.py [depths, default 1,4,8,16] [rounds, default 5]
"""
from __future__ import annotations

import statistics
import sys
import time

from runit_link import PACKETS, STREAM_INTERFACE, open_link, pack_fields

fails = 0


def check(name: str, cond: bool, detail: str = "") -> None:
    global fails
    print(f"{'PASS' if cond else 'FAIL'}  {name}" + (f"   ({detail})" if detail else ""))
    if not cond:
        fails += 1


def answers(link, since: int) -> list[tuple[float, int, int]]:
    """(time, seq, status) of every interface response received after frame index `since`."""
    return [(f.t, f.body[0], f.body[3]) for f in link.frames[since:] if f.stream == STREAM_INTERFACE and len(f.body) >= 4]


def burst(link, seqs: list[int], body: bytes, timeout: float = 5.0) -> tuple[dict[int, list[float]], list[float]]:
    """Write one frame per seq without waiting; return answer times per seq and send times."""
    since = len(link.frames)
    sent = []
    for seq in seqs:
        sent.append(time.time() - link._t0)
        link.send_raw(bytes([seq]) + body)
    end = time.time() + timeout
    while time.time() < end and len(answers(link, since)) < len(seqs):
        time.sleep(0.01)
    got: dict[int, list[float]] = {}
    for t, seq, status in answers(link, since):
        got.setdefault(seq, []).append(t)
        if status != 0:
            got.setdefault(-1, []).append(seq)  # a failed answer
    return got, sent


def main() -> int:
    depths = [int(x) for x in sys.argv[1].split(",")] if len(sys.argv) > 1 else [1, 4, 8, 16]
    rounds = int(sys.argv[2]) if len(sys.argv) > 2 else 5
    desc = PACKETS["packet_sys_power_get_status_t"]
    body = bytes([desc["class"], int(desc["packet_header"], 16)]) + pack_fields(desc, {})
    with open_link("BLE") as link:
        print(f"BLE connected, MTU {link.mtu}")
        time.sleep(0.5)

        got, _ = burst(link, [0, 255], body)
        check("seq 0 and 255 echoed", sorted(k for k in got if k >= 0) == [0, 255], str(sorted(got)))

        seq = 1
        for depth in depths:
            rtts, lost, dup, bad, order_ok = [], 0, 0, 0, True
            t0 = time.time()
            for _ in range(rounds):
                seqs = [((seq + i - 1) % 254) + 1 for i in range(depth)]  # 1..254, 0 / 255 tested above
                seq += depth
                got, sent = burst(link, seqs, body)
                bad += len(got.pop(-1, []))
                arrival = sorted((ts[0], s) for s, ts in got.items())
                order_ok &= [s for _, s in arrival] == [s for s in seqs if s in got]
                for s, t_sent in zip(seqs, sent):
                    if s not in got:
                        lost += 1
                    else:
                        dup += len(got[s]) - 1
                        rtts.append((got[s][0] - t_sent) * 1000)
            total = depth * rounds
            rate = total / (time.time() - t0)
            detail = f"{total} cmds, lost {lost}, dup {dup}, failed {bad}, rtt median {statistics.median(rtts):.0f} ms max {max(rtts):.0f} ms, {rate:.0f} cmd/s" if rtts else f"lost {lost}"
            check(f"depth {depth:2}: all answered once, in order", lost == 0 and dup == 0 and bad == 0 and order_ok, detail)
        crash = [l for _, l in link.logs if "Guru" in l or "abort" in l.lower() or "overflow" in l.lower()]
        check("no crash / overflow on the log stream", not crash, str(crash[:3]))
    print(f"\n{fails} failure(s)")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
