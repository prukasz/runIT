"""The board's console, read beside the BLE link: every log line as JSON on stdout, so a test can see what the firmware
logged (a panic, a boot, the safe state) independently of the link under test. The console is a binary line: the tap opens
a client session on it (router_link.py of the runit-esp skill) and takes the LOGS stream, plus the text that is no frame (the
ROM, a panic). The port opens with DTR / RTS low so opening does not reset the board; {"cmd": "reset"} pulses EN on purpose.

  -> {"cmd": "reset"}
  <- {"event": "line", "at": 1712345678.9, "text": "I (1234) ..."}
"""
import json
import sys
import threading
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[3] / ".claude" / "skills" / "runit-esp" / "scripts"))
from router_link import Link  # noqa: E402


def emit(obj: dict) -> None:
    sys.stdout.write(json.dumps(obj) + "\n")
    sys.stdout.flush()


def main() -> None:
    link = Link(sys.argv[1])

    def commands() -> None:
        for line in sys.stdin:
            if json.loads(line).get("cmd") == "reset":
                link.ser.rts = True  # EN low
                time.sleep(0.1)
                link.ser.rts = False

    threading.Thread(target=commands, daemon=True).start()
    sent = 0
    while True:
        time.sleep(0.02)
        with link._lock:
            fresh = link.logs[sent:]
            sent = len(link.logs)
        for at, text in fresh:
            emit({"event": "line", "at": at, "text": text})


if __name__ == "__main__":
    main()
