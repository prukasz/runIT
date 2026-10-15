"""BLE link suite for a runIT board, from a PC's Bluetooth stack (bleak): the link as the app's backend uses it.

  ..\\..\\..\\..\\.venv\\Scripts\\python ble_suite.py [scenario ...] [--name runit | --address AA:BB:..] [--uart COM10]

Scenarios (default: basic frames streams latency cycles):
  basic     MTU is what the board offers, GATT table, PING, urgent PING, BLE STATUS agrees on the MTU
  frames    requests from 13 B to the frame limit in packets of 1 to 512 B (counter wrap included), the limit itself, a frame
            over it, fragment errors on the errors stream
  streams   the errors stream and the log line on LOGS, the board's fragmented answer (connectors INFO)
  latency   PING round trips, pipelining, a full frame per packet size, throughput
  cycles    disconnect + connect with a PING, --cycles times; a disconnect in the middle of a fragmented frame
  idle      the link after --idle-s seconds with no traffic
  stress    --requests mixed requests (ping, list, status, oversized, bursts)
  restart   RESTART the board: the loss is noticed and the board is back (reboots the board)
  gatt      a runtime GATT change with APPLY while connected, then a reopen sees it (adds and removes a test service)
  load      --frames FILE: sends VM frames ([{"op": n, "payload": "hex"}, ...] as the app's engine makes them) to the VM router in order, each
            answered OK. The program stays loaded: end the file with a reset frame to leave the VM empty
  actions   stored records: a user action (--action-id, default 10; 3 to 6 belong to the firmware) that switches the two LEDs
            (--device 1, --pins 22,23) off is stored, listed, read back frame for frame, run, and removed
  project   a project with a device section that lights the two LEDs and autostart: stored, INFO matches, every record read back,
            the board restarted and the LEDs are on after the boot; then erased (reboots the board)
  capture   --plan FILE: {"load": [frames], "writes": [[frames], ...]} (the engine's frames: a program, then runtime writes): loads the
            program, applies each write 0.7 s apart and records every stream frame raw (--out FILE); checks that telemetry came after
            each write. Ends with a VM reset
  deadman   dead-man on link lost, read from the console (needs --uart); the safe state suspends every device, so the scenario resumes them and leaves the dead-man off
  soak      --minutes of random cycles, with --uart also watching the console for panics and reboots

--uart (a console port) lets the suite see the board's log: the dead-man check and the panic / unexpected-reboot watch.
Exit 1 when a check fails. Nothing is typed in from the firmware: see ble_link.py.
"""
from __future__ import annotations

import argparse
import asyncio
import json
import random
import re
import statistics
import struct
import sys
import time
import zlib
from pathlib import Path

from ble_link import (
    ATT_VALUE_MAX, BLE_OPS, KEY_DEVICE, action_info, action_list, action_read, action_run, action_store, device_frame, image, project_info, project_save, section, FRAG_FIRST, FRAG_LAST, FRAME_MAX, GENERATED, KEYS, LOGS, RX, STREAM_ERRORS, STREAM_LOGS, STREAM_TELEMETRY, SVC, TX,
    BleLink, find_board, fragment, offered_mtu,
)
from router_link import Link, decode_record

KEY_SYS, KEY_BLE, KEY_CONN = KEYS["sys"], KEYS["ble"], KEYS["conn"]
PING = bytes([KEY_SYS, 0x01])
OP_RESTART, OP_DEADMAN_SET = 0x03, 0x04
CONN_OPS = {c["id"]: c["op"] for c in json.loads((GENERATED / "services" / "contract_connector.json").read_text(encoding="utf8"))["commands"]}
BLE_STATUS, BLE_LIST = bytes([KEY_BLE, BLE_OPS["STATUS"]]), bytes([KEY_BLE, BLE_OPS["LIST"]])
BAD_LOG = re.compile(r"Guru Meditation|abort\(\)|Backtrace|rst:0x|assert failed|stack overflow|Task watchdog")
BOOT_LOG = "boot sequence complete"
TEST_SVC, TEST_CHR = 0xEE00, 0xEE01

results: list[tuple[str, bool]] = []


def check(name: str, ok: bool, detail: str = "") -> bool:
    results.append((name, ok))
    print(f"{'PASS' if ok else 'FAIL'}  {name}" + (f"\n      {detail}" if detail and not ok else ""), flush=True)
    return ok


