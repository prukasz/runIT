"""Remote devices: a device of the child, driven from the root over the cable.

  python remote_tests.py --port COM9 [--child 1] [--pin 4] [--blink 6] [--keep]

Same two boards as tree_tests.py (build.md "Two boards on a cable"). The host is on the root's console only. The
child's GPIO device (its device 0) gets a remote twin on the root; the checks drive the child's pin through it, with the
child's own answers to the host as the truth (a command with the child as its destination, forwarded by the root).

  --blink N   also runs a program on the root (a PERIODIC of 1 s and a TOGGLE of the remote pin) and counts the pin's
              changes on the child for N seconds
  --keep      leaves that program running (the pin blinks until the root is reset)

Hardware touched: pin PIN of the child, as a push-pull output. Nothing is expected on it (an LED with a resistor may be).
"""

from __future__ import annotations

import argparse
import struct
import sys
import time

from router_link import FLAG_NOREPLY, KEY_CONN, KEY_DEVICE, READ, STREAM_EVENTS, WRITE, Link, Response, frame
from vm_lib import Prog, Vm, X_NORMAL, X_RESET, ok as vm_ok
from vm_t_blocks import periodic_state
from vm_t_hw import IO_LEVEL, IO_MODE, IO_RESET, IO_TOGGLE, dev_state

RESULTS: list[tuple[str, bool, str]] = []

SYS_DEVICE_OP_CREATE_REMOTE, SYS_DEVICE_OP_REMOVE, SYS_DEVICE_OP_INFO = 0x0008, 0x0004, 0x0005
SYS_DEVICE_OP_SUSPEND, SYS_DEVICE_OP_RESUME, DEVICE_ALL = 0x0002, 0x0003, 0xFFFF
EV_DEVICE_SUSPENDED = 0x0000  # the DEVICE contract's event 0
ESP32S3_GPIO = 0x0040
ASYNC, BLOCKING = 0, 1
LOCAL = 0x0030  # the remote twin's id on the root
CONN_CREATE, CONN_REMOVE, CONN_READ, CONN_MIRROR = 0x01, 0x02, 0x08, 0x0B
EVENTS_ALL = 0x07  # the child's connector of every event
RX_LATEST, RX_FIFO = 1, 2
MSG = struct.Struct("<IHHBBI")  # READ's head: seq, len, missed, cut, from link, from channel


def check(name: str, ok: bool, detail: str = "") -> bool:
    RESULTS.append((name, ok, detail))
    print(f"{'PASS' if ok else 'FAIL'}  {name}" + (f"  -- {detail}" if detail and not ok else ""))
    return ok


def cause(r: Response | None) -> str:
    return r.record.cause.name if r and r.record and r.record.cause else ""


def chain(r: Response | None) -> list[str]:
    return [n.name for n in r.record.nodes] if r and r.record else []


def dev(link: Link, dev_id: int, op: int, payload: bytes = b"", dir: int | None = None, dst: int | None = None,
        timeout: float = 2.0) -> Response | None:
    return link.request(bytes([KEY_DEVICE]) + struct.pack("<HH", dev_id, op), payload, dir=dir, dst=dst, timeout=timeout)


def level(link: Link, pin: int, dst: int | None, dev_id: int = 0) -> int | None:
    r = dev(link, dev_id, IO_LEVEL, bytes([pin]), dir=READ, dst=dst)
    return r.data[1] if r is not None and r.ok and len(r.data) >= 2 else None


def release(link: Link, pin: int, child: int) -> None:
    """The pin goes back to the board (a mode is refused on a pin that is in use)."""
    dev(link, 0, IO_RESET, bytes([pin]), dir=WRITE, dst=child)


def create_remote(link: Link, child: int, mode: int, max_age: int) -> Response | None:
    return dev(link, LOCAL, SYS_DEVICE_OP_CREATE_REMOTE, struct.pack("<HBHBH", ESP32S3_GPIO, child, 0, mode, max_age),
               dir=WRITE)


