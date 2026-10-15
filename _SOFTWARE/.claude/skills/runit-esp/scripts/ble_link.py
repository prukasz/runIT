"""Host side of the runIT BLE link, from a PC's Bluetooth stack (bleak). The same protocol the app's backend speaks
(runit-app/src/backend): packets with a fragment header on the runIT service, router frames inside (ROUTER.MD).

  Needs `pip install bleak` (the repository's .venv has it; the IDF env does not).

Nothing about the firmware is typed in here: UUIDs, fragment bits, stream bytes and root keys are read from
generated/common/enums.json, the BLE op numbers from generated/services/contract_ble.json, the frame limit from
limits.json, the MTU the board offers from sdkconfig. Errors are decoded by router_link (tags from the firmware headers).

    dev = await find_board(name="runit")                 # or address="94:A9:..."
    async with BleLink(dev) as link:
        r = await link.request(bytes([KEYS["sys"], 0x01]))   # a Response, or None when nothing answered
"""
from __future__ import annotations

import asyncio
import json
import re
import time

from bleak import BleakClient, BleakScanner

from router_link import ROOT, Response

GENERATED = ROOT / "generated"


def _load(*path: str) -> dict:
    return json.loads(GENERATED.joinpath(*path).read_text(encoding="utf8"))


def _group(name: str, prefix: str) -> dict[str, int]:
    return {m["name"].removeprefix(prefix): m["value"] for m in _load("common", "enums.json")["enums"][name]["members"]}


_UUID = _group("ble_runit_uuid", "SYS_BLE_")
_FRAG = _group("ble_frag", "SYS_BLE_FRAG_")
_STREAM = _group("stream_byte", "SYS_CONNECTOR_STREAM_")


def uuid16(u: int) -> str:
    return f"0000{u:04x}-0000-1000-8000-00805f9b34fb"


SVC, TX, RX, LOGS = (uuid16(_UUID[k]) for k in ("SVC_RUNIT", "CHR_RUNIT_TX", "CHR_RUNIT_RX", "CHR_RUNIT_LOGS"))
FRAG_LAST, FRAG_FIRST, FRAG_MASK = _FRAG["LAST"], _FRAG["FIRST"], _FRAG["COUNT_MASK"]
STREAM_RESPONSE, STREAM_TELEMETRY, STREAM_LOGS, STREAM_ERRORS, STREAM_EVENTS = (
    _STREAM[k] for k in ("RESPONSE", "TELEMETRY", "LOGS", "ERRORS", "EVENTS")
)
#: root keys by name: KEYS["sys"], KEYS["ble"], KEYS["vm"], ...
KEYS = {m["name"].removeprefix("ROUTER_KEY_").lower(): m["value"] for m in _load("common", "enums.json")["enums"]["router_key"]["members"]}
#: the ops of the ble router by id: BLE_OPS["STATUS"], BLE_OPS["LIST"], ...
BLE_OPS = {c["id"]: c["op"] for c in _load("services", "contract_ble.json")["commands"]}
FRAME_MAX = _load("common", "limits.json")["limits"]["ROUTER_FRAME_MAX"]["value"]
#: the longest value one GATT write can carry (BLE Core spec): the largest packet, header included
ATT_VALUE_MAX = 512


def offered_mtu() -> int | None:
    """The MTU the board asks the central for (sdkconfig), or None when there is no sdkconfig."""
    path = ROOT / "sdkconfig"
    m = re.search(r"^CONFIG_BT_NIMBLE_ATT_PREFERRED_MTU=(\d+)", path.read_text(encoding="utf8"), re.M) if path.exists() else None
    return int(m.group(1)) if m else None


