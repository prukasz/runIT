"""BLE link test for the runIT board, from a PC's Bluetooth stack (bleak). The app's Web Bluetooth link does the same in a browser.

  python ble_test.py [--name runit] [--address AA:BB:..] [--skip-restart]

Needs `pip install bleak` (not in the IDF env). Checks what the app relies on:
  1. the board advertises (name, runIT service 0xFFE0 in the advertisement)
  2. GATT table: 0xFFE1 notify, 0xFFE2 write, 0xFFE3 notify
  3. fragment framing both ways: PING answered, a 300-byte request reassembled
  4. the link drops when the board restarts (SYS RESTART) and the board advertises again; a new connection works
"""
import argparse
import asyncio
import struct
import sys
import time

from bleak import BleakClient, BleakScanner

SVC = "0000ffe0-0000-1000-8000-00805f9b34fb"
TX = "0000ffe1-0000-1000-8000-00805f9b34fb"
RX = "0000ffe2-0000-1000-8000-00805f9b34fb"
LOGS = "0000ffe3-0000-1000-8000-00805f9b34fb"
KEY_SYS, OP_PING, OP_RESTART = 0x01, 0x01, 0x03

FIRST, LAST = 0x40, 0x80
results = []


def check(name, ok, detail=""):
    results.append(ok)
    print(f"  {'ok  ' if ok else 'FAIL'} {name}" + (f" -- {detail}" if detail and not ok else ""))


def split(frame: bytes, size: int = 185):
    body = size - 1
    n = max(1, -(-len(frame) // body))
    return [bytes([(FIRST if i == 0 else 0) | (LAST if i == n - 1 else 0) | (i & 0x3F)]) + frame[i * body:(i + 1) * body] for i in range(n)]


class Joiner:
    def __init__(self):
        self.buf, self.exp, self.inside = b"", 0, False
        self.frames = asyncio.Queue()
        self.problems = 0

    def push(self, _sender, packet: bytearray):
        h = packet[0]
        if h & FIRST:
            self.buf, self.exp, self.inside = b"", 0, True
        elif not self.inside:
            self.problems += 1
            return
        if (h & 0x3F) != self.exp:
            self.problems += 1
            self.inside = False
            return
        self.buf += bytes(packet[1:])
        self.exp = (self.exp + 1) & 0x3F
        if h & LAST:
            self.inside = False
            self.frames.put_nowait(self.buf)


async def find(name, address, timeout=10.0):
    found = {}

    def on(device, adv):
        if (address and device.address.upper() == address.upper()) or (not address and (adv.local_name or "").lower().startswith(name)):
            found[device.address] = (device, adv)

    async with BleakScanner(on):
        end = time.time() + timeout
        while time.time() < end and not found:
            await asyncio.sleep(0.2)
    return next(iter(found.values()), (None, None))


async def request(client, rx_join, seq, keys, payload=b"", timeout=3.0):
    for p in split(bytes([seq, 0]) + keys + payload):
        await client.write_gatt_char(RX, p, response=True)
    return await asyncio.wait_for(rx_join.frames.get(), timeout)


async def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--name", default="runit")
    ap.add_argument("--address")
    ap.add_argument("--skip-restart", action="store_true")
    args = ap.parse_args()

    print("1. advertisement")
    device, adv = await find(args.name, args.address)
    check("board found by name", device is not None, "is the board powered, and not connected to another central?")
    if device is None:
        sys.exit(1)
    print(f"     {device.address}  name={adv.local_name!r}  rssi={adv.rssi}  services={adv.service_uuids}")
    check("runIT service 0xFFE0 is in the advertisement", SVC in [u.lower() for u in adv.service_uuids], "old firmware? the app falls back to the name")

    print("2. GATT table and PING")
    t0 = time.time()
    dropped = asyncio.Event()
    async with BleakClient(device, disconnected_callback=lambda _c: dropped.set()) as client:
        print(f"     connected in {time.time() - t0:.2f} s, mtu {client.mtu_size}")
        chars = {c.uuid: c for s in client.services for c in s.characteristics}
        check("0xFFE1 notifies", TX in chars and "notify" in chars[TX].properties)
        check("0xFFE2 writes", RX in chars and "write" in chars[RX].properties)
        check("0xFFE3 notifies", LOGS in chars and "notify" in chars[LOGS].properties)
        main_join, log_join = Joiner(), Joiner()
        await client.start_notify(TX, main_join.push)
        await client.start_notify(LOGS, log_join.push)
        answer = await request(client, main_join, 1, bytes([KEY_SYS, OP_PING]))
        ok = len(answer) == 7 and answer[0] == 5 and answer[1] == 1 and answer[2] == 0
        check("PING answered [05][seq][00][u32 uptime]", ok, answer.hex(" "))
        uptime = struct.unpack_from("<I", answer, 3)[0] if ok else 0
        print(f"     uptime {uptime / 1000:.1f} s")
        times = []
        for seq in range(2, 22):
            t = time.time()
            a = await request(client, main_join, seq, bytes([KEY_SYS, OP_PING]))
            times.append(time.time() - t)
            if a[1] != seq:
                check("20 pings in order", False, a.hex(" "))
                break
        else:
            check("20 pings in order", True)
            print(f"     round trip median {sorted(times)[10] * 1000:.0f} ms, max {max(times) * 1000:.0f} ms")
        check("no framing problems", main_join.problems == 0 and log_join.problems == 0)

        # a long request: the router answers a bad key with an error record, which can be longer than one packet
        long_bad = await request(client, main_join, 30, bytes([0x7E]) + bytes(300))
        check("a 300-byte request is reassembled by the board (answers, any status)", long_bad[:2] == bytes([5, 30]), long_bad.hex(" ")[:60])

        if args.skip_restart:
            return
        print("3. restart: the link drops and the board comes back")
        await request(client, main_join, 40, bytes([KEY_SYS, OP_RESTART]))
        t = time.time()
        try:
            await asyncio.wait_for(dropped.wait(), 10)
            check("the link drop is reported", True)
            print(f"     dropped after {time.time() - t:.2f} s")
        except asyncio.TimeoutError:
            check("the link drop is reported", False, "no disconnect within 10 s")

    t = time.time()
    device, adv = await find(args.name, args.address, timeout=15)
    check("the board advertises again", device is not None)
    if device is None:
        return
    print(f"     advertising again {time.time() - t:.2f} s after the drop")
    async with BleakClient(device) as client:
        join = Joiner()
        await client.start_notify(TX, join.push)
        a = await request(client, join, 1, bytes([KEY_SYS, OP_PING]))
        ok = len(a) == 7 and a[2] == 0
        check("PING works on the new connection", ok, a.hex(" "))
        if ok:
            up = struct.unpack_from("<I", a, 3)[0]
            check("uptime restarted (smaller than before the restart)", up < uptime, f"{up} vs {uptime}")


asyncio.run(main())
bad = results.count(False)
print(f"\n{len(results) - bad}/{len(results)} checks passed")
sys.exit(1 if bad else 0)