def clean(link: Link, child: int) -> None:
    """A program of an earlier run (--keep) and its twin would be in the way; so would the child's safe state (the root's
    STOP when a host is gone, e.g. after router_tests.py's dead-man test, suspends its devices)."""
    dev(link, DEVICE_ALL, SYS_DEVICE_OP_RESUME, dir=WRITE, dst=child)
    vm = Vm(link)
    vm.exec(X_RESET)
    vm.reset()
    dev(link, LOCAL, SYS_DEVICE_OP_REMOVE, dir=WRITE)


def test_remote(link: Link, child: int, pin: int) -> None:
    info = dev(link, 0, SYS_DEVICE_OP_INFO, dir=READ, dst=child)
    check("the child has a GPIO device 0", info is not None and info.ok and info.data[:2] == struct.pack("<H", ESP32S3_GPIO),
          str(info))
    release(link, pin, child)
    r = dev(link, 0, IO_MODE, bytes([pin, 3]), dir=WRITE, dst=child)
    check(f"the child's pin {pin} is a push-pull output", r is not None and r.ok, str(r))
    dev(link, 0, IO_LEVEL, bytes([pin, 0]), dir=WRITE, dst=child)

    r = create_remote(link, child, ASYNC, 0)
    check("an async remote device needs a max age (0 is refused)", r is not None and not r.ok and "ERR_DEV_BAD_ARG" in chain(r),
          str(chain(r)))
    r = create_remote(link, child, ASYNC, 100)
    check("the root makes the child's GPIO a device of its own (async, 100 ms)", r is not None and r.ok, str(r))
    r = create_remote(link, child, ASYNC, 100)
    check("the same device twice is refused", r is not None and not r.ok, str(r))
    info = dev(link, LOCAL, SYS_DEVICE_OP_INFO, dir=READ)
    check("its INFO is the type's", info is not None and info.ok and info.data[:2] == struct.pack("<H", ESP32S3_GPIO),
          str(info))

    r = dev(link, LOCAL, IO_LEVEL, bytes([pin, 1]), dir=WRITE)
    check("a write through the twin is taken", r is not None and r.ok, str(r))
    time.sleep(0.3)
    check("... and the child's pin is high", level(link, pin, child) == 1, str(level(link, pin, child)))
    dev(link, LOCAL, IO_LEVEL, bytes([pin, 0]), dir=WRITE)
    time.sleep(0.3)
    check("... and low again", level(link, pin, child) == 0)
    dev(link, LOCAL, IO_TOGGLE, bytes([pin]), dir=WRITE)
    time.sleep(0.3)
    check("TOGGLE through the twin flips it", level(link, pin, child) == 1)
    dev(link, LOCAL, IO_TOGGLE, bytes([pin]), dir=WRITE)
    time.sleep(0.3)

    r = dev(link, LOCAL, 0x0DEF, b"", dir=WRITE)
    check("an op the type does not have is refused on the root", r is not None and not r.ok and "ERR_DEV_UNKNOWN_OP" in chain(r),
          str(chain(r)))

    # reads: a copy kept by a watch on the child
    first = dev(link, LOCAL, IO_LEVEL, bytes([pin]), dir=READ)
    check("the first read of an async twin has no copy yet, or the watch's first one",
          first is not None and (first.ok or "ERR_WATCH_NO_VALUE" in chain(first)), str(chain(first)))
    time.sleep(0.4)
    r = dev(link, LOCAL, IO_LEVEL, bytes([pin]), dir=READ)
    check("then a read answers the pin's level from the copy", r is not None and r.ok and r.data[:2] == bytes([pin, 0]),
          str(r))
    dev(link, 0, IO_LEVEL, bytes([pin, 1]), dir=WRITE, dst=child)  # someone else drives the pin
    time.sleep(0.4)
    r = dev(link, LOCAL, IO_LEVEL, bytes([pin]), dir=READ)
    check("the copy follows a change made on the child (within a few of its max ages)",
          r is not None and r.ok and r.data[:2] == bytes([pin, 1]), str(r))
    dev(link, LOCAL, IO_LEVEL, bytes([pin, 0]), dir=WRITE)
    time.sleep(0.05)
    r = dev(link, LOCAL, IO_LEVEL, bytes([pin]), dir=READ)
    check("a read just after a write is not the old copy", r is None or not r.ok or r.data[:2] == bytes([pin, 0]),
          str(r))
    time.sleep(0.4)
    r = link.request(bytes([0x0F, 3]), dir=READ, dst=child)
    check("the child runs a watch for the twin's read", r is not None and r.ok and r.data[:1] != b"\x00", str(r))

    r = dev(link, LOCAL, SYS_DEVICE_OP_CREATE_REMOTE, struct.pack("<HBHBH", ESP32S3_GPIO, child, 7, ASYNC, 100), dir=WRITE)
    check("CREATE_REMOTE of an id that exists is refused", r is not None and not r.ok and "ERR_DEV_ID_TAKEN" in chain(r),
          str(chain(r)))
    r = dev(link, LOCAL, SYS_DEVICE_OP_REMOVE, dir=WRITE)
    check("the twin is removed", r is not None and r.ok, str(r))
    time.sleep(1.2)
    r = link.request(bytes([0x0F, 3]), dir=READ, dst=child)
    check("... and its watch on the child ends with the idle floor", r is not None and r.ok and r.data[:1] == b"\x00",
          str(r.data[:1] if r is not None else None))

    # blocking: a wired neighbour waits for the answer
    r = create_remote(link, child, BLOCKING, 0)
    check("a blocking twin of a board on the cable is made", r is not None and r.ok, str(r))
    r = dev(link, LOCAL, IO_LEVEL, bytes([pin, 1]), dir=WRITE)
    check("a blocking write returns once the child ran it", r is not None and r.ok and level(link, pin, child) == 1, str(r))
    r = dev(link, LOCAL, IO_LEVEL, bytes([pin]), dir=READ)
    check("a blocking read asks now (max age 0)", r is not None and r.ok and r.data[:2] == bytes([pin, 1]), str(r))
    dev(link, LOCAL, IO_LEVEL, bytes([pin, 0]), dir=WRITE)
    r = dev(link, LOCAL, SYS_DEVICE_OP_REMOVE, dir=WRITE)
    check("... and is removed", r is not None and r.ok, str(r))
    release(link, pin, child)


