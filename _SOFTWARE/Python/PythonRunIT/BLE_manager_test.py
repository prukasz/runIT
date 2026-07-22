import asyncio
import sys
import logging
from typing import Dict, List, Optional, Callable, Union
from bleak import BleakScanner, BleakClient
from bleak.backends.characteristic import BleakGATTCharacteristic

# Configure logger
logging.basicConfig(level=logging.INFO, format="%(asctime)s [%(levelname)s] %(message)s")
logger = logging.getLogger("RunIT_BLE")

def to_short_uuid(uuid_str: str) -> str:
    """Format standard or Bleak UUID to a clean 16-bit hex format (e.g. '0xFFE1')."""
    clean = str(uuid_str).lower().replace("-", "").replace("0x", "")
    if len(clean) == 32 and clean.startswith("0000") and clean.endswith("00805f9b34fb"):
        return f"0x{clean[4:8].upper()}"
    elif len(clean) == 4:
        return f"0x{clean.upper()}"
    elif len(clean) == 8 and clean.startswith("0000"):
        return f"0x{clean[4:8].upper()}"
    return f"0x{clean[:4].upper()}" if len(clean) >= 4 else f"0x{clean.upper()}"

class RunITBLEClient:
    """
    Client class for interacting with runIT BLE devices.
    Dynamically reads 0x2901 descriptors directly from the device firmware.
    """
    def __init__(self, device_name: str = "runit", address: Optional[str] = None):
        self.device_name = device_name
        self.address = address
        self.client: Optional[BleakClient] = None
        self.characteristics: Dict[int, BleakGATTCharacteristic] = {}
        self.char_names: Dict[int, str] = {}
        self.selected_char: Optional[BleakGATTCharacteristic] = None
        self.selected_char_id: Optional[int] = None
        self.on_notification_cb: Optional[Callable[[BleakGATTCharacteristic, bytes], None]] = None

    async def connect(self, timeout: float = 10.0) -> bool:
        """Find and connect to the target BLE device."""
        if self.address:
            print(f"Connecting directly to address: {self.address}...")
            self.client = BleakClient(self.address)
            await self.client.connect()
            return self.client.is_connected

        print(f"Scanning for BLE device with name '{self.device_name}'...")
        device = await BleakScanner.find_device_by_filter(
            lambda d, ad: (d.name and self.device_name.lower() in d.name.lower()) or 
                          (ad.local_name and self.device_name.lower() in ad.local_name.lower()),
            timeout=timeout
        )

        if not device:
            print(f"Error: Device '{self.device_name}' not found.")
            return False

        print(f"Found device: {device.name} [{device.address}]")
        self.client = BleakClient(device)
        await self.client.connect()
        print("Connected successfully!")
        return True

    async def read_user_descriptor(self, char: BleakGATTCharacteristic) -> Optional[str]:
        """Read 0x2901 Characteristic User Description descriptor directly over BLE."""
        for desc in char.descriptors:
            if to_short_uuid(desc.uuid) == "0x2901":
                try:
                    raw_val = await self.client.read_gatt_descriptor(desc.handle)
                    text = raw_val.decode("utf-8", errors="replace").strip()
                    if text:
                        return text
                except Exception as e:
                    logger.debug(f"Failed to read 0x2901 descriptor on {char.uuid}: {e}")
        return None

    async def discover_services(self) -> Dict[int, BleakGATTCharacteristic]:
        """Discover services and dynamically read 0x2901 descriptors directly from device."""
        if not self.client or not self.client.is_connected:
            raise RuntimeError("Client is not connected.")

        self.characteristics.clear()
        self.char_names.clear()
        char_id = 1

        print("\n" + "="*85)
        print(f"{'ID':<4} {'16-Bit UUID':<14} {'Descriptor / Name':<28} {'Properties':<22} {'Handle':<8}")
        print("="*85)

        for service in self.client.services:
            svc_short = to_short_uuid(service.uuid)
            print(f"\n[Service {svc_short}] {service.description}")
            
            for char in service.characteristics:
                short_uuid = to_short_uuid(char.uuid)
                
                # Read 0x2901 descriptor directly from firmware over BLE
                desc_name = await self.read_user_descriptor(char)
                if not desc_name:
                    desc_name = char.description or "Unknown"

                props = ", ".join(char.properties)
                print(f"  {char_id:<3} {short_uuid:<14} {desc_name:<28} {props:<22} {hex(char.handle):<8}")
                
                self.characteristics[char_id] = char
                self.char_names[char_id] = desc_name
                char_id += 1

        print("="*85 + "\n")
        return self.characteristics

    async def enable_all_notifications(self, user_callback: Optional[Callable[[BleakGATTCharacteristic, bytes], None]] = None):
        """Turn on notifications/indications (CCCD) on all supporting characteristics."""
        if not self.client or not self.client.is_connected:
            raise RuntimeError("Client is not connected.")

        self.on_notification_cb = user_callback

        for cid, char in self.characteristics.items():
            if "notify" in char.properties or "indicate" in char.properties:
                short_uuid = to_short_uuid(char.uuid)
                name = self.char_names.get(cid, char.description)
                try:
                    await self.client.start_notify(char.uuid, self._internal_notification_handler)
                    print(f"  [+] Notifications enabled on ID {cid} ({short_uuid} - {name})")
                except Exception as e:
                    print(f"  [-] Failed to enable notifications on ID {cid} ({short_uuid}): {e}")

    def _internal_notification_handler(self, sender: BleakGATTCharacteristic, data: bytearray):
        """Internal handler formatting received notifications in HEX and ASCII text."""
        raw_bytes = bytes(data)
        short_uuid = to_short_uuid(sender.uuid)
        
        # Find matching CID
        cid = next((k for k, v in self.characteristics.items() if v == sender), None)
        name = self.char_names.get(cid, sender.description) if cid else sender.description

        hex_str = " ".join(f"{b:02X}" for b in raw_bytes)
        text_str = raw_bytes.decode("utf-8", errors="replace").replace("\r", "").replace("\n", " ")

        print(f"\n[RX Notification from {short_uuid} ({name})]")
        print(f"  HEX : {hex_str}")
        print(f"  TEXT: {text_str}")

        if self.on_notification_cb:
            self.on_notification_cb(sender, raw_bytes)

    async def send_bytes(self, char_target: Union[BleakGATTCharacteristic, int, str], data: bytes, response: Optional[bool] = None) -> bool:
        """Send raw bytes to a characteristic specified by numerical ID, 16-bit UUID, or name."""
        char = self._resolve_char(char_target)
        if not char:
            print(f"Error: Could not resolve characteristic '{char_target}'")
            return False

        if response is None:
            response = "write" in char.properties

        cid = next((k for k, v in self.characteristics.items() if v == char), None)
        short_uuid = to_short_uuid(char.uuid)
        name = self.char_names.get(cid, char.description) if cid else char.description

        await self.client.write_gatt_char(char, data, response=response)
        hex_sent = " ".join(f"{b:02X}" for b in data)
        print(f"[TX -> {short_uuid} ({name})] Sent {len(data)} bytes | HEX: {hex_sent}")
        return True

    async def send_hex(self, char_target: Union[BleakGATTCharacteristic, int, str], hex_str: str, response: Optional[bool] = None) -> bool:
        """Send a hex string (e.g. '0A 0B FF' or '0A0BFF') to a characteristic."""
        clean_hex = hex_str.replace(" ", "").replace("0x", "")
        try:
            raw_data = bytes.fromhex(clean_hex)
        except ValueError as e:
            print(f"Error: Invalid hex string '{hex_str}': {e}")
            return False

        return await self.send_bytes(char_target, raw_data, response=response)

    async def send_text(self, char_target: Union[BleakGATTCharacteristic, int, str], text_str: str, response: Optional[bool] = None) -> bool:
        """Send an ASCII/UTF-8 string to a characteristic."""
        return await self.send_bytes(char_target, text_str.encode("utf-8"), response=response)

    async def read_char(self, char_target: Union[BleakGATTCharacteristic, int, str]) -> Optional[bytes]:
        """Read data from a readable characteristic."""
        char = self._resolve_char(char_target)
        if not char:
            print(f"Error: Could not resolve characteristic '{char_target}'")
            return None

        if "read" not in char.properties:
            print(f"Warning: Characteristic {char.uuid} does not explicitly list 'read' property.")

        cid = next((k for k, v in self.characteristics.items() if v == char), None)
        short_uuid = to_short_uuid(char.uuid)
        name = self.char_names.get(cid, char.description) if cid else char.description

        data = await self.client.read_gatt_char(char)
        hex_str = " ".join(f"{b:02X}" for b in data)
        text_str = data.decode("utf-8", errors="replace").replace("\r", "").replace("\n", " ")

        print(f"[READ <- {short_uuid} ({name})]")
        print(f"  HEX : {hex_str}")
        print(f"  TEXT: {text_str}")
        return data

    def _resolve_char(self, target: Union[BleakGATTCharacteristic, int, str]) -> Optional[BleakGATTCharacteristic]:
        """Resolve target characteristic by ID, 16-bit UUID (e.g. '0xFFE2'), or description name."""
        if isinstance(target, BleakGATTCharacteristic):
            return target
        if isinstance(target, int) and target in self.characteristics:
            return self.characteristics[target]
        if isinstance(target, str):
            clean_target = target.strip().lower()
            if clean_target.isdigit() and int(clean_target) in self.characteristics:
                return self.characteristics[int(clean_target)]
            for cid, c in self.characteristics.items():
                short_uuid = to_short_uuid(c.uuid).lower()
                name = self.char_names.get(cid, c.description or "").lower()
                if (clean_target in short_uuid or 
                    clean_target.replace("0x", "") in short_uuid or 
                    clean_target in name):
                    return c
        return None

    async def disconnect(self):
        """Disconnect from the BLE device."""
        if self.client and self.client.is_connected:
            await self.client.disconnect()
            print("Disconnected.")

