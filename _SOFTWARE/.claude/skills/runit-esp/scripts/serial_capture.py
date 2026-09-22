"""Non-interactive serial capture: reset the ESP32 via RTS/DTR and print output for N seconds.
Usage: python serial_capture.py <PORT> [seconds] [baud] [--no-reset]
"""
import sys, time
import serial

port = sys.argv[1]
secs = float(sys.argv[2]) if len(sys.argv) > 2 and not sys.argv[2].startswith("--") else 15.0
baud = int(sys.argv[3]) if len(sys.argv) > 3 and not sys.argv[3].startswith("--") else 115200
reset = "--no-reset" not in sys.argv

s = serial.Serial()
s.port, s.baudrate, s.timeout = port, baud, 0.2
s.dtr = False
s.rts = False
s.open()
if reset:
    # EN low (RTS=1, DTR=0) then release -> normal boot (IO0 not held)
    s.dtr = False; s.rts = True; time.sleep(0.2)
    s.rts = False; time.sleep(0.05)

end = time.time() + secs
total = 0
while time.time() < end:
    data = s.read(4096)
    if data:
        total += len(data)
        sys.stdout.write(data.decode("utf-8", errors="replace"))
        sys.stdout.flush()
s.close()
print(f"\n--- captured {total} bytes in {secs:.0f}s on {port} @ {baud} ---")