def test_events_errors(link: Link, child: int) -> None:
    """The child's events and errors about its device 0 are the root's about the twin."""
    r = create_remote(link, child, ASYNC, 100)
    if not check("the twin for the events is made", r is not None and r.ok, str(r)):
        return
    time.sleep(0.3)  # its mirrors subscribe on the child
    t = link.mark()
    dev(link, 0, SYS_DEVICE_OP_SUSPEND, dir=WRITE, dst=child)
    time.sleep(0.5)
    events = [p for p in link.stream_since(STREAM_EVENTS, t) if len(p) >= 2 + 18]
    mine = [struct.unpack_from("<HH", p, 2) for p in events]
    check("the child's SUSPENDED event comes to the root as the twin's", (LOCAL, EV_DEVICE_SUSPENDED) in mine, str(mine))
    dev(link, DEVICE_ALL, SYS_DEVICE_OP_RESUME, dir=WRITE, dst=child)

    t = link.mark()
    link.send_raw(link.next_seq(), FLAG_NOREPLY, frame(bytes([KEY_DEVICE]) + struct.pack("<HH", 0, 0x0DEF), WRITE), child)
    time.sleep(0.6)
    recs = link.error_records_since(t)
    names = [[n.name for n in rec.nodes] for rec in recs]
    check("an error of a command on the child's device 0 comes to the root as the twin's (ERR_DEV_ID over ERR_REMOTE)",
          any(n[:2] == ["ERR_DEV_ID", "ERR_REMOTE"] and "ERR_DEV_UNKNOWN_OP" in n for n in names), str(names))
    r = dev(link, LOCAL, SYS_DEVICE_OP_REMOVE, dir=WRITE)
    check("the twin is removed", r is not None and r.ok, str(r))


def conn(link: Link, op: int, payload: bytes, dir: int | None = None) -> Response | None:
    return link.request(bytes([KEY_CONN, op]), payload, dir=dir)