def info(text: str) -> None:
    print(f"      {text}", flush=True)


def cause(r) -> str:
    rec = r.record if r is not None else None
    return rec.cause.name if rec and rec.cause else ""


def bad_len_got(r) -> int | None:
    rec = r.record if r is not None else None
    return struct.unpack_from("<H", rec.cause.data)[0] if rec and rec.cause and cause(r) == "ERR_CMD_BAD_LEN" else None


class Ctx:
    def __init__(self, args, dev, uart):
        self.args, self.dev, self.uart, self.link = args, dev, uart, None

    async def open(self, retries: int = 3) -> BleLink:
        last = None
        for _ in range(retries):
            link = BleLink(self.dev, packet=self.args.packet)
            try:
                await link.open()
                self.link = link
                return link
            except Exception as e:  # noqa: BLE001 - retried, then raised
                last = e
                await asyncio.sleep(0.5)
        raise RuntimeError(f"could not connect: {last}")

    async def close(self) -> None:
        if self.link is not None:
            await self.link.close()

    async def reopen(self) -> BleLink:
        await self.close()
        return await self.open()


async def basic(ctx: Ctx) -> None:
    link = ctx.link
    want = offered_mtu()
    check(f"MTU is what the board offers ({want})", want is None or link.mtu == want, f"the central negotiated {link.mtu}")
    services = {s.uuid.lower(): s for s in link.client.services}
    chars = {c.uuid.lower(): c.properties for c in services[SVC].characteristics} if SVC in services else {}
    check("GATT table: TX and LOGS notify, RX writable", "notify" in chars.get(TX, []) and "notify" in chars.get(LOGS, []) and "write" in chars.get(RX, []), str(chars))
    r = await link.request(PING)
    check("PING", r is not None and r.ok and len(r.data) == 4, str(r))
    if r is not None and r.ok:
        info(f"uptime {struct.unpack('<I', r.data)[0]} ms")
    r = await link.request(PING, urgent=True)
    check("PING urgent", r is not None and r.ok, str(r))
    r = await link.request(BLE_STATUS)
    if check("BLE STATUS reports the connection", r is not None and r.ok and r.data[0] == 1, str(r)):
        connected, mtu, overflow = struct.unpack("<BHI", r.data[:7])
        check("the board and the central agree on the MTU", mtu == link.mtu, f"board {mtu}, central {link.mtu}")
        check("no RX overflow", overflow == 0, str(overflow))


async def frames(ctx: Ctx) -> None:
    link = ctx.link
    longest = FRAME_MAX - 4  # [seq][flags][key][op] before the payload
    for packet in (20, 100, 240, ATT_VALUE_MAX):
        bad = []
        for n in (1, 100, 300, longest):
            r = await link.request(PING, bytes(n), packet=packet)
            if bad_len_got(r) != n:
                bad.append(f"{n} B -> {cause(r) or 'no answer'}")
        check(f"requests of 5 to {FRAME_MAX} B in packets of {packet}: reassembled and routed", not bad, "; ".join(bad))
    r = await link.request(PING, bytes(68), packet=2)  # 72 packets of one byte: the counter wraps 63 -> 0
    check("a frame in 72 packets (the counter wraps)", bad_len_got(r) == 68, str(r))
    r = await link.request(PING, bytes(longest + 1))
    check(f"a frame over {FRAME_MAX} B is dropped, with no answer", r is None, str(r))
    r = await link.request(PING)
    check("the link is fine after it", r is not None and r.ok, str(r))
    t = time.time()
    await link.write_packets([bytes([FRAG_LAST | 5, 0x99, 0x99])])  # no FIRST
    p = await link.wait_stream(t, STREAM_ERRORS, lambda p: "ERR_BLE_FRAG" in decode_record(p).names())
    check("a packet without FIRST -> ERR_BLE_FRAG on the errors stream", p is not None)
    t = time.time()
    await link.write_packets([bytes([FRAG_FIRST, 0xAA, 0x00]), bytes([FRAG_LAST | 2, 0x01, 0x01])])  # skips 1
    p = await link.wait_stream(t, STREAM_ERRORS, lambda p: "ERR_BLE_FRAG" in decode_record(p).names())
    check("a counter gap -> ERR_BLE_FRAG, the frame dropped", p is not None)
    r = await link.request(PING)
    check("the next frame is fine after a dropped one", r is not None and r.ok, str(r))
    check("no reassembly errors on the client", not link.reassembly_errors, str(link.reassembly_errors))


