import asyncio
from bleak import BleakScanner, BleakClient

DEVICE_NAME = "runit"
UUID_WRITE = "00000000-0000-0000-0000-000000000003"   # TX (write to device)
UUID_READ  = "00000000-0000-0000-0000-000000000002"   # RX (read from device)}]

async def rx_handler(sender, data: bytearray):
    """Handle received notifications."""
    print(f"[RX] {len(data)} bytes: {data.hex().upper()}")
    try:
        print(f"[ASCII] {data.decode('utf-8', errors='replace')}")
    except:
        pass

async def send_message(client, write_char, message: str):
    """Send a message to the device."""
    data = message.encode('utf-8')
    # vm_in is configured as WRITE on firmware side, so use write-with-response.
    await client.write_gatt_char(write_char, data, response=True)
    print(f"[TX] Sent: {message} ({data.hex().upper()})")


async def send_bytes(client, write_char, data: bytes):
    """Send raw bytes to the device."""
    await client.write_gatt_char(write_char, data, response=True)
    print(f"[TX] Sent bytes: {data.hex().upper()}")

async def main():
    print("Scanning for device...")
    device = await BleakScanner.find_device_by_filter(
        lambda d, ad: d.name == DEVICE_NAME or ad.local_name == DEVICE_NAME,
        timeout=10
    )

    if not device:
        print("Device not found.")
        return

    print(f"Found: {device.name} ({device.address})")

    async with BleakClient(device) as client:
        print("Connected")

        # Start receiving notifications
        read_char = client.services.get_characteristic(UUID_READ)
        await client.start_notify(UUID_READ, rx_handler)
        write_char = client.services.get_characteristic(UUID_WRITE)
        if write_char is None or read_char is None:
            print("Required characteristics not found.")
            return
        print(f"Notify on: {UUID_READ}")
        print(f"Write to : {UUID_WRITE}")

        # Interactive mode: send a single hex packet (or multiple) instead of spamming
        print("Enter hex bytes to send (e.g. '0A0BFF'), or 'q' to quit.")
        loop = asyncio.get_running_loop()
        while True:
            try:
                user = await loop.run_in_executor(None, input, "> ")
                if user is None:
                    continue
                user = user.strip()
                if user.lower() in ('q', 'quit', 'exit'):
                    print("Disconnecting...")
                    break
                # allow spaces in hex input
                hexstr = user.replace(' ', '')
                if len(hexstr) == 0:
                    continue
                try:
                    b = bytes.fromhex(hexstr)
                except ValueError:
                    print("Invalid hex. Example valid input: 0A 0B FF")
                    continue
                await send_bytes(client, write_char, b)
            except KeyboardInterrupt:
                print("\nDisconnecting...")
                break


if __name__ == "__main__":
    try:
        asyncio.run(main())
    except KeyboardInterrupt:
        print("Interrupted.")
        