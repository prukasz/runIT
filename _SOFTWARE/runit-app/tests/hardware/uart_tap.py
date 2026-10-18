"""The board's console, read beside the BLE link: every line as JSON on stdout, so a test can see what the firmware logged
(a panic, a boot, the safe state) independently of the link under test. The port opens with DTR / RTS low so opening does not
reset the board; {"cmd": "reset"} pulses EN on purpose.

  -> {"cmd": "reset"}
  <- {"event": "line", "at": 1712345678.9, "text": "I (1234) ..."}
"""
import json
import sys
import threading
import time

import serial


def emit(obj: dict) -> None:
    sys.stdout.write(json.dumps(obj) + "\n")
    sys.stdout.flush()


def main() -> None:
    ser = serial.serial_for_url(sys.argv[1], baudrate=115200, timeout=0.05, do_not_open=True)
    ser.rts = False
    ser.dtr = False
    ser.open()

    def commands() -> None:
        for line in sys.stdin:
            if json.loads(line).get("cmd") == "reset":
                ser.rts = True  # EN low
                time.sleep(0.1)
                ser.rts = False

    threading.Thread(target=commands, daemon=True).start()
    buf = b""
    while True:
        try:
            buf += ser.read(512)
        except (serial.SerialException, OSError):  # the native USB port re-enumerates on a reset: wait for it
            ser.close()
            while True:
                time.sleep(0.2)
                try:
                    ser.open()
                    break
                except (serial.SerialException, OSError):
                    pass
            continue
        while b"\n" in buf:
            raw, buf = buf.split(b"\n", 1)
            emit({"event": "line", "at": time.time(), "text": raw.decode("utf8", "replace").rstrip("\r")})


if __name__ == "__main__":
    main()
