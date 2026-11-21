#!/usr/bin/env python3
"""A client of the firmware over BLE (bleak, the PC's Python): a session on the runIT service, like session_client.py on the console.

    python runit-esp/tools/ble_client.py                    # the hello, PING and PEERS
    python runit-esp/tools/ble_client.py --smoke            # PING, PEERS, GPIO 4 level, a watch on it
    python runit-esp/tools/ble_client.py --hold 10          # a watch on GPIO 4's level for 10 s: the entries by owner

Service 0xFFE0: frames out on 0xFFE1 (notify), in on 0xFFE2 (write), both framed (SYS_BLE.MD "Framing": a header byte per
packet, bit 7 LAST, bit 6 FIRST, bits 0-5 a counter); a frame is [type][src][key][seq u16][body] (CMD.MD "The header").
The watches' entries are not on the session: they are raw notifications of 0xFFEA, [hdr PUSH][owner][id][status][data], one
per notification (RUNIT.MD "Ways"), every owner's. The client's id is 0xF1 (the console client has 0xF0). Needs `pip install bleak` (the system Python, not the IDF's).
Exit 0 when every step answered as it should.
"""
import argparse
import asyncio
import os
import struct
import sys
import time

from bleak import BleakClient, BleakScanner

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import runit_defs as defs  # noqa: E402

CLIENT = defs.sym("CMD_SRC_CLIENT_FIRST") + 1
REQUEST = defs.sym("CMD_REQUEST")
ANSWER = defs.sym("CMD_ANSWER")
PUSH = defs.sym("CMD_PUSH")
CONTROL = defs.sym("CMD_CONTROL")
HELLO = defs.sym("SYS_SESSION_HELLO")
PING = defs.sym("SYS_SESSION_PING")
KEY_SYS = defs.sym("MODULE_ID_SYS")
KEY_SESSION = defs.sym("MODULE_ID_SESSION")
KEY_WATCH = defs.sym("MODULE_ID_WATCH")
KEY_DEVICE = defs.sym("MODULE_ID_SYS_DEVICE")
READ = defs.sym("CMD_DIR_READ")
WRITE = defs.sym("CMD_DIR_WRITE")
GPIO = 0x00
IO_LEVEL = defs.contract_op("IO", "LEVEL")
IO_MODE = defs.contract_op("IO", "MODE")
IO_RESET = defs.contract_op("IO", "RESET")
SYS_OP_PING = defs.service_op("SYS", "PING")
SESSION_OP_PEERS = defs.service_op("SESSION", "PEERS")
WATCH_OP_ADD = defs.service_op("WATCH", "ADD")
WATCH_OP_REMOVE = defs.service_op("WATCH", "REMOVE")
MODE_OUTPUT_PP = defs.sym("SYS_IO_MODE_OUTPUT_PUSH_PULL")
TRIG_PERIODIC = defs.sym("SYS_WATCH_PERIODIC")
F_ERROR = defs.sym("CMD_TYPE_STATUS")
SERVICE = defs.sym("RUNIT_BLE_SERVICE")
TX = defs.sym("RUNIT_BLE_TX")
RX = defs.sym("RUNIT_BLE_RX")
LOGS = defs.sym("RUNIT_BLE_LOGS")
TELEMETRY = defs.sym("RUNIT_BLE_TELEMETRY")


def uuid16(u: int) -> str:
    return f"0000{u:04x}-0000-1000-8000-00805f9b34fb"


class Link:
    def __init__(self, client: BleakClient):
        self.c, self.seq, self.boot_id = client, 0, int(time.time()) & 0xFFFFFFFF
        self.q: asyncio.Queue = asyncio.Queue()
        self.partial, self.board = bytearray(), None
        self.pushes: list = []  # (src, key, seq, body)

    def on_packet(self, _char, data: bytearray):
        h = data[0]
        if h & 0x40:
            self.partial = bytearray()
        self.partial += data[1:]
        if h & 0x80:
            f, self.partial = bytes(self.partial), bytearray()
            if len(f) >= 5:
                self.q.put_nowait((f[0] >> 6, f[0] & 0x3F, f[1], f[2], struct.unpack_from("<H", f, 3)[0], f[5:]))

    def on_telemetry(self, _char, data: bytearray):
        if len(data) >= 5 and data[0] >> 6 == PUSH:
            self._push(data[1], data[2], struct.unpack_from("<H", data, 3)[0], bytes(data[5:]))

    async def send(self, kind: int, key: int, body: bytes, seq: int = 0):
        frame = bytes([kind << 6, CLIENT, key]) + struct.pack("<H", seq) + body
        step = self.c.mtu_size - 3 - 1
        parts = [frame[i:i + step] for i in range(0, len(frame), step)] or [b""]
        for i, p in enumerate(parts):
            h = (0x40 if i == 0 else 0) | (0x80 if i == len(parts) - 1 else 0) | (i & 0x3F)
            await self.c.write_gatt_char(uuid16(RX), bytes([h]) + p, response=True)

    async def frames(self, wait: float):
        end = time.monotonic() + wait
        while (left := end - time.monotonic()) > 0:
            try:
                yield await asyncio.wait_for(self.q.get(), left)
            except asyncio.TimeoutError:
                return

    async def hello(self) -> bool:
        await self.send(CONTROL, KEY_SESSION, bytes([HELLO]) + struct.pack("<II", self.boot_id, 0))
        async for kind, _, src, _, _, b in self.frames(3.0):
            if kind == CONTROL and b[:1] == bytes([HELLO]) and len(b) >= 9:
                self.board = src
                boot, build = struct.unpack_from("<II", b, 1)
                print(f"<- hello: board {src:#04x}, boot {boot:#010x}, build {build:#010x}, mtu {self.c.mtu_size}")
                return True
        return False

    def _push(self, src, key, seq, b):
        self.pushes.append((src, key, seq, b))

    async def listen(self, wait: float):
        async for kind, _, src, key, seq, b in self.frames(wait):
            if kind == PUSH:
                self._push(src, key, seq, b)

    async def call(self, cmd: bytes, wait: float = 2.0):
        """cmd is [key][dir][op][args]."""
        self.seq = (self.seq + 1) & 0xFFFF
        await self.send(REQUEST, cmd[0], cmd[1:], self.seq)
        print(f"-> seq {self.seq}: {cmd.hex(' ')}")
        async for kind, flags, src, key, seq, b in self.frames(wait):
            if kind == ANSWER and seq == self.seq:
                st = 1 if flags & F_ERROR else 0
                print(f"<- seq {seq}: status {st} {b.hex(' ')}")
                return st, b
            if kind == PUSH:
                self._push(src, key, seq, b)
        print("<- no answer")
        return None, b""


