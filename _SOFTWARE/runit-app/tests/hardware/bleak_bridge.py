"""The radio of the hardware tests: bleak behind JSON lines on stdin / stdout, so the TypeScript backend can run against a
real board from Node (Node has no BLE). The backend's own code (framing, commands, link, services) stays in TypeScript;
this file only moves packets. It reads no UUID: the caller sends them from generated/.

  -> {"id": n, "cmd": "find", "service": 65504, "name": "runit", "address": null, "timeout": 10}
  -> {"id": n, "cmd": "open", "address": "...", "service": 65504, "write": 65505, "notify": [65505, 65507]}
  -> {"id": n, "cmd": "write", "data": "hex"}   -> {"id": n, "cmd": "close"}   -> {"id": n, "cmd": "drop"}  (the OS-side loss is faked)
  <- {"id": n, "ok": true, ...} | {"id": n, "error": "..."}
  <- {"event": "packet", "char": 65505, "data": "hex"} | {"event": "closed", "reason": "..."}
"""
import asyncio
import json
import sys
import threading

from bleak import BleakClient, BleakScanner


def uuid16(u: int) -> str:
    return f"0000{u:04x}-0000-1000-8000-00805f9b34fb"


def emit(obj: dict) -> None:
    sys.stdout.write(json.dumps(obj) + "\n")
    sys.stdout.flush()


class Bridge:
    def __init__(self) -> None:
        self.client: BleakClient | None = None
        self.write_char: str | None = None
        self.ours = False

    def on_disconnect(self, _client) -> None:
        if not self.ours:
            emit({"event": "closed", "reason": "The board disconnected."})

    async def find(self, m: dict) -> dict:
        svc = uuid16(m["service"])
        name = (m.get("name") or "").lower()
        address = (m.get("address") or "").lower()

        def match(d, a) -> bool:
            if address:
                return d.address.lower() == address
            return svc in [u.lower() for u in a.service_uuids] or (name != "" and (d.name or "").lower() == name)

        dev = await BleakScanner.find_device_by_filter(match, timeout=m.get("timeout", 10))
        if dev is None:
            raise RuntimeError("no board advertises the runIT service")
        return {"address": dev.address, "name": dev.name}

    async def open(self, m: dict) -> dict:
        self.ours = False
        client = BleakClient(m["address"], disconnected_callback=self.on_disconnect)
        await client.connect()
        try:
            for ch in m["notify"]:
                await client.start_notify(uuid16(ch), lambda _c, d, ch=ch: emit({"event": "packet", "char": ch, "data": bytes(d).hex()}))
        except Exception:
            self.ours = True
            await client.disconnect()
            raise
        self.client, self.write_char = client, uuid16(m["write"])
        return {"mtu": client.mtu_size}

    async def write(self, m: dict) -> dict:
        assert self.client is not None and self.write_char is not None
        await self.client.write_gatt_char(self.write_char, bytes.fromhex(m["data"]), response=True)
        return {}

    async def close(self, _m: dict) -> dict:
        self.ours = True
        if self.client is not None and self.client.is_connected:
            await self.client.disconnect()
        self.client = None
        return {}

    async def drop(self, _m: dict) -> dict:
        """Disconnects as if the OS had lost the link: the loss is reported."""
        if self.client is not None and self.client.is_connected:
            await self.client.disconnect()
        self.client = None
        return {}

    async def handle(self, m: dict) -> None:
        try:
            out = await getattr(self, m["cmd"])(m)
            emit({"id": m["id"], "ok": True, **out})
        except Exception as e:  # noqa: BLE001 - every failure goes back to the caller
            emit({"id": m["id"], "error": f"{type(e).__name__}: {e}"})


async def main() -> None:
    bridge = Bridge()
    loop = asyncio.get_running_loop()
    lines: asyncio.Queue = asyncio.Queue()

    def read_stdin() -> None:  # a thread: pipes on stdin are not readable through the Windows event loop
        for line in sys.stdin:
            loop.call_soon_threadsafe(lines.put_nowait, line)
        loop.call_soon_threadsafe(lines.put_nowait, None)

    threading.Thread(target=read_stdin, daemon=True).start()
    tasks: set[asyncio.Task] = set()
    while (line := await lines.get()) is not None:
        task = asyncio.create_task(bridge.handle(json.loads(line)))
        tasks.add(task)
        task.add_done_callback(tasks.discard)
    await bridge.close({})


if __name__ == "__main__":
    asyncio.run(main())
