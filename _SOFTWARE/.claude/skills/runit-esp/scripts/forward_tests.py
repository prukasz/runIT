"""Forwarding from the root to the child: a connector's rx binding and FORWARD, on two boards.

  .venv\\Scripts\\python.exe forward_tests.py --port COM9 --ble 94:A9:90:DB:32:3E [--child 1]

Same two boards as tree_tests.py (build.md "Two boards on a cable"), and the host also has Bluetooth (bleak). On the root:
a user service 0xEE00 with a characteristic 0xEE01 the host writes (an rx provider) and 0xEE02 it is notified on (a tx
provider), and a connector 0x50 with FORWARD: rx on 0xEE01, tx on 0xEE02. On the child: a connector 0x51 (a queue) that
mirrors the root's 0x50. A message written to 0xEE01 is forwarded to every other target of 0x50: back to the host on
0xEE02, and over the cable to the child's queue. Needs --ble: both boards advertise "runit".
"""

from __future__ import annotations

import argparse
import asyncio
import struct
import sys
import time

from bleak import BleakClient, BleakScanner

from router_link import KEY_BLE, KEY_CONN, LINK_BLE, READ, WRITE, Link, Response
from router_tests import conn_list

RESULTS: list[tuple[str, bool, str]] = []

SVC, CHAR_IN, CHAR_OUT = 0xEE00, 0xEE01, 0xEE02
ROOT_CONN, CHILD_CONN = 0x50, 0x51
RX_NONE, RX_FIFO = 0, 2
FLAG_FORWARD = 0x02
BIND_RX, BIND_TX = 0x01, 0x02
MSG = struct.Struct("<IHHBBI")  # READ's head: seq, len, missed, cut, from link, from channel


def check(name: str, ok: bool, detail: str = "") -> bool:
    RESULTS.append((name, ok, detail))
    print(f"{'PASS' if ok else 'FAIL'}  {name}" + (f"  -- {detail}" if detail and not ok else ""))
    return ok


def uuid16(u: int) -> str:
    return f"0000{u:04x}-0000-1000-8000-00805f9b34fb"


def provider(uuid: int) -> bytes:
    return struct.pack("<BI", LINK_BLE, uuid)


def req(link: Link, key: int, op: int, payload: bytes = b"", dir: int | None = None, dst: int | None = None) -> Response | None:
    return link.request(bytes([key, op]), payload, dir=dir, dst=dst)


def read_child(link: Link, child: int) -> dict | None:
    r = req(link, KEY_CONN, 0x08, bytes([CHILD_CONN]), dir=READ, dst=child)
    if r is None or not r.ok or len(r.data) < MSG.size:
        return None
    seq, length, missed, cut, lk, ch = MSG.unpack_from(r.data)
    return {"seq": seq, "len": length, "missed": missed, "cut": cut, "from": (lk, ch), "data": bytes(r.data[MSG.size:])}


def clean(link: Link, child: int) -> None:
    """What an earlier run left: the connectors, the mirror and the service."""
    req(link, KEY_CONN, 0x0B, bytes([CHILD_CONN, 0, 0]), WRITE, dst=child)
    req(link, KEY_CONN, 0x02, bytes([CHILD_CONN]), WRITE, dst=child)
    req(link, KEY_CONN, 0x02, bytes([ROOT_CONN]), WRITE)
    req(link, KEY_BLE, 0x02, struct.pack("<H", SVC), WRITE)
    req(link, KEY_BLE, 0x05, b"", WRITE)


def ok(r: Response | None) -> bool:
    return r is not None and r.ok