async def streams(ctx: Ctx) -> None:
    link = ctx.link
    t = time.time()
    r = await link.request(bytes([0x09, 0x01]))  # an unused root key
    check("unknown key -> an error answer", cause(r) == "ERR_ROUTER_UNKNOWN_KEY", str(r))
    seq = link.seq - 1 & 0xFF
    p = await link.wait_stream(t, STREAM_ERRORS, lambda p: (rc := decode_record(p)).src == 1 and rc.seq == seq)
    check("the same error record on the errors stream", p is not None)
    p = await link.wait_stream(t, STREAM_LOGS, lambda p: b"ERR_ROUTER_UNKNOWN_KEY" in p)
    check("the log line on the logs stream", p is not None)
    # the connectors' INFO grows with each connector: more than one packet at a small MTU
    made = []
    for i in range(3, 9):
        name = f"long-name-{i:02d}-xx".encode() + b"\0"
        if (await link.request(bytes([KEY_CONN, CONN_OPS["CREATE"], i, 0x02]) + name)) is None:
            break
        made.append(i)
    r = await link.request(bytes([KEY_CONN, CONN_OPS["INFO"]]))
    size = len(r.data) + 3 if r else 0
    check(f"connectors INFO ({size} B) arrives whole, in {getattr(r, 'packets', '?')} packet(s)", r is not None and r.ok and not link.reassembly_errors, str(r))
    for i in made:
        await link.request(bytes([KEY_CONN, CONN_OPS["REMOVE"], i]))


async def latency(ctx: Ctx) -> None:
    link = ctx.link
    ts = []
    for _ in range(50):
        t = time.perf_counter()
        r = await link.request(PING)
        if r is not None:
            ts.append((time.perf_counter() - t) * 1000)
    ts.sort()
    check("50 PINGs answered", len(ts) == 50)
    if ts:
        info(f"round trip: median {statistics.median(ts):.1f} ms, p95 {ts[int(len(ts) * 0.95) - 1]:.1f} ms, max {ts[-1]:.1f} ms")
    t = time.perf_counter()
    rs = await asyncio.gather(*[link.request(PING) for _ in range(40)])
    check("40 pipelined PINGs answered", all(r is not None and r.ok for r in rs))
    info(f"40 pipelined: {(time.perf_counter() - t) * 1000:.0f} ms")
    for packet in (20, 100, 240, ATT_VALUE_MAX):
        ts = []
        for _ in range(10):
            t = time.perf_counter()
            await link.request(PING, bytes(FRAME_MAX - 4), packet=packet)
            ts.append((time.perf_counter() - t) * 1000)
        info(f"a {FRAME_MAX} B request in packets of {packet}: median {statistics.median(ts):.0f} ms")
    t = time.perf_counter()
    for _ in range(20):
        await link.request(PING, bytes(FRAME_MAX - 4))
    info(f"throughput, {FRAME_MAX} B back to back: {20 * FRAME_MAX / (time.perf_counter() - t) / 1000:.1f} KB/s")


async def cycles(ctx: Ctx) -> None:
    times, failed = [], 0
    for i in range(ctx.args.cycles):
        await ctx.close()
        t = time.perf_counter()
        try:
            link = await ctx.open()
            r = await link.request(PING)
            if r is None or not r.ok:
                raise RuntimeError("no PING answer")
            times.append((time.perf_counter() - t) * 1000)
        except Exception as e:  # noqa: BLE001
            failed += 1
            info(f"cycle {i}: {e}")
            await asyncio.sleep(1.0)
    check(f"{ctx.args.cycles} disconnect + connect cycles, each with a PING", failed == 0 and bool(times), f"{failed} failed")
    if times:
        info(f"median {statistics.median(times):.0f} ms, max {max(times):.0f} ms")
    await ctx.reopen()
    await dropped_mid_frame(ctx)