async def interactive_cli(client: RunITBLEClient):
    """Command-line interface for selecting characteristics and sending commands."""
    loop = asyncio.get_running_loop()
    print("\n--- Interactive BLE 16-Bit Characteristic Interface ---")
    print("Commands:")
    print("  list / ls                     : List 16-bit UUIDs, descriptor names, and IDs")
    print("  select <id|uuid|name> / s ... : Select characteristic (e.g. 's 6', 's FFE2', 's rx')")
    print("  hex <data> / h <data>         : Send hex string to selected characteristic (e.g. 'h 0A0BFF')")
    print("  text <msg> / t <msg>          : Send text string to selected characteristic (e.g. 't hello')")
    print("  read [id] / r [id]            : Read characteristic data")
    print("  quit / q                      : Disconnect and exit\n")

    while True:
        try:
            if client.selected_char:
                short_uuid = to_short_uuid(client.selected_char.uuid)
                name = client.char_names.get(client.selected_char_id, client.selected_char.description)
                sel_desc = f"ID:{client.selected_char_id} ({short_uuid} - {name})"
            else:
                sel_desc = "None"

            user_input = await loop.run_in_executor(None, input, f"RunIT [{sel_desc}]> ")
            if user_input is None:
                continue

            cmd_line = user_input.strip()
            if not cmd_line:
                continue

            parts = cmd_line.split(maxsplit=1)
            cmd = parts[0].lower()
            args = parts[1] if len(parts) > 1 else ""

            if cmd in ("q", "quit", "exit"):
                break

            elif cmd in ("ls", "list"):
                await client.discover_services()

            elif cmd in ("s", "select"):
                if not args:
                    print("Usage: select <id | 16-bit UUID (e.g. FFE2) | name (e.g. rx)>")
                    continue
                char = client._resolve_char(args)
                if char:
                    client.selected_char = char
                    cid = [k for k, v in client.characteristics.items() if v == char][0]
                    client.selected_char_id = cid
                    name = client.char_names.get(cid, char.description)
                    print(f"Selected Characteristic ID {cid}: {to_short_uuid(char.uuid)} ({name})")
                else:
                    print(f"Characteristic '{args}' not found.")

            elif cmd in ("h", "hex"):
                if not client.selected_char:
                    print("Error: No characteristic selected. Use 'select <id>' first.")
                    continue
                if not args:
                    print("Usage: hex <hex_string>")
                    continue
                await client.send_hex(client.selected_char, args)

            elif cmd in ("t", "text"):
                if not client.selected_char:
                    print("Error: No characteristic selected. Use 'select <id>' first.")
                    continue
                if not args:
                    print("Usage: text <string>")
                    continue
                await client.send_text(client.selected_char, args)

            elif cmd in ("r", "read"):
                target = args if args else client.selected_char
                if not target:
                    print("Usage: read <id|uuid|name> or select a characteristic first.")
                    continue
                await client.read_char(target)

            else:
                print(f"Unknown command '{cmd}'. Type 'list', 'select', 'hex', 'text', 'read', or 'quit'.")

        except KeyboardInterrupt:
            break
        except Exception as e:
            print(f"Command Error: {e}")

async def main():
    client = RunITBLEClient(device_name="runit")
    if not await client.connect():
        return

    try:
        await client.discover_services()
        print("Enabling notifications on all supporting characteristics...")
        await client.enable_all_notifications()
        
        # Start interactive CLI
        await interactive_cli(client)
    finally:
        await client.disconnect()

if __name__ == "__main__":
    try:
        asyncio.run(main())
    except KeyboardInterrupt:
        print("\nExiting...")