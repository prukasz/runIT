"""Tree test: two boards on a cable, the host on the root's console.

  python tree_tests.py --port COM9 [--child 1]

The root is built with sdkconfig.link_root.defaults (the runIT PCB), the child with sdkconfig.link_child.defaults (the
devkit) and the two are joined as build.md "Two boards on a cable" says. The host opens a client session on the root
only: everything for the child goes through the root (a frame with the child's id as its destination).

Checks the session layer (SYS_SESSION.MD): the link's sessions on both boards, a command to the child and its answer, a
board that is not in the tree, the root's clock on the child, a watch on the child whose entries come back through the
root, and the child's loss (its session ends, a command to it fails).
"""

from __future__ import annotations

import argparse
import struct
import sys
import time

from router_link import KEY_SESSION, KEY_SYS, READ, WRITE, Link, Response, frame

RESULTS: list[tuple[str, bool, str]] = []


def check(name: str, ok: bool, detail: str = "") -> bool:
    RESULTS.append((name, ok, detail))
    print(f"{'PASS' if ok else 'FAIL'}  {name}" + (f"  -- {detail}" if detail and not ok else ""))
    return ok


SYS_PING, SYS_TIME = 1, 7
SESSION_ID, SESSION_PEERS = 1, 4
KEY_WATCH, WATCH_ADD, WATCH_REMOVE, WATCH_LIST = 0x0F, 1, 2, 3
WATCHES = 5  # the byte of the watches' entries in a stream frame
PEER_PARENT, PEER_CHILD, PEER_CLIENT = 0, 1, 2
PEER = struct.Struct("<BIBBBBHB")  # provider link, channel, peer, access, open, peer id, timeout, subtree


def cause(r: Response | None) -> str:
    return r.record.cause.name if r and r.record and r.record.cause else ""


def peers(link: Link, dst: int | None = None) -> list[tuple]:
    r = link.request(bytes([KEY_SESSION, SESSION_PEERS]), dst=dst)
    if r is None or not r.ok:
        return []
    return [PEER.unpack_from(r.data, 1 + PEER.size * n) for n in range(r.data[0])]


def uptime(link: Link, dst: int | None = None) -> int | None:
    r = link.request(bytes([KEY_SYS, SYS_PING]), dst=dst)
    return struct.unpack("<I", r.data[:4])[0] if r is not None and r.ok and len(r.data) >= 4 else None


def board_id(link: Link, dst: int | None = None) -> int | None:
    r = link.request(bytes([KEY_SESSION, SESSION_ID]), dir=READ, dst=dst)
    return r.data[0] if r is not None and r.ok and r.data else None


def watch_head(wid: int, period: int) -> bytes:
    # id, connector (0: to the owner), trigger (0: every period), period, max silence, filter (unused)
    return struct.pack("<BBBHHHHB", wid, 0, 0, period, 0, 0xFFFF, 0xFFFF, 0xFF)


def test_tree(link: Link, child: int) -> None:
    mine = peers(link)
    links = [p for p in mine if p[2] == PEER_CHILD]
    check("root: the link is a child session, open, with the child's id",
          len(links) == 1 and links[0][4] == 1 and links[0][5] == child, str(mine))
    check("root: the host's session is a client", any(p[2] == PEER_CLIENT and p[4] == 1 for p in mine), str(mine))

    check("root answers a ping", uptime(link) is not None)
    up = uptime(link, child)
    check("the child answers a ping through the root", up is not None, "no answer: is the cable joined, are the ids different?")
    check("the child has the board id the host expects", board_id(link, child) == child)

    theirs = peers(link, child)
    check("child: its one link is a parent session, open, with the root's id",
          len(theirs) == 1 and theirs[0][2] == PEER_PARENT and theirs[0][4] == 1 and theirs[0][5] == link.board_id,
          str(theirs))

    r = link.request(bytes([KEY_SYS, SYS_PING]), dst=child + 7)
    check("a board that is not in the tree is refused", r is not None and not r.ok and cause(r) == "ERR_SESSION_NO_ROUTE",
          str(r))