def conn_read(link: Link, cid: int) -> dict | None:
    r = conn(link, CONN_READ, bytes([cid]), dir=READ)
    if r is None or not r.ok or len(r.data) < MSG.size:
        return None
    seq, length, missed, cut, lk, ch = MSG.unpack_from(r.data)
    return {"seq": seq, "len": length, "missed": missed, "cut": cut, "from": (lk, ch), "data": bytes(r.data[MSG.size:])}


def event_of(msg: dict | None) -> tuple[int, int] | None:
    """(device, event) of an EVENTS_ALL frame kept by a connector: [event key][NOTIFY][sys_event_t]."""
    return struct.unpack_from("<HH", msg["data"], 2) if msg and len(msg["data"]) >= 2 + 18 else None


def suspend_resume(link: Link, child: int, times: int = 1) -> None:
    """Two events of the child's device 0 each: SUSPENDED, then RESUMED."""
    for _ in range(times):
        dev(link, 0, SYS_DEVICE_OP_SUSPEND, dir=WRITE, dst=child)
        dev(link, 0, SYS_DEVICE_OP_RESUME, dir=WRITE, dst=child)
    time.sleep(0.5)


def bad_op(link: Link, child: int) -> None:
    """An error on the child (an op its device 0 does not have): an errors record, and a line in its log."""
    link.send_raw(link.next_seq(), FLAG_NOREPLY, frame(bytes([KEY_DEVICE]) + struct.pack("<HH", 0, 0x0DEF), WRITE), child)
    time.sleep(0.5)


def conn_make(link: Link, cid: int, rx: int, size: int, child: int, remote: int) -> bool:
    r = conn(link, CONN_CREATE, struct.pack("<BBBH", cid, rx, 0, size), dir=WRITE)
    r2 = conn(link, CONN_MIRROR, bytes([cid, child, remote]), dir=WRITE)
    return check(f"connector 0x{cid:02x} keeps stream 0x{remote:02x} of the child", r is not None and r.ok and r2 is not None and r2.ok, f"{r} {r2}")


def conn_drop(link: Link, cid: int, child: int) -> None:
    conn(link, CONN_MIRROR, bytes([cid, child, 0]), dir=WRITE)
    conn(link, CONN_REMOVE, bytes([cid]), dir=WRITE)


