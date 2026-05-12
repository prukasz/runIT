import asyncio
from bleak import BleakScanner, BleakClient

DEVICE_NAME = "runit"
UUID_WRITE = "00000000-0000-0000-0000-000000000003"   # TX (write to device)
UUID_READ  = "00000000-0000-0000-0000-000000000002"   # RX (read from device)

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

        # Send test messages every 2 seconds
        counter = 0
        while True:
            try:
                await send_message(client, write_char, f"TEST_{counter:03d}")
                counter += 1
                await asyncio.sleep(0.1)
            except KeyboardInterrupt:
                print("\nDisconnecting...")
                break


if __name__ == "__main__":
    try:
        asyncio.run(main())
    except KeyboardInterrupt:
        print("Interrupted.")