async def dropped_mid_frame(ctx: Ctx) -> None:
    """A disconnect with half a fragmented frame sent: the next connection must start clean."""
    frame = bytes([0x7E, 0]) + PING + bytes(200)
    packets = fragment(frame, 20)
    await ctx.link.write_packets(packets[: len(packets) // 2])
    await ctx.reopen()
    r = await ctx.link.request(PING)
    check("a disconnect in the middle of a frame leaves the next connection clean", r is not None and r.ok, str(r))


async def idle(ctx: Ctx) -> None:
    await asyncio.sleep(ctx.args.idle_s)
    t = time.perf_counter()
    r = await ctx.link.request(PING)
    check(f"the link survives {ctx.args.idle_s:g} s with no traffic", r is not None and r.ok and not ctx.link.lost.is_set(), str(r))
    info(f"first PING after the idle: {(time.perf_counter() - t) * 1000:.0f} ms")


async def stress(ctx: Ctx) -> None:
    link, rng, bad = ctx.link, random.Random(1), []
    t = time.perf_counter()
    for i in range(ctx.args.requests):
        kind = rng.choice(["ping", "ping", "list", "status", "big", "burst"])
        if kind == "ping":
            r = await link.request(PING)
            ok = r is not None and r.ok
        elif kind == "list":
            r = await link.request(BLE_LIST)
            ok = r is not None and r.ok
        elif kind == "status":
            r = await link.request(BLE_STATUS)
            ok = r is not None and r.ok
        elif kind == "big":
            n = rng.randint(1, FRAME_MAX - 4)
            ok = bad_len_got(await link.request(PING, bytes(n))) == n
        else:
            rs = await asyncio.gather(*[link.request(PING) for _ in range(8)])
            ok = all(r is not None and r.ok for r in rs)
        if not ok:
            bad.append(f"#{i} {kind}")
    check(f"{ctx.args.requests} mixed requests", not bad, ", ".join(bad[:10]))
    check("no reassembly errors on the client", not link.reassembly_errors, str(link.reassembly_errors[:3]))
    info(f"{time.perf_counter() - t:.1f} s")


async def restart(ctx: Ctx) -> None:
    r = await ctx.link.request(bytes([KEY_SYS, OP_RESTART]))
    check("RESTART answered", r is not None and r.ok, str(r))
    t0 = time.perf_counter()
    try:
        await asyncio.wait_for(ctx.link.lost.wait(), 6)
        info(f"the loss was noticed after {(time.perf_counter() - t0) * 1000:.0f} ms")
    except asyncio.TimeoutError:
        # the OS may report a dead link late or never (see the gatt scenario): carry on and reconnect, as the app's link does
        info("no disconnect event within 6 s; reconnecting anyway")
    for attempt in range(40):
        try:
            try:
                await asyncio.wait_for(ctx.close(), 3)
            except Exception:  # noqa: BLE001 - a link the OS has not yet declared dead may refuse to close
                pass
            link = await ctx.open(retries=1)
            r = await link.request(PING, timeout=1.0)
            if r is not None and r.ok:
                up = struct.unpack("<I", r.data)[0]
                check("the board is back after the restart", up < 20000, f"{(time.perf_counter() - t0) * 1000:.0f} ms from the drop, attempt {attempt}, uptime {up} ms")
                return
        except Exception:  # noqa: BLE001 - the board is still booting
            await asyncio.sleep(0.2)
    check("the board is back after the restart", False)


async def gatt(ctx: Ctx) -> None:
    link = ctx.link
    made = await link.request(bytes([KEY_BLE, BLE_OPS["SVC_CREATE"]]), struct.pack("<HB", TEST_SVC, 1))
    made2 = await link.request(bytes([KEY_BLE, BLE_OPS["CHAR_CREATE"]]), struct.pack("<HHBBBBII", TEST_SVC, TEST_CHR, 0, 0, 1, 0, 96, 0) + b"temp\0")
    applied = await link.request(bytes([KEY_BLE, BLE_OPS["APPLY"]]))
    check("service and characteristic created, APPLY accepted", all(r is not None and r.ok for r in (made, made2, applied)), f"{made} {made2} {applied}")
    await asyncio.sleep(1.5)  # the board applies after its delay; the central's OS may drop its handles without a disconnect
    try:
        r = await link.request(PING, timeout=1.0)
        info(f"PING on the same link after APPLY: {'answered' if r else 'no answer'} (disconnect event: {link.lost.is_set()})")
    except Exception as e:  # noqa: BLE001
        info(f"PING on the same link after APPLY failed: {e!r} (disconnect event: {link.lost.is_set()}): the app must reopen")
    link = await ctx.reopen()
    names = [s.uuid[4:8] for s in link.client.services]
    r = await link.request(BLE_LIST)
    check("after a reopen the new service is visible and the link works", r is not None and r.ok and f"{TEST_SVC:04x}" in names, f"services {names}")
    await link.request(bytes([KEY_BLE, BLE_OPS["SVC_REMOVE"]]), struct.pack("<H", TEST_SVC))
    await link.request(bytes([KEY_BLE, BLE_OPS["APPLY"]]))
    await asyncio.sleep(1.0)
    link = await ctx.reopen()
    names = [s.uuid[4:8] for s in link.client.services]
    check("the test service is removed again", f"{TEST_SVC:04x}" not in names, f"services {names}")


async def load(ctx: Ctx) -> None:
    if not ctx.args.frames:
        check("load needs --frames FILE", False)
        return
    frames = json.loads(Path(ctx.args.frames).read_text(encoding="utf8"))
    bad, t = [], time.perf_counter()
    for i, f in enumerate(frames):
        r = await ctx.link.request(bytes([KEYS["vm"], f["op"]]), bytes.fromhex(f.get("payload", "")), timeout=5.0)
        if r is None or not r.ok:
            bad.append(f"frame {i} (op {f['op']:#04x}): {r if r else 'no answer'}")
            break
    check(f"{len(frames)} VM frames, each answered OK", not bad, "; ".join(bad))
    info(f"{(time.perf_counter() - t) * 1000:.0f} ms")


def _pin_frames(args, level: int) -> list[bytes]:
    pins = [int(p) for p in args.pins.split(",")]
    return [device_frame(args.device, 0x0100, bytes([pin, level])) for pin in pins]  # IO contract (1) SET_LEVEL (0)


async def _levels(ctx: Ctx) -> list:
    out = []
    for pin in (int(p) for p in ctx.args.pins.split(",")):
        r = await ctx.link.request(bytes([KEY_DEVICE]), struct.pack("<HHB", ctx.args.device, 0x0102, pin))  # IO GET_LEVEL
        out.append(r.data[0] if r is not None and r.ok and r.data else None)
    return out


async def actions(ctx: Ctx) -> None:
    link, aid = ctx.link, ctx.args.action_id
    if aid in (3, 4, 5, 6):
        check(f"action {aid} is free", False, "3 to 6 are the firmware's own (resume, suspend, reset, hard reset)")
        return
    off = _pin_frames(ctx.args, 0)
    r = await action_store(link, aid, off)
    check(f"action {aid} stored", r is not None and r.ok, str(r))
    info = await action_info(link, aid)
    check("INFO: bytes, frames and the CRC of the image", info is not None and info[1] == len(off) and info[2] == zlib.crc32(image(off)), str(info))
    check("LIST names it", aid in await action_list(link))
    check("READ gives the frames back as stored", await action_read(link, aid) == off)
    for frame in _pin_frames(ctx.args, 1):
        await link.request(frame[:1], frame[1:])
    check("the LEDs are on before the run", await _levels(ctx) == [1] * len(off))
    r = await action_run(link, aid)
    check(f"RUN {aid}", r is not None and r.ok, str(r))
    check("both LEDs are off after it", await _levels(ctx) == [0] * len(off), str(await _levels(ctx)))
    r = await link.request(bytes([KEYS["actions"], 0x0A]), struct.pack("<I", aid))  # REMOVE
    check("REMOVE", r is not None and r.ok and aid not in await action_list(link), str(r))


async def project(ctx: Ctx) -> None:
    link = ctx.link
    device_id = 0x02000000 | ctx.args.device
    before = await project_info(link)
    check("no project is stored to start with", before is not None and not before["stored"], f"{before}: ERASE it first, this scenario replaces it")
    on = _pin_frames(ctx.args, 1)
    r = await project_save(link, {device_id: on}, autostart=True)
    check("the project is stored", r is not None and r.ok, str(r))
    stream = image([section(device_id), *on])
    info = await project_info(link)
    check("INFO: stored, autostart, the length and CRC of the stream", info is not None and info["stored"] and info["autostart"] and info["bytes"] == len(stream) and info["crc"] == zlib.crc32(stream), str(info))
    check("the device section is a record of its own, as sent", await action_read(link, device_id) == on)
    record = await action_read(link, 0x03000000)
    check("the project record runs the section", record is not None and any(f[:2] == bytes([KEYS["actions"], 0x0D]) and struct.unpack_from("<I", f, 2)[0] == device_id for f in record), str(record))
    for frame in _pin_frames(ctx.args, 0):
        await link.request(frame[:1], frame[1:])
    check("the LEDs are off before the restart", await _levels(ctx) == [0] * len(on))
    await restart(ctx)
    await asyncio.sleep(1.0)
    levels = await _levels(ctx)
    check("after the boot the project lit the LEDs", levels == [1] * len(on), str(levels))
    r = await ctx.link.request(bytes([KEYS["project"], 0x07]))  # ERASE
    r2 = await ctx.link.request(bytes([KEYS["actions"], 0x0A]), struct.pack("<I", device_id))  # REMOVE the device record
    after = await project_info(ctx.link)
    check("erased, and the device record removed", r is not None and r.ok and r2 is not None and r2.ok and after is not None and not after["stored"], f"{r} {r2} {after}")


async def capture(ctx: Ctx) -> None:
    if not ctx.args.plan:
        check("capture needs --plan FILE", False)
        return
    plan = json.loads(Path(ctx.args.plan).read_text(encoding="utf8"))
    link, t0 = ctx.link, time.time()

    async def send(frames) -> bool:
        for f in frames:
            r = await link.request(bytes([KEYS["vm"], f["op"]]), bytes.fromhex(f.get("payload", "")), timeout=5.0)
            if r is None or not r.ok:
                return False
        return True

    check("the program loads", await send(plan["load"]))
    await asyncio.sleep(0.7)
    for i, writes in enumerate(plan.get("writes", [])):
        before = len(link.streams_since(0, STREAM_TELEMETRY))
        ok = await send(writes)
        await asyncio.sleep(0.7)
        check(f"runtime write {i} accepted, and telemetry followed", ok and len(link.streams_since(0, STREAM_TELEMETRY)) > before)
    await link.request(bytes([KEYS["vm"], 1]))  # RESET: leave the VM empty
    frames = [{"t": round(t - t0, 3), "stream": s, "hex": p.hex()} for (t, s, p, _n) in link.streams]
    info(f"{len(frames)} stream frames, {sum(f['stream'] == STREAM_TELEMETRY for f in frames)} of them telemetry")
    if ctx.args.out:
        Path(ctx.args.out).write_text(json.dumps(frames), encoding="utf8")
        info(f"written to {ctx.args.out}")


async def deadman(ctx: Ctx) -> None:
    if ctx.uart is None:
        check("dead-man on link lost", False, "needs --uart (the log shows the safe state)")
        return
    r = await ctx.link.request(bytes([KEY_SYS, OP_DEADMAN_SET]), struct.pack("<IB", 60000, 1))
    check("DEADMAN_SET 60 s, on link lost", r is not None and r.ok, str(r))
    mark = ctx.uart.mark()
    await ctx.close()
    check("disconnect -> safe state LINK_LOST (console log)", ctx.uart.wait_log("safe state (reason 3)", 6.0, since=mark))
    r = ctx.uart.request(bytes([KEY_SYS, OP_DEADMAN_SET]), payload=struct.pack("<IB", 0, 0))
    check("dead-man off again (console)", r is not None and r.ok, str(r))
    link = await ctx.open()
    # the safe state suspended every device and stopped the VM: give the board back as it was found
    r = await link.request(bytes([KEYS["device"]]), struct.pack("<HH", 0xFFFF, 0x0003))
    check("devices resumed after the safe state", r is not None and r.ok, str(r))


async def soak(ctx: Ctx) -> None:
    uart, rng = ctx.uart, random.Random()
    end = time.time() + ctx.args.minutes * 60
    boots0 = len([x for x in uart.logs_since(0) if BOOT_LOG in x]) if uart else 0
    n, failures = 0, 0
    while time.time() < end:
        kind = rng.choices(["reconnect", "drop", "burst", "fragmented"], [4, 3, 2, 2])[0]
        n += 1
        try:
            await ctx.reopen()
            if kind == "drop":
                frame = bytes([0x7E, 0]) + PING + bytes(200)
                packets = fragment(frame, 20)
                await ctx.link.write_packets(packets[: len(packets) // 2])
                await ctx.reopen()
            count = 100 if kind == "burst" else 1
            for _ in range(count):
                r = await ctx.link.request(PING)
                if r is None or not r.ok:
                    raise RuntimeError("no PING answer")
            if kind == "fragmented":
                for packet in (2, 6, 100):
                    if (await ctx.link.request(PING, bytes(rng.randint(0, 40)), packet=packet)) is None:
                        raise RuntimeError(f"no answer in packets of {packet}")
            if ctx.link.reassembly_errors:
                raise RuntimeError(f"reassembly errors {ctx.link.reassembly_errors[:2]}")
        except Exception as e:  # noqa: BLE001
            failures += 1
            info(f"cycle {n} ({kind}): {e}")
        if uart:
            bad = [x for x in uart.logs_since(0) if BAD_LOG.search(x)]
            if bad:
                info(f"console: {bad[0]}")
                failures += 1
                break
        if n % 10 == 0:
            info(f"{n} cycles, {int(end - time.time())} s left, {failures} failures")
    reboots = (len([x for x in uart.logs_since(0) if BOOT_LOG in x]) - boots0) if uart else 0
    check(f"{n} soak cycles", failures == 0 and reboots == 0, f"{failures} failure(s), {reboots} unexpected reboot(s)")


SCENARIOS = {"basic": basic, "frames": frames, "streams": streams, "latency": latency, "cycles": cycles, "idle": idle,
             "stress": stress, "restart": restart, "gatt": gatt, "load": load, "actions": actions, "project": project, "capture": capture, "deadman": deadman, "soak": soak}
DEFAULT = ["basic", "frames", "streams", "latency", "cycles"]


async def run(args) -> int:
    dev = await find_board(args.name, args.address, args.scan_s)
    if not check("a board advertises the runIT service", dev is not None):
        return 1
    info(f"{dev.name} {dev.address}")
    uart = Link(args.uart) if args.uart else None
    ctx = Ctx(args, dev, uart)
    try:
        link = await ctx.open()
        info(f"MTU {link.mtu}, packets of {link.packet} B")
        for name in args.scenarios or DEFAULT:
            print(f"\n== {name}", flush=True)
            try:
                if ctx.link is None or not ctx.link.client.is_connected:
                    await ctx.open()
                await SCENARIOS[name](ctx)
            except Exception as e:  # noqa: BLE001 - reported as a failure of the scenario
                check(f"{name} ran to the end", False, repr(e))
    finally:
        await ctx.close()
        if uart:
            uart.close()
    passed = sum(ok for _, ok in results)
    print(f"\n{passed}/{len(results)} passed")
    return 0 if passed == len(results) else 1


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("scenarios", nargs="*", metavar="scenario", help="see above (default: " + " ".join(DEFAULT) + ")")
    ap.add_argument("--name", default="runit", help="advertised name (default runit)")
    ap.add_argument("--address", help="Bluetooth address; wins over --name")
    ap.add_argument("--uart", help="console port to watch (dead-man, panics), e.g. COM10")
    ap.add_argument("--packet", type=int, default=ATT_VALUE_MAX, help=f"packet size written, header included (default {ATT_VALUE_MAX}, the ATT limit)")
    ap.add_argument("--cycles", type=int, default=10, help="disconnect + connect cycles (default 10)")
    ap.add_argument("--requests", type=int, default=300, help="requests of the stress scenario (default 300)")
    ap.add_argument("--frames", help="JSON file of VM frames for the load scenario")
    ap.add_argument("--action-id", type=int, default=10, help="user action the actions scenario stores (default 10)")
    ap.add_argument("--device", type=int, default=1, help="device id of the LEDs' expander (default 1, the TCA6424A)")
    ap.add_argument("--pins", default="22,23", help="LED pins of that device (default 22,23)")
    ap.add_argument("--plan", help="JSON plan for the capture scenario")
    ap.add_argument("--out", help="file the capture scenario writes the raw stream frames to")
    ap.add_argument("--idle-s", type=float, default=45.0)
    ap.add_argument("--minutes", type=float, default=5.0, help="soak length")
    ap.add_argument("--scan-s", type=float, default=10.0)
    args = ap.parse_args()
    unknown = [n for n in args.scenarios if n not in SCENARIOS]
    if unknown:
        ap.error(f"unknown scenario {unknown[0]!r} (choose from {', '.join(SCENARIOS)})")
    sys.exit(asyncio.run(run(args)))


if __name__ == "__main__":
    main()