def test_connector_rx(link: Link, child: int) -> None:
    """Mirrors of the child's streams, kept by the root's connectors: the latest, a queue in order, a full queue that
    loses the new ones, a second mirror of one stream refused, and a pool that closes over a removed connector's buffer
    without losing what the others hold."""
    for cid in (0x40, 0x41, 0x42):
        conn_drop(link, cid, child)
    LOGS, ERRORS = 0x03, 0x04

    # a queue: every event in order, then a full one
    if not conn_make(link, 0x42, RX_FIFO, 200, child, EVENTS_ALL):
        return
    r = conn(link, CONN_CREATE, struct.pack("<BBBH", 0x41, RX_LATEST, 0, 64), dir=WRITE)
    r = conn(link, CONN_MIRROR, bytes([0x41, child, EVENTS_ALL]), dir=WRITE)
    check("a second mirror of the same stream is refused (the users of one share it)", cause(r) == "ERR_CONN_BAD_ARG", str(r))
    conn(link, CONN_REMOVE, bytes([0x41]), dir=WRITE)
    time.sleep(0.3)  # its mirror subscribes on the child
    suspend_resume(link, child)
    got = []
    while (m := conn_read(link, 0x42)) is not None and m["len"] > 0:
        got.append(m)
    events = [event_of(m) for m in got]
    check("queue: every event, the oldest first (SUSPENDED, then the one after it), then empty",
          len(got) >= 2 and events[0] == (0, EV_DEVICE_SUSPENDED) and all(g["seq"] < h["seq"] for g, h in zip(got, got[1:])), str(events))
    suspend_resume(link, child, 8)  # 16 events: more than 200 bytes of the queue hold
    first = conn_read(link, 0x42)
    check("queue: a full queue drops the new ones and counts them (missed)", first is not None and first["len"] > 0 and first["missed"] > 0, str(first))
    conn_drop(link, 0x42, child)

    # the latest of three streams, and the pool closing over the first one's buffer
    for cid, rx, size, remote in ((0x40, RX_LATEST, 64, EVENTS_ALL), (0x41, RX_LATEST, 128, ERRORS), (0x42, RX_FIFO, 300, LOGS)):
        if not conn_make(link, cid, rx, size, child, remote):
            return
    time.sleep(0.3)
    suspend_resume(link, child)
    bad_op(link, child)
    a, b = conn_read(link, 0x40), conn_read(link, 0x41)
    check("latest: the child's last event is kept (device 0, from the child's board)",
          a is not None and a["seq"] >= 2 and event_of(a) is not None and event_of(a)[0] == 0 and a["from"] == (0, child), str(a))
    again = conn_read(link, 0x40)
    check("latest: a read leaves it (the same count, the same bytes)", again is not None and a is not None and
          (again["seq"], again["data"]) == (a["seq"], a["data"]), f"{a} {again}")
    check("latest: another stream's connector keeps its own (the error record of the bad op)",
          b is not None and b["len"] > 0 and b["data"] != a["data"] and b["from"] == (0, child), str(b))
    lines = conn_read(link, 0x42)
    check("queue: the child's log lines arrive too", lines is not None and lines["len"] > 0, str(lines))

    conn_drop(link, 0x40, child)
    after = conn_read(link, 0x41)
    check("pool: the connectors after a removed one keep what they had", b is not None and after is not None and
          (b["seq"], b["data"]) == (after["seq"], after["data"]), f"{b} {after}")
    bad_op(link, child)
    newer, text = conn_read(link, 0x41), conn_read(link, 0x42)
    check("pool: and keep receiving, with their buffers moved", newer is not None and b is not None and newer["seq"] > b["seq"] and
          text is not None and text["len"] > 0, f"{newer} {text}")
    for cid in (0x41, 0x42):
        conn_drop(link, cid, child)


DUMMY_ERRORS = [  # (name, device, op, dir, payload, the error the child's device raises)
    ("an op the device does not have", 0, 0x0DEF, WRITE, b"", "ERR_DEV_UNKNOWN_OP"),
    ("an argument of the wrong length", 0, IO_LEVEL, WRITE, b"\x04", "ERR_CMD_BAD_LEN"),
    ("a pin that does not exist", 0, IO_LEVEL, WRITE, bytes([200, 1]), None),
    ("a command to a suspended device", 0, IO_LEVEL, WRITE, bytes([4, 1]), "ERR_DEV_SUSPENDED"),
]


def test_dummy_errors(link: Link, child: int) -> None:
    """Errors raised on the child's device 0 by commands that do nothing (nothing is wired to it): each comes to the
    root as the twin's, and the child's own policy sees it first."""
    r = create_remote(link, child, ASYNC, 100)
    if not check("the twin for the errors is made", r is not None and r.ok, str(r)):
        return
    time.sleep(0.3)
    for name, device, op, d, payload, tag in DUMMY_ERRORS:
        if tag == "ERR_DEV_SUSPENDED":
            dev(link, device, SYS_DEVICE_OP_SUSPEND, dir=WRITE, dst=child)
        t = link.mark()
        link.send_raw(link.next_seq(), FLAG_NOREPLY,
                      frame(bytes([KEY_DEVICE]) + struct.pack("<HH", device, op), d) + payload, child)
        time.sleep(0.5)
        names = [[n.name for n in rec.nodes] for rec in link.error_records_since(t)]
        mine = [n for n in names if n[:2] == ["ERR_DEV_ID", "ERR_REMOTE"]]
        check(f"{name}: the root reports it as the twin's" + (f" ({tag})" if tag else ""),
              bool(mine) and (tag is None or any(tag in n for n in mine)), str(names))
        if tag == "ERR_DEV_SUSPENDED":
            dev(link, DEVICE_ALL, SYS_DEVICE_OP_RESUME, dir=WRITE, dst=child)
    r = dev(link, LOCAL, SYS_DEVICE_OP_REMOVE, dir=WRITE)
    check("the twin is removed", r is not None and r.ok, str(r))


