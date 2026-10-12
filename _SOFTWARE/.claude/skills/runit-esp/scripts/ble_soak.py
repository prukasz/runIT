"""BLE soak / link-loss test for the runIT link: repeated connect-disconnect, drops in the
middle of a fragmented frame, request bursts, dead-man on link loss. --uart watches the
console for reboots / panics the whole time.

  ..\\..\\..\\..\\.venv\\Scripts\\python ble_soak.py --uart COM11 --minutes 5

Exit 1 on any failed cycle, reboot or panic. Summary at the end (counts, latencies).
"""

from __future__ import annotations

import argparse
import asyncio
import random
import re
import statistics
import struct
import sys
import time

from bleak import BleakClient, BleakScanner

from ble_tests import FRAG_FIRST, BleLink, fragments
from router_link import KEY_SYS, Link

BAD_LOG = re.compile(r"Guru Meditation|abort\(\)|Backtrace|rst:0x|assert failed|stack overflow|Task watchdog")
BOOT_LOG = "boot sequence complete"

stats: dict[str, list[float]] = {}
failures: list[str] = []


def fail(msg: str) -> None:
    failures.append(msg)
    print(f"FAIL  {msg}", flush=True)


def note(name: str, value: float) -> None:
    stats.setdefault(name, []).append(value)


async def connect(name: str, retries: int = 3) -> tuple[BleakClient, BleLink]:
    last: Exception | None = None
    for _ in range(retries):
        t0 = time.time()
        try:
            dev = await BleakScanner.find_device_by_name(name, timeout=15.0)
            if dev is None:
                raise TimeoutError("advertiser not found")
            client = BleakClient(dev, timeout=20.0)
            await client.connect()
            link = BleLink(client)
            await link.start()
            await asyncio.sleep(0.3)
            note("connect_s", time.time() - t0)
            return client, link
        except Exception as e:  # noqa: BLE001 - retried, then reported
            last = e
            await asyncio.sleep(1.0)
    raise RuntimeError(f"connect failed after {retries} tries: {last}")


async def ping(link: BleLink) -> bool:
    t0 = time.time()
    r, _ = await link.request(bytes([KEY_SYS, 0x01]))
    ok = r is not None and r.ok
    if ok:
        note("ping_ms", (time.time() - t0) * 1000)
    return ok


async def cycle_reconnect(name: str) -> None:
    c, l = await connect(name)
    if not await ping(l):
        fail("reconnect: no PING answer")
    await c.disconnect()


async def cycle_drop_mid_frame(name: str) -> None:
    """Disconnect with a half-sent fragmented frame; the next connection must start clean."""
    c, l = await connect(name)
    frame = bytes([0x7E, 0, KEY_SYS, 0x01]) + bytes(200)
    pk = fragments(frame, 20)
    await l.write_packets(pk[: len(pk) // 2])
    await c.disconnect()
    await asyncio.sleep(0.5)
    c, l = await connect(name)
    if not await ping(l):
        fail("after drop mid-frame: no PING answer")
    await c.disconnect()


async def cycle_burst(name: str, n: int = 100) -> None:
    c, l = await connect(name)
    lost = 0
    for _ in range(n):
        if not await ping(l):
            lost += 1
    if lost:
        fail(f"burst: {lost}/{n} requests unanswered")
    if l.tx.errors or l.logs.errors:
        fail(f"burst: client reassembly errors {l.tx.errors + l.logs.errors}")
    await c.disconnect()


async def cycle_deadman(name: str, uart: Link | None) -> None:
    c, l = await connect(name)
    r, _ = await l.request(bytes([KEY_SYS, 0x04]) + struct.pack("<IB", 60000, 1))
    if r is None or not r.ok:
        fail("deadman: SET refused")
    mark = uart.mark() if uart else 0.0
    await c.disconnect()
    if uart:
        if not uart.wait_log("safe state (reason 3)", 6.0, since=mark):
            fail("deadman: no safe state after the link was lost")
        r = uart.request(bytes([KEY_SYS, 0x04]), payload=struct.pack("<IB", 0, 0))
        if r is None or not r.ok:
            fail("deadman: could not clear over UART")


async def cycle_fragmented(name: str) -> None:
    c, l = await connect(name)
    for chunk in (1, 5, l.chunk_max):
        r, _ = await l.request(bytes([KEY_SYS, 0x01]) + bytes(random.randint(0, 40)), chunk=chunk)
        if r is None:
            fail(f"fragmented chunk {chunk}: no answer")
    await c.disconnect()


CYCLES = [("reconnect", 4), ("drop_mid_frame", 3), ("burst", 2), ("deadman", 1), ("fragmented", 2)]


async def run(args) -> int:
    uart = Link(args.uart) if args.uart else None
    end = time.time() + args.minutes * 60
    n, counts = 0, {}
    boots0 = len([x for x in uart.logs_since(0) if BOOT_LOG in x]) if uart else 0
    names, weights = zip(*CYCLES)
    while time.time() < end:
        kind = random.choices(names, weights)[0]
        n += 1
        counts[kind] = counts.get(kind, 0) + 1
        try:
            if kind == "deadman":
                await cycle_deadman(args.name, uart)
            else:
                await {"reconnect": cycle_reconnect, "drop_mid_frame": cycle_drop_mid_frame,
                       "burst": cycle_burst, "fragmented": cycle_fragmented}[kind](args.name)
        except Exception as e:  # noqa: BLE001
            fail(f"{kind}: {e}")
        if uart:
            bad = [x for x in uart.logs_since(0) if BAD_LOG.search(x)]
            if bad:
                fail(f"console panic/reset: {bad[0]}")
                break
        if n % 10 == 0:
            print(f"  {n} cycles, {int(end - time.time())} s left, {len(failures)} failures", flush=True)

    reboots = (len([x for x in uart.logs_since(0) if BOOT_LOG in x]) - boots0) if uart else 0
    if reboots:
        fail(f"{reboots} unexpected reboot(s)")
    print(f"\n{n} cycles {counts}")
    for k, v in stats.items():
        print(f"  {k}: n={len(v)} mean={statistics.mean(v):.2f} max={max(v):.2f}")
    print(f"{'PASS' if not failures else 'FAIL'}: {len(failures)} failure(s)")
    if uart:
        uart.close()
    return 1 if failures else 0


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--name", default="runit")
    ap.add_argument("--uart", help="console port to watch, e.g. COM11")
    ap.add_argument("--minutes", type=float, default=5.0)
    sys.exit(asyncio.run(run(ap.parse_args())))


if __name__ == "__main__":
    main()
