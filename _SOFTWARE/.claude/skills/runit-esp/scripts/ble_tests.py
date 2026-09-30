"""BLE data connector tests from the PC (bleak), with the console UART link open to cross-check.

Needs a Python with bleak + pyserial (the IDF venv has no bleak):
    python -m venv <dir> && <dir>/Scripts/python -m pip install bleak pyserial
Usage:
    <venv python> ble_tests.py COM3 [--reset]
"""
from __future__ import annotations

import asyncio
import struct
import sys
import time

from bleak import BleakClient, BleakScanner

from runit_link import Link, decode_error_packet, pack_fields, PACKETS

NAME = "runit"
U = "0000{:04x}-0000-1000-8000-00805f9b34fb".format
CH_TX, CH_RX, CH_LOGS = U(0xFFE1), U(0xFFE2), U(0xFFE3)

fails = 0


def check(name: str, cond: bool, detail: str = "") -> None:
    global fails
    print(f"{'PASS' if cond else 'FAIL'}  {name}" + (f"   ({detail})" if detail else ""))
    if not cond:
        fails += 1


class Ble:
    def __init__(self, client: BleakClient):
        self.c = client
        self.tx: list[tuple[float, bytes]] = []
        self.logs: list[tuple[float, bytes]] = []
        self.seq = 0x40

    async def start(self) -> None:
        await self.c.start_notify(CH_TX, lambda _, d: self.tx.append((time.time(), bytes(d))))
        await self.c.start_notify(CH_LOGS, lambda _, d: self.logs.append((time.time(), bytes(d))))

    async def request(self, frame_wo_seq: bytes, timeout: float = 2.0, response: bool = True):
        self.seq = (self.seq + 1) & 0xFF
        seq = self.seq
        await self.c.write_gatt_char(CH_RX, bytes([seq]) + frame_wo_seq, response=response)
        end = time.time() + timeout
        while time.time() < end:
            for _, d in self.tx:
                if len(d) >= 5 and d[0] == 0x05 and d[1] == seq:
                    return d
            await asyncio.sleep(0.02)
        return None

    async def call(self, packet_id: str, **values):
        desc = PACKETS[packet_id]
        return await self.request(bytes([desc["class"], int(desc["packet_header"], 16)]) + pack_fields(desc, values))


def status(resp: bytes | None) -> str:
    if resp is None:
        return "no response"
    if resp[4] == 0:
        return "OK"
    tag, owner = struct.unpack_from("<HH", resp, 5) if len(resp) >= 9 else (0, 0)
    from runit_link import tag_name
    return tag_name(tag)


async def main() -> int:
    link = Link(sys.argv[1], reset="--reset" in sys.argv)
    time.sleep(0.5)
    dev = await BleakScanner.find_device_by_name(NAME, timeout=10.0)
    check("device advertises", dev is not None)
    if dev is None:
        return 1
    async with BleakClient(dev) as client:
        ble = Ble(client)
        await ble.start()
        await asyncio.sleep(1.0)
        mtu = client.mtu_size
        check("MTU above default 23", mtu > 23, f"mtu={mtu}")

        # 1. command over BLE: response on 0xFFE1, not on the UART
        m = link.mark()
        r = await ble.call("packet_sys_io_reset_t", device_id=0, pin=4)
        check("BLE command answered on FFE1", r is not None and r[2:4] == bytes([0x01, 0x20]), r.hex() if r else "none")
        check("seq echoed", r is not None and r[1] == ble.seq)
        await asyncio.sleep(0.3)
        uart_resp = [f for f in link.frames if f.t * 1000 >= m and f.stream == 0x05]
        check("BLE command's response not sent to UART", not uart_resp, f"{len(uart_resp)} on UART")

        # 2. command over UART: response on UART only
        n_before = len(ble.tx)
        ru = link.call("packet_sys_io_reset_t", device_id=0, pin=4)
        await asyncio.sleep(0.3)
        ble_resp = [d for _, d in ble.tx[n_before:] if d[0] == 0x05]
        check("UART command answered on UART", ru is not None and ru.ok)
        check("UART command's response not sent to BLE", not ble_resp, f"{len(ble_resp)} on BLE")

        # 3. error over BLE: error response + error packet + log text on FFE3, not cut to 20 B
        n_logs = len(ble.logs)
        r = await ble.call("packet_sys_io_get_level_t", device_id=77, pin=1)
        check("error response over BLE", status(r) == "ERR_DEV_NOT_FOUND", status(r))
        await asyncio.sleep(0.5)
        new = [d for _, d in ble.logs[n_logs:]]
        texts = [d for d in new if d[0] == 0x03]
        errs = [d for d in new if d[0] == 0x04]
        check("error packet on FFE3", bool(errs))
        if errs:
            ep = decode_error_packet(errs[0][1:])
            check("error packet decodes", any("ERR_DEV_NOT_FOUND" in str(n) for n in ep.nodes), str(ep))
        longest = max((len(d) for d in texts), default=0)
        check("log text lines longer than 20 B", longest > 20, f"longest={longest}, lines={len(texts)}")
        if texts:
            print("      sample log:", texts[0][1:].decode(errors="replace").strip()[:120])

        # 4. large writes: one frame per write, up to the frame limit
        for size in (100, mtu - 3, 249, 400, 512):
            payload = bytes([0x7E, 0x01]) + bytes(size - 3)  # unknown class, padded; +1 seq = size
            try:
                r = await ble.request(payload, timeout=3.0)
                check(f"{size} B write answered", r is not None, status(r))
            except Exception as e:  # noqa: BLE001
                check(f"{size} B write answered", False, f"write failed: {e}")
        big = bytes([0x7E, 0x01]) + bytes(600)
        try:
            r = await ble.request(big, timeout=2.0)
            check("603 B write (over FRAME_MAX) rejected", r is None or status(r) != "OK", status(r))
        except Exception as e:  # noqa: BLE001
            check("603 B write (over FRAME_MAX) rejected", True, f"write error: {type(e).__name__}")

        # 5. burst of 20 writes without response
        seqs = []
        for _ in range(20):
            ble.seq = (ble.seq + 1) & 0xFF
            seqs.append(ble.seq)
            await client.write_gatt_char(CH_RX, bytes([ble.seq, 0x01, 0x20, 0, 4]), response=False)
        await asyncio.sleep(2.0)
        got = [d[1] for _, d in ble.tx if d[0] == 0x05 and d[1] in seqs]
        check("20 writes without response: 20 answers in order", got == seqs, f"got {len(got)}")

    # 6. reconnect
    await asyncio.sleep(1.0)
    dev = await BleakScanner.find_device_by_name(NAME, timeout=10.0)
    check("advertises again after disconnect", dev is not None)
    if dev:
        async with BleakClient(dev) as client:
            ble = Ble(client)
            await ble.start()
            r = await ble.call("packet_sys_io_reset_t", device_id=0, pin=4)
            check("works after reconnect", status(r) == "OK", status(r))
    crash = [l for _, l in link.logs if "Guru" in l or "abort" in l.lower() or "rst:" in l]
    check("no crash / reset on the UART log", not crash or ("--reset" in sys.argv and len(crash) == 1), str(crash))
    link.close()
    print(f"\n{fails} failure(s)")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(asyncio.run(main()))