async def main_async(a: argparse.Namespace) -> None:
    with Link(a.port) as link:
        if not link.wait_open(10.0):
            print("no session on the root")
            return
        clean(link, a.child)
        await asyncio.sleep(0.5)

        # The root: the service, the two characteristics (unframed: each write is one message), the connector.
        r = [
            req(link, KEY_BLE, 0x01, struct.pack("<HB", SVC, 1), WRITE),
            req(link, KEY_BLE, 0x03, struct.pack("<HHBBBBII", SVC, CHAR_IN, 1, 0, 0, 0, 0, 0) + b"in\0", WRITE),
            req(link, KEY_BLE, 0x03, struct.pack("<HHBBBBII", SVC, CHAR_OUT, 0, 0, 1, 0, 96, 0) + b"out\0", WRITE),
            req(link, KEY_BLE, 0x05, b"", WRITE),
        ]
        check("the root has a service with a characteristic to write and one to be notified on", all(ok(x) for x in r), str(r))
        await asyncio.sleep(0.5)
        r = [
            req(link, KEY_CONN, 0x01, struct.pack("<BBBH", ROOT_CONN, RX_NONE, FLAG_FORWARD, 0), WRITE),
            req(link, KEY_CONN, 0x03, bytes([ROOT_CONN]) + provider(CHAR_IN) + bytes([BIND_RX]), WRITE),
            req(link, KEY_CONN, 0x03, bytes([ROOT_CONN]) + provider(CHAR_OUT) + bytes([BIND_TX]), WRITE),
        ]
        check("connector 0x50 forwards: rx on the written characteristic, tx on the notified one", all(ok(x) for x in r), str(r))

        # The child: a queue that mirrors it.
        r = [
            req(link, KEY_CONN, 0x01, struct.pack("<BBBH", CHILD_CONN, RX_FIFO, 0, 300), WRITE, dst=a.child),
            req(link, KEY_CONN, 0x0B, bytes([CHILD_CONN, 0, ROOT_CONN]), WRITE, dst=a.child),
        ]
        check("the child's connector 0x51 mirrors the root's 0x50 (a queue)", all(ok(x) for x in r), str(r))

        got: list[bytes] = []
        device = await BleakScanner.find_device_by_address(a.ble, timeout=20.0)  # WinRT connects to what it has seen
        if not check("the root is found over Bluetooth", device is not None):
            return
        async with BleakClient(device, timeout=20.0) as client:
            check("the host is connected to the root over Bluetooth", client.is_connected)
            await client.start_notify(uuid16(CHAR_OUT), lambda _c, data: got.append(bytes(data)))
            await asyncio.sleep(0.5)

            sent = [b"hello devkit", b"second", bytes(range(100)), b"after"]  # the third is past the 96 B of 0xEE02's buffer
            t = time.time()
            for m in sent:
                await client.write_gatt_char(uuid16(CHAR_IN), m, response=True)
                await asyncio.sleep(0.3)
            await asyncio.sleep(0.5)

            check("what is written comes back on the notified characteristic (forwarded to the other binding)",
                  got == [sent[0], sent[1], sent[3]], f"{[len(g) for g in got]}")
            binds = conn_list(link).get(ROOT_CONN, {}).get("binds", [])
            check("... a message too long for one target is lost there only (its dropped count), the next one goes on",
                  [b[2] for b in binds if b[0] == (LINK_BLE, CHAR_OUT)] == [1] and [b[2] for b in binds if b[0] == (LINK_BLE, CHAR_IN)] == [0], str(binds))
            queue = []
            while (m := read_child(link, a.child)) is not None and m["len"] > 0:
                queue.append(m)
            check("... and reaches the child over the cable, in order, from the root (board 0)",
                  [m["data"] for m in queue] == sent and all(m["from"] == (0, 0) for m in queue),
                  f"{[(m['len'], m['from']) for m in queue]}")
            r = req(link, KEY_CONN, 0x07, b"", READ)
            check("the root's LIST shows the connector (stream 0x50)", ok(r) and bytes([ROOT_CONN, RX_NONE, FLAG_FORWARD]) in bytes(r.data), str(r))

            # a message the host does not write is not forwarded
            before = len(got)
            await asyncio.sleep(1.0)
            check("nothing is forwarded that was not written", len(got) == before)

        await asyncio.sleep(0.5)
        clean(link, a.child)
        r = req(link, KEY_CONN, 0x07, b"", READ)
        check("cleaned up: the connector is gone", ok(r) and bytes([ROOT_CONN, RX_NONE, FLAG_FORWARD]) not in bytes(r.data), str(r))


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", required=True, help="the root's console, e.g. COM9")
    ap.add_argument("--ble", required=True, help="the root's Bluetooth address (both boards advertise as runit)")
    ap.add_argument("--child", type=int, default=1)
    a = ap.parse_args()
    asyncio.run(main_async(a))
    failed = [n for (n, good, _) in RESULTS if not good]
    print(f"\n{len(RESULTS) - len(failed)}/{len(RESULTS)} passed")
    return 1 if failed or not RESULTS else 0


if __name__ == "__main__":
    sys.exit(main())
