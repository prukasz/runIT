"""Boot capture over the ESP32-S3 native USB (USB-Serial-JTAG, e.g. the runIT PCB on COM4).

A reset re-enumerates the USB port, so the handle opened before the reset dies.
This resets the chip (RTS pulse, like esptool's hard reset), then reopens the
port as soon as it comes back and reads for N seconds.
Usage: python usb_capture.py <PORT> [seconds] [--no-reset]
"""
import sys
import time

import serial

port = sys.argv[1]
secs = float(sys.argv[2]) if len(sys.argv) > 2 and not sys.argv[2].startswith("--") else 8.0

if "--no-reset" not in sys.argv:
    s = serial.Serial(port, 115200, timeout=0.1)
    s.dtr = False
    s.rts = True
    s.dtr = s.dtr  # usbser.sys applies RTS only together with DTR
    time.sleep(0.1)
    s.rts = False
    s.dtr = s.dtr
    s.close()
    time.sleep(0.2)

deadline = time.time() + 5.0
s = None
while time.time() < deadline:
    try:
        s = serial.Serial(port, 115200, timeout=0.1)
        break
    except serial.SerialException:
        time.sleep(0.05)
if s is None:
    print(f"--- {port} did not come back within 5 s ---")
    sys.exit(1)

end = time.time() + secs
total = 0
while time.time() < end:
    try:
        data = s.read(4096)
    except serial.SerialException:
        s.close()
        time.sleep(0.1)
        try:
            s = serial.Serial(port, 115200, timeout=0.1)
        except serial.SerialException:
            pass
        continue
    if data:
        total += len(data)
        sys.stdout.write(data.decode("utf-8", errors="replace"))
        sys.stdout.flush()
s.close()
print(f"\n--- captured {total} bytes in {secs:.0f}s on {port} ---")