def dev(d: int, op: int, args: bytes = b"") -> bytes:
    return bytes([KEY_DEVICE, d, GPIO]) + struct.pack("<H", op) + args


def watches(link: Link) -> list:
    return [(b[0], b[1], b[2], b[3:]) for _, k, _, b in link.pushes if k == KEY_WATCH and len(b) >= 3]


async def smoke(link: Link) -> bool:
    ok, pin = True, 4

    async def step(name, cmd, want=None):
        nonlocal ok
        st, b = await link.call(cmd)
        good = st == 0 and (want is None or b == want)
        print(f"{'ok  ' if good else 'FAIL'} {name}")
        ok &= good
        return b

    await step("PING", bytes([KEY_SYS, READ, SYS_OP_PING]))
    rows = await step("PEERS", bytes([KEY_SESSION, READ, SESSION_OP_PEERS]))
    print(f"   {rows[0]} sessions")
    await step("GPIO4 MODE output", dev(WRITE, IO_MODE, bytes([pin, MODE_OUTPUT_PP])))
    for lvl in (1, 0, 1):
        await step(f"GPIO4 LEVEL {lvl}", dev(WRITE, IO_LEVEL, bytes([pin, lvl])))
        await step(f"GPIO4 LEVEL reads {lvl}", dev(READ, IO_LEVEL, bytes([pin])), bytes([pin, lvl]))
    link.pushes.clear()
    head = struct.pack("<BBHH", 1, TRIG_PERIODIC, 5, 0)
    await step("WATCH ADD", bytes([KEY_WATCH, WRITE, WATCH_OP_ADD]) + head + dev(READ, IO_LEVEL, bytes([pin])))
    await link.listen(0.6)
    mine = [w for w in watches(link) if w[0] == CLIENT and w[1] == 1]
    good = len(mine) >= 4 and all(w[2] == 0 and w[3] == bytes([pin, 1]) for w in mine)
    print(f"{'ok  ' if good else 'FAIL'} WATCH entries: {len(mine)} in 0.6 s, level 1")
    ok &= good
    await step("WATCH REMOVE", bytes([KEY_WATCH, WRITE, WATCH_OP_REMOVE, 1]))
    await step("GPIO4 RESET", dev(WRITE, IO_RESET, bytes([pin])))
    print("smoke PASSED" if ok else "smoke FAILED")
    return ok


async def hold(link: Link, seconds: float) -> bool:
    """A watch on GPIO 4's level (every 50 ms), listened for `seconds`: the entries by owner and the levels seen."""
    head = struct.pack("<BBHH", 1, TRIG_PERIODIC, 5, 0)
    st, _ = await link.call(bytes([KEY_WATCH, WRITE, WATCH_OP_ADD]) + head + dev(READ, IO_LEVEL, bytes([4])))
    link.pushes.clear()
    await link.listen(seconds)
    by_owner: dict = {}
    for owner, wid, status, data in watches(link):
        by_owner.setdefault((owner, wid), []).append(data[1] if status == 0 and len(data) == 2 else None)
    for (owner, wid), lv in sorted(by_owner.items()):
        print(f"owner {owner:#04x} watch {wid}: {len(lv)} entries, levels {sorted(set(lv), key=str)}")
    await link.call(bytes([KEY_WATCH, WRITE, WATCH_OP_REMOVE, 1]))
    return st == 0 and (CLIENT, 1) in by_owner


async def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--smoke", action="store_true")
    ap.add_argument("--hold", type=float, default=0, help="watch GPIO 4's level this many seconds")
    ap.add_argument("--name", default="runit")
    args = ap.parse_args()

    dev_ = await BleakScanner.find_device_by_filter(
        lambda d, a: uuid16(SERVICE) in [u.lower() for u in a.service_uuids] or (d.name or "").lower() == args.name,
        timeout=10)
    if dev_ is None:
        print("no board advertises the runIT service")
        return 1
    print(f"board: {dev_.name} {dev_.address}")
    async with BleakClient(dev_) as client:
        link = Link(client)
        await client.start_notify(uuid16(TX), link.on_packet)
        await client.start_notify(uuid16(TELEMETRY), link.on_telemetry)
        if not await link.hello():
            print("<- no hello")
            return 1
        if args.smoke:
            return 0 if await smoke(link) else 1
        if args.hold:
            return 0 if await hold(link, args.hold) else 1
        ok = True
        for cmd in (bytes([KEY_SYS, READ, SYS_OP_PING]), bytes([KEY_SESSION, READ, SESSION_OP_PEERS])):
            st, _ = await link.call(cmd)
            ok &= st == 0
        return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(asyncio.run(main()))