def fragment(frame: bytes, packet: int) -> list[bytes]:
    """The packets of a frame, each at most `packet` bytes long, header included."""
    piece = packet - 1
    n = max(1, -(-len(frame) // piece))
    return [
        bytes([(FRAG_FIRST if i == 0 else 0) | (FRAG_LAST if i == n - 1 else 0) | (i & FRAG_MASK)]) + frame[i * piece : (i + 1) * piece]
        for i in range(n)
    ]


class Reassembler:
    """The rules of the firmware's RX side: FIRST starts a frame, a packet out of order drops it up to the next FIRST."""

    def __init__(self, on_frame):
        self.on_frame = on_frame  # (frame, packets)
        self.parts: list[bytes] = []
        self.expected = 0
        self.active = False
        self.errors: list[str] = []

    def feed(self, data: bytes) -> None:
        h = data[0]
        if h & FRAG_FIRST:
            self.parts, self.expected, self.active = [], 0, True
        if not self.active or (h & FRAG_MASK) != self.expected:
            self.errors.append(f"hdr {h:02x} expected {self.expected}")
            self.active = False
            return
        self.parts.append(data[1:])
        self.expected = (self.expected + 1) & FRAG_MASK
        if h & FRAG_LAST:
            frame, packets = b"".join(self.parts), len(self.parts)
            self.parts, self.active = [], False
            self.on_frame(frame, packets)


async def find_board(name: str | None = "runit", address: str | None = None, timeout: float = 10.0):
    """The first board that advertises the runIT service (by address when given, else by name or by the service)."""

    def match(d, a) -> bool:
        if address:
            return d.address.lower() == address.lower()
        return (name is not None and (d.name or "").lower() == name.lower()) or SVC in [u.lower() for u in a.service_uuids]

    return await BleakScanner.find_device_by_filter(match, timeout=timeout)


class BleLink:
    """One connection to a board: requests with their answers, and the frames that are not answers (streams)."""

    def __init__(self, device, packet: int = ATT_VALUE_MAX):
        self.device = device
        self.packet = min(packet, ATT_VALUE_MAX)
        self.client: BleakClient | None = None
        self.lost = asyncio.Event()  # the OS reported the link gone
        self.seq = 0
        self.pending: dict[int, asyncio.Future] = {}
        self.streams: list[tuple[float, int, bytes, int]] = []  # time, stream, payload, packets
        self.tx = Reassembler(self._on_tx)
        self.logs = Reassembler(self._on_logs)

    async def open(self) -> dict[str, float]:
        """Connects and subscribes; the seconds each took."""
        self.lost.clear()
        self.client = BleakClient(self.device, disconnected_callback=lambda _c: self.lost.set())
        t0 = time.perf_counter()
        await self.client.connect()
        connected = time.perf_counter() - t0
        await self.client.start_notify(TX, lambda _c, d: self.tx.feed(bytes(d)))
        await self.client.start_notify(LOGS, lambda _c, d: self.logs.feed(bytes(d)))
        return {"connect_s": connected, "total_s": time.perf_counter() - t0}

    async def close(self) -> None:
        for fut in self.pending.values():
            fut.cancel()
        self.pending.clear()
        if self.client is not None and self.client.is_connected:
            await self.client.disconnect()

    async def __aenter__(self) -> "BleLink":
        await self.open()
        return self

    async def __aexit__(self, *exc) -> None:
        await self.close()

    @property
    def mtu(self) -> int:
        return self.client.mtu_size if self.client else 0

    @property
    def reassembly_errors(self) -> list[str]:
        return self.tx.errors + self.logs.errors

    # ---- frames in ----

    def _on_tx(self, frame: bytes, packets: int) -> None:
        if frame and frame[0] == STREAM_RESPONSE and len(frame) >= 3:
            fut = self.pending.pop(frame[1], None)
            if fut is not None and not fut.done():
                r = Response(frame[1], frame[2], frame[3:], time.time())
                r.packets = packets  # type: ignore[attr-defined]
                fut.set_result(r)
        elif frame:
            self.streams.append((time.time(), frame[0], frame[1:], packets))

    def _on_logs(self, frame: bytes, packets: int) -> None:
        if frame:
            self.streams.append((time.time(), frame[0], frame[1:], packets))

    def streams_since(self, t: float, stream: int) -> list[tuple[bytes, int]]:
        return [(p, n) for (ts, s, p, n) in self.streams if ts >= t and s == stream]

    async def wait_stream(self, t: float, stream: int, pred, timeout: float = 3.0) -> bytes | None:
        end = time.time() + timeout
        while time.time() < end:
            for payload, _ in self.streams_since(t, stream):
                if pred(payload):
                    return payload
            await asyncio.sleep(0.02)
        return None

    # ---- frames out ----

    async def write_packets(self, packets: list[bytes]) -> None:
        for p in packets:
            await self.client.write_gatt_char(RX, p, response=True)

    async def request(self, keys: bytes, payload: bytes = b"", urgent: bool = False, timeout: float = 2.0, packet: int | None = None) -> Response | None:
        """Sends `[seq][flags][keys][payload]`; the answer, or None when nothing came within `timeout`. A write that fails raises."""
        seq = self.seq
        self.seq = (seq + 1) & 0xFF
        fut = asyncio.get_running_loop().create_future()
        self.pending[seq] = fut
        frame = bytes([seq, 1 if urgent else 0]) + keys + payload
        try:
            await self.write_packets(fragment(frame, min(packet or self.packet, ATT_VALUE_MAX)))
            return await asyncio.wait_for(fut, timeout)
        except asyncio.TimeoutError:
            return None
        finally:
            self.pending.pop(seq, None)