def test_time(link: Link, child: int) -> None:
    now_ms = int(time.time() * 1000)
    r = link.request(bytes([KEY_SYS, SYS_TIME]), payload=struct.pack("<Q", now_ms), dir=WRITE)
    check("the host sets the root's time", r is not None and r.ok, str(r))
    time.sleep(0.5)  # the root sends it on to every board
    got = link.request(bytes([KEY_SYS, SYS_TIME]), dir=READ, dst=child)
    unix = struct.unpack("<Q", got.data[:8])[0] if got is not None and got.ok and len(got.data) >= 8 else 0
    check("the child has the root's time", abs(unix - now_ms) < 5000, f"child {unix}, root {now_ms}")


def test_watch(link: Link, child: int) -> None:
    wid = 9
    read = frame(bytes([KEY_SYS, SYS_PING]), READ)
    before = len(link.streams)
    r = link.request(bytes([KEY_WATCH, WATCH_ADD]), payload=watch_head(wid, 10) + read, dir=WRITE, dst=child)
    check("a watch is added on the child", r is not None and r.ok, str(r))
    time.sleep(1.5)
    entries = [(t, b) for (t, s, b) in link.streams[before:] if s == WATCHES and len(b) >= 3 and b[1] == wid]
    check("its entries come back through the root (about one per 100 ms)", len(entries) >= 5, f"{len(entries)} entries")
    check("each entry is the owner's, OK, with a ping's uptime",
          bool(entries) and all(b[0] == link.my_id and b[2] == 0 and len(b) == 3 + 4 for _, b in entries),
          str(entries[:1]))
    listed = link.request(bytes([KEY_WATCH, WATCH_LIST]), dir=READ, dst=child)
    check("the child lists the watch", listed is not None and listed.ok and listed.data[:1] == b"\x01", str(listed))
    r = link.request(bytes([KEY_WATCH, WATCH_REMOVE]), payload=bytes([wid]), dir=WRITE, dst=child)
    check("the watch is removed", r is not None and r.ok, str(r))
    time.sleep(0.3)
    seen = len(link.streams)
    time.sleep(0.5)
    check("no entries after the removal", not any(s == WATCHES for (_, s, _) in link.streams[seen:]))


def test_loss(link: Link, child: int, wait_s: float) -> None:
    print(f"      Unplug the cable (or reset the child) within {wait_s:.0f} s ...")
    end = time.time() + wait_s
    gone = False
    while time.time() < end and not gone:
        gone = not any(p[2] == PEER_CHILD and p[4] == 1 for p in peers(link))
        time.sleep(0.3)
    if not gone:
        print("SKIP  the child's loss  -- the link stayed up")
        return
    check("root: the child's session ended", True)
    r = link.request(bytes([KEY_SYS, SYS_PING]), dst=child)
    check("a command to the lost child is refused", r is not None and not r.ok and cause(r) == "ERR_SESSION_NO_ROUTE", str(r))


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", required=True, help="the root's console, e.g. COM9")
    ap.add_argument("--child", type=int, default=1, help="the child's board id (sdkconfig.link_child.defaults: 1)")
    ap.add_argument("--loss", type=float, default=0.0, help="also test the child's loss: seconds to unplug the cable")
    ap.add_argument("--echo", action="store_true")
    a = ap.parse_args()
    with Link(a.port, echo=a.echo) as link:
        if not link.wait_open(10.0):
            print("no session on the root")
            return 2
        test_tree(link, a.child)
        test_time(link, a.child)
        test_watch(link, a.child)
        if a.loss > 0:
            test_loss(link, a.child, a.loss)
    failed = [n for (n, ok, _) in RESULTS if not ok]
    print(f"\n{len(RESULTS) - len(failed)}/{len(RESULTS)} passed")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