def test_board_errors(link: Link) -> None:
    """The board as the source of an error: a board the root has no slot to follow (CONFIG_SYS_REMOTE_BOARDS is 4)."""
    boards = [21, 22, 23, 24, 25]  # boards that do not exist: the twins are only records
    t = link.mark()
    made = []
    for i, board in enumerate(boards):
        r = dev(link, 0x40 + i, SYS_DEVICE_OP_CREATE_REMOTE, struct.pack("<HBHBH", ESP32S3_GPIO, board, 0, ASYNC, 100),
                dir=WRITE)
        made.append(r is not None and r.ok)
    check("five twins on five boards are made", all(made), str(made))
    time.sleep(0.5)
    recs = [rec for rec in link.error_records_since(t) if rec.src == 2]
    check("the fifth board has no slot for its mirrors: an error about the board (source BOARD, board 25)",
          any(rec.sender == 25 and "ERR_CONN_FULL" in [n.name for n in rec.nodes] for rec in recs),
          str([(rec.sender, [n.name for n in rec.nodes]) for rec in recs]))
    for i in range(len(boards)):
        dev(link, 0x40 + i, SYS_DEVICE_OP_REMOVE, dir=WRITE)


def test_blink(link: Link, child: int, pin: int, seconds: float, keep: bool) -> None:
    vm = Vm(link)
    release(link, pin, child)  # the program makes the pin an output itself, through the twin
    r = create_remote(link, child, ASYNC, 100)
    if not check("the twin for the program is made", r is not None and r.ok, str(r)):
        return
    p = Prog()
    tick = p.b()
    p.block("IO", state=dev_state(LOCAL, IO_MODE, k=(pin, 3)))  # on the first pass: the pin is an output
    p.block("PERIODIC", eno=tick, state=periodic_state(1, unit=1))  # a tick every second
    p.block("IO", ens=[tick], state=dev_state(LOCAL, IO_TOGGLE, k=(pin,)))
    r = vm.start(p, X_NORMAL)
    if not check("the program (PERIODIC 1 s -> TOGGLE of the child's pin) runs on the root", vm_ok(r), str(r)):
        return
    print(f"      The pin {pin} of the board {child} blinks every second: counting its changes for {seconds:.0f} s ...")
    changes, last = 0, level(link, pin, child)
    end = time.time() + seconds
    while time.time() < end:
        now = level(link, pin, child)
        if now is not None and last is not None and now != last:
            changes += 1
        last = now if now is not None else last
        time.sleep(0.1)
    check(f"the child's pin changed {changes} times in {seconds:.0f} s (about one per second)",
          seconds - 2 <= changes <= seconds + 1, str(changes))
    if not keep:
        vm.exec(X_RESET)
        vm.reset()
        dev(link, LOCAL, SYS_DEVICE_OP_REMOVE, dir=WRITE)
        release(link, pin, child)


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", required=True, help="the root's console, e.g. COM9")
    ap.add_argument("--child", type=int, default=1)
    ap.add_argument("--pin", type=int, default=4, help="a free pin of the child")
    ap.add_argument("--blink", type=float, default=0.0, help="also run the blink program for this many seconds")
    ap.add_argument("--keep", action="store_true", help="leave the blink program running")
    ap.add_argument("--echo", action="store_true")
    a = ap.parse_args()
    with Link(a.port, echo=a.echo) as link:
        if not link.wait_open(10.0):
            print("no session on the root")
            return 2
        clean(link, a.child)
        if a.blink <= 0 or not a.keep:
            test_remote(link, a.child, a.pin)
            test_events_errors(link, a.child)
            test_connector_rx(link, a.child)
            test_dummy_errors(link, a.child)
            test_board_errors(link)
        if a.blink > 0:
            test_blink(link, a.child, a.pin, a.blink, a.keep)
    failed = [n for (n, ok, _) in RESULTS if not ok]
    print(f"\n{len(RESULTS) - len(failed)}/{len(RESULTS)} passed")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
