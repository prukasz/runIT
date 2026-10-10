"""Router / streams test suite over the console link.

  QEMU:   python router_tests.py --port socket://localhost:5555 --target qemu
  Board:  python router_tests.py --port COM5

Checks the router (keys, payload sizes, error records, urgent path,
dead-man), the sys / i2c / conn / ble routers, and the errors stream.
Target "qemu" skips what QEMU can't do (BLE controller, I2C devices).
"""

from __future__ import annotations

import argparse
import struct
import sys
import time
import zlib

from router_link import (
    KEY_ACTIONS,
    KEY_BLE,
    KEY_CONN,
    KEY_DEVICE,
    KEY_I2C,
    KEY_SYS,
    TRANSPORT_BLE,
    TRANSPORT_UART,
    Link,
    Response,
)

RESULTS: list[tuple[str, bool, str]] = []


def check(name: str, ok: bool, detail: str = "") -> bool:
    RESULTS.append((name, ok, detail))
    print(f"{'PASS' if ok else 'FAIL'}  {name}" + (f"  -- {detail}" if detail and not ok else ""))
    return ok


def err_cause(r: Response | None) -> str:
    return r.record.cause.name if r and r.record and r.record.cause else ""


def wait_boot(link: Link, timeout: float) -> bool:
    return link.wait_log("runIT up", timeout)


def ping(link: Link, urgent: bool = False) -> int | None:
    r = link.request(bytes([KEY_SYS, 0x01]), urgent=urgent)
    return struct.unpack("<I", r.data)[0] if r and r.ok and len(r.data) == 4 else None


# ---------------------------------------------------------------------------


def test_sys(link: Link) -> None:
    t1 = ping(link)
    check("sys PING (normal)", t1 is not None)
    t2 = ping(link, urgent=True)
    check("sys PING (urgent)", t2 is not None)
    check("uptime grows", t1 is not None and t2 is not None and t2 >= t1, f"{t1} -> {t2}")

    r = link.request(bytes([KEY_SYS, 0x01]), payload=b"\x00")
    check("PING with payload -> ERR_CMD_BAD_LEN", err_cause(r) == "ERR_CMD_BAD_LEN", str(r))

    r = link.request(bytes([KEY_SYS, 0x05]))
    ok = r is not None and r.ok and len(r.data) == 5
    check("DEADMAN_GET", ok, str(r))
    if ok:
        ms, on_lost = struct.unpack("<IB", r.data)
        check("dead-man off at boot", ms == 0 and on_lost == 0, f"{ms} {on_lost}")


def test_router_errors(link: Link) -> None:
    r = link.request(bytes([0x7E]))
    rec = r.record if r else None
    check("unknown root key -> ERR_ROUTER_UNKNOWN_KEY", err_cause(r) == "ERR_ROUTER_UNKNOWN_KEY", str(r))
    if rec:
        check("record: src command, link UART", rec.src == 1 and rec.link == TRANSPORT_UART, str(rec))
        check("record: echoes seq and request", rec.seq == r.seq and rec.request == bytes([0x7E]), str(rec))

    r = link.request(bytes([KEY_SYS, 0x7E]))
    names = r.record.names() if r and r.record else []
    check(
        "unknown op -> route node + ERR_ROUTER_UNKNOWN_KEY",
        names == ["ERR_ROUTER_KEY", "ERR_ROUTER_UNKNOWN_KEY"],
        str(r),
    )

    # Frame without a root key: rejected by router_submit, answered all the same.
    seq = link.next_seq()
    link.send_frame(bytes([seq, 0x00]))
    r = link.wait_response(seq)
    check("frame without key -> ERR_ROUTER_FRAME_LEN", err_cause(r) == "ERR_ROUTER_FRAME_LEN", str(r))


def test_errors_stream(link: Link) -> None:
    t = link.mark()
    r = link.request(bytes([KEY_SYS, 0x7D]))
    time.sleep(0.5)
    recs = [x for x in link.error_records_since(t) if x.src == 1 and r and x.seq == r.seq]
    check("command error also on the errors stream", len(recs) == 1, f"{len(link.error_records_since(t))} record(s)")
    if recs and r and r.record:
        check("stream record == response record", recs[0].names() == r.record.names())


def test_deadman(link: Link) -> None:
    t = link.mark()
    r = link.request(bytes([KEY_SYS, 0x04]), payload=struct.pack("<IB", 300, 0))
    check("DEADMAN_SET 300 ms", r is not None and r.ok, str(r))
    fired = link.wait_log("safe state", 2.0, since=t)
    check("dead-man fires without frames", fired)
    r = link.request(bytes([KEY_SYS, 0x04]), payload=struct.pack("<IB", 0, 0))
    check("DEADMAN_SET off", r is not None and r.ok, str(r))
    r = link.request(bytes([KEY_SYS, 0x04]), payload=struct.pack("<IB", 0, 2))
    check("DEADMAN_SET bad flag -> ERR_CMD_BAD_ARG", err_cause(r) == "ERR_CMD_BAD_ARG", str(r))


def test_stop(link: Link) -> None:
    t = link.mark()
    r = link.request(bytes([KEY_SYS, 0x02]), urgent=True)
    check("STOP (urgent) answered", r is not None and r.ok, str(r))
    check("STOP runs the safe-state handler", link.wait_log("safe state", 1.0, since=t))


def test_i2c(link: Link, target: str) -> None:
    r = link.request(bytes([KEY_I2C, 0x01]))
    ok = r is not None and r.ok and len(r.data) == 2 * 19
    check("i2c INFO (2 buses)", ok, str(r))
    if ok:
        for b in range(2):
            flags, sda, scl = r.data[b * 19], r.data[b * 19 + 1], r.data[b * 19 + 2]
            print(f"      bus {b}: flags {flags:02x} sda {sda} scl {scl}")
    r = link.request(bytes([KEY_I2C, 0x02]), payload=b"\x00")
    check("i2c SCAN bus 0 -> ERR_I2C_REMOTE_DENIED", err_cause(r) == "ERR_I2C_REMOTE_DENIED", str(r))
    r = link.request(bytes([KEY_I2C, 0x02]), payload=b"\x01", timeout=10.0)
    if r and r.ok:
        found = [a for a in range(128) if r.data[a // 8] >> (a % 8) & 1]
        check("i2c SCAN bus 1", True)
        print("      found:", [hex(a) for a in found])
    else:
        check("i2c SCAN bus 1 answered", r is not None, str(r))
        print("      ", r)


def test_conn(link: Link) -> None:
    def info() -> dict[int, dict]:
        r = link.request(bytes([KEY_CONN, 0x07]))
        out = {}
        if not (r and r.ok):
            return out
        d, pos = r.data, 1
        for _ in range(d[0]):
            cid, stream, flags, n = d[pos : pos + 4]
            pos += 4
            binds = []
            for _ in range(n):
                prov, ep = struct.unpack_from("<BI", d, pos)
                pos += 5
                binds.append((prov, ep))
            end = d.index(0, pos)
            out[cid] = {"stream": stream, "flags": flags, "binds": binds, "name": d[pos:end].decode()}
            pos = end + 1
        return out

    i = info()
    check("conn INFO: 4 system connectors", all(k in i for k in (0, 1, 2, 3)), str(i))
    if 1 in i:
        check("errors -> UART + BLE 0xFFE3", sorted(i[1]["binds"]) == [(TRANSPORT_UART, 0), (TRANSPORT_BLE, 0xFFE3)], str(i[1]))
    if 3 in i:
        check("events: stream 0x06 -> UART + BLE 0xFFE1", i[3]["name"] == "events" and i[3]["stream"] == 0x06
              and sorted(i[3]["binds"]) == [(TRANSPORT_UART, 0), (TRANSPORT_BLE, 0xFFE1)], str(i[3]))

    r = link.request(bytes([KEY_CONN, 0x01]), payload=bytes([3, 0x10]) + b"temp\0")
    check("conn CREATE on the events ID -> ERR_CONN_PROTECTED", err_cause(r) == "ERR_CONN_PROTECTED", str(r))
    r = link.request(bytes([KEY_CONN, 0x01]), payload=bytes([4, 0x10]) + b"temp\0")
    check("conn CREATE user 4", r is not None and r.ok, str(r))
    r = link.request(bytes([KEY_CONN, 0x03]), payload=struct.pack("<BBI", 4, TRANSPORT_UART, 0))
    check("conn BIND 4 -> UART", r is not None and r.ok, str(r))
    i = info()
    check("conn INFO shows 'temp'", 4 in i and i[4]["name"] == "temp" and i[4]["binds"] == [(TRANSPORT_UART, 0)], str(i.get(4)))
    r = link.request(bytes([KEY_CONN, 0x01]), payload=bytes([1, 0x10]) + b"x\0")
    check("conn CREATE on a system ID -> ERR_CONN_PROTECTED", err_cause(r) == "ERR_CONN_PROTECTED", str(r))
    r = link.request(bytes([KEY_CONN, 0x02]), payload=bytes([1]))
    check("conn REMOVE errors -> ERR_CONN_PROTECTED", err_cause(r) == "ERR_CONN_PROTECTED", str(r))
    r = link.request(bytes([KEY_CONN, 0x02]), payload=bytes([4]))
    check("conn REMOVE user 4", r is not None and r.ok, str(r))
    check("conn INFO: 4 gone", 4 not in info())


def test_ble(link: Link, target: str) -> None:
    r = link.request(bytes([KEY_BLE, 0x06]))
    ok = r is not None and r.ok and len(r.data) == 7
    check("ble STATUS", ok, str(r))
    if ok:
        conn, mtu, ovf = struct.unpack("<BHI", r.data)
        print(f"      connected {conn} mtu {mtu} rx_overflow {ovf}")
    r = link.request(bytes([KEY_BLE, 0x03]), payload=struct.pack("<HHBBBBII", 0xFFE0, 0xEE01, 0, 0, 1, 0, 96, 0) + b"temp\0")
    check("ble CHAR_CREATE on locked runIT service -> ERR_BLE_SVC_LOCKED", err_cause(r) == "ERR_BLE_SVC_LOCKED", str(r))
    if target == "qemu":
        return
    r = link.request(bytes([KEY_BLE, 0x01]), payload=struct.pack("<HB", 0xEE00, 1))
    check("ble SVC_CREATE 0xEE00", r is not None and r.ok, str(r))
    r = link.request(bytes([KEY_BLE, 0x03]), payload=struct.pack("<HHBBBBII", 0xEE00, 0xEE01, 0, 0, 1, 0, 96, 0) + b"temp\0")
    check("ble CHAR_CREATE 0xEE01", r is not None and r.ok, str(r))
    r = link.request(bytes([KEY_BLE, 0x03]), payload=struct.pack("<HHBBBBII", 0xEE00, 0xEE02, 1, 0, 0, 0, 0, 96) + b"cmd\0")
    check("ble CHAR_CREATE 0xEE02", r is not None and r.ok, str(r))
    r = link.request(bytes([KEY_BLE, 0x04]), payload=struct.pack("<HH", 0xEE00, 0xEE02))
    check("ble CHAR_REMOVE 0xEE02 before APPLY (freed now)", r is not None and r.ok, str(r))
    r = link.request(bytes([KEY_BLE, 0x05]))
    check("ble APPLY", r is not None and r.ok, str(r))
    r = link.request(bytes([KEY_BLE, 0x04]), payload=struct.pack("<HH", 0xEE00, 0xEE01))
    check("ble CHAR_REMOVE 0xEE01 after APPLY (pending)", r is not None and r.ok, str(r))
    r = link.request(bytes([KEY_BLE, 0x04]), payload=struct.pack("<HH", 0xEE00, 0xEE01))
    check("ble CHAR_REMOVE pending again -> ERR_BLE_NOT_FOUND", err_cause(r) == "ERR_BLE_NOT_FOUND", str(r))
    r = link.request(bytes([KEY_BLE, 0x05]))
    check("ble APPLY (char remove)", r is not None and r.ok, str(r))
    r = link.request(bytes([KEY_BLE, 0x03]), payload=struct.pack("<HHBBBBII", 0xEE00, 0xEE01, 0, 0, 1, 0, 96, 0) + b"temp\0")
    check("ble CHAR_CREATE 0xEE01 again after sync", r is not None and r.ok, str(r))
    r = link.request(bytes([KEY_BLE, 0x02]), payload=struct.pack("<H", 0xEE00))
    check("ble SVC_REMOVE 0xEE00", r is not None and r.ok, str(r))
    r = link.request(bytes([KEY_BLE, 0x05]))
    check("ble APPLY (remove)", r is not None and r.ok, str(r))


PCA_TYPE = 0x41
# Ops are u16: high byte = contract (0x00 device, 0x01 IO, 0xFF the device's own), low byte = op.
OP_CREATE, OP_RESET, OP_SUSPEND, OP_RESUME, OP_REMOVE, OP_INFO, OP_OPS = 0x0000, 0x0001, 0x0002, 0x0003, 0x0004, 0x0005, 0x0006
IO_SET_LEVEL, IO_TOGGLE, IO_GET_LEVEL, IO_SET_DUTY, IO_SET_FREQ = 0x0100, 0x0101, 0x0102, 0x0103, 0x0104
IO_SET_MODE, IO_RESET, IO_CONFIGURE_INTR, IO_GET_VOLTAGE, IO_SET_VOLTAGE = 0x0105, 0x0106, 0x0107, 0x0108, 0x0109
PINS_AVAILABLE, PINS_LOCKED, PINS_LOCK, PINS_UNLOCK, PINS_SET_LOCKED_LEVEL = 0x0200, 0x0201, 0x0202, 0x0203, 0x0204
PCA_ID = 3


def dev(link: Link, dev_id: int, op: int, payload: bytes = b"") -> Response | None:
    return link.request(bytes([KEY_DEVICE]) + struct.pack("<HH", dev_id, op), payload=payload)


PIN_NONE = struct.pack("<HB", 0xFFFF, 0xFF)
PCA_BUS, PCA_ADDR = 0, 0x60  # the onboard PCA9685 (HARDWARE.md); QEMU and a bare devkit have none


def create_pca(link: Link, dev_id: int, bus: int, addr: int, oe: bytes = PIN_NONE) -> Response | None:
    return dev(link, dev_id, OP_CREATE, struct.pack("<H", PCA_TYPE) + bytes([bus, addr]) + oe)


GPIO_TYPE = 0x40
GPIO_ID = 0
GPIO_VALID = 0x1FFFFFFFFFFFF & ~(0xF << 22)  # ESP32-S3: GPIO 22-25 don't exist
OUT_PIN, PWM_PIN = 4, 5  # free on the devkit and the runIT board


def test_gpio(link: Link, target: str) -> None:
    r = dev(link, GPIO_ID, OP_CREATE, struct.pack("<H", GPIO_TYPE))
    check(f"CREATE GPIO ESP id {GPIO_ID}", r is not None and r.ok, str(r))
    if r is None or not r.ok:
        return
    r = dev(link, 1, OP_CREATE, struct.pack("<H", GPIO_TYPE))
    check("second GPIO ESP -> ERR_DEV_ID_TAKEN", err_cause(r) == "ERR_DEV_ID_TAKEN", str(r))
    r = dev(link, GPIO_ID, OP_INFO)
    check("INFO: type 0x0040", r is not None and r.ok and r.data == struct.pack("<HB", GPIO_TYPE, 0), str(r))
    r = dev(link, GPIO_ID, OP_OPS)
    ops = set(struct.unpack(f"<{len(r.data) // 2}H", r.data)) if r is not None and r.ok else set()
    check("OPS: SET_MODE, GET_VOLTAGE, CONFIGURE_INTR, PINS; no SET_VOLTAGE",
          {IO_SET_MODE, IO_GET_VOLTAGE, IO_CONFIGURE_INTR, PINS_LOCK} <= ops and IO_SET_VOLTAGE not in ops, str(sorted(ops)))
    r = dev(link, GPIO_ID, PINS_AVAILABLE)
    check("PINS AVAILABLE = the S3's valid GPIOs", r is not None and r.ok and r.data == struct.pack("<Q", GPIO_VALID), str(r))
    r = dev(link, GPIO_ID, IO_SET_MODE, bytes([22, 3]))
    check("GPIO 22 (doesn't exist) -> ERR_DEV_BAD_PIN", err_cause(r) == "ERR_DEV_BAD_PIN", str(r))
    r = dev(link, GPIO_ID, IO_SET_LEVEL, bytes([OUT_PIN, 1]))
    check("write of an unconfigured pin -> ERR_IO_PIN_UNCONFIGURED", err_cause(r) == "ERR_IO_PIN_UNCONFIGURED", str(r))

    r = dev(link, GPIO_ID, IO_SET_MODE, bytes([OUT_PIN, 3]))
    check("SET_MODE push-pull", r is not None and r.ok, str(r))
    r = dev(link, GPIO_ID, IO_SET_MODE, bytes([OUT_PIN, 3]))
    check("SET_MODE of a configured pin -> ERR_IO_PIN_IN_USE", err_cause(r) == "ERR_IO_PIN_IN_USE", str(r))
    r = dev(link, GPIO_ID, IO_SET_MODE, bytes([PWM_PIN, 8]))
    check("SET_MODE DAC -> ERR_IO_MODE_UNSUPPORTED", err_cause(r) == "ERR_IO_MODE_UNSUPPORTED", str(r))
    r = dev(link, GPIO_ID, IO_SET_LEVEL, bytes([OUT_PIN, 1]))
    check("SET_LEVEL 1", r is not None and r.ok, str(r))
    r = dev(link, GPIO_ID, IO_GET_LEVEL, bytes([OUT_PIN]))
    check("GET_LEVEL = 1", r is not None and r.ok and r.data == bytes([1]), str(r))
    r = dev(link, GPIO_ID, IO_TOGGLE, bytes([OUT_PIN]))
    check("TOGGLE", r is not None and r.ok, str(r))
    r = dev(link, GPIO_ID, IO_GET_LEVEL, bytes([OUT_PIN]))
    check("GET_LEVEL = 0", r is not None and r.ok and r.data == bytes([0]), str(r))
    test_gpio_intr(link, target)

    r = dev(link, GPIO_ID, IO_SET_MODE, bytes([PWM_PIN, 6]))
    check("SET_MODE PWM", r is not None and r.ok, str(r))
    r = dev(link, GPIO_ID, IO_SET_LEVEL, bytes([PWM_PIN, 1]))
    check("SET_LEVEL of a PWM pin -> ERR_IO_WRONG_MODE", err_cause(r) == "ERR_IO_WRONG_MODE", str(r))
    r = dev(link, GPIO_ID, IO_SET_FREQ, struct.pack("<BI", PWM_PIN, 4))
    check("PWM 4 Hz -> ERR_DEV_RANGE", err_cause(r) == "ERR_DEV_RANGE", str(r))
    r = dev(link, GPIO_ID, IO_SET_FREQ, struct.pack("<BI", PWM_PIN, 1000))
    check("SET_PWM_FREQ 1 kHz", r is not None and r.ok, str(r))
    r = dev(link, GPIO_ID, IO_SET_DUTY, struct.pack("<BI", PWM_PIN, 4096))
    check("duty 4096 -> ERR_DEV_RANGE", err_cause(r) == "ERR_DEV_RANGE", str(r))
    r = dev(link, GPIO_ID, IO_SET_DUTY, struct.pack("<BI", PWM_PIN, 2048))
    check("SET_PWM_DUTY 2048", r is not None and r.ok, str(r))

    r = dev(link, GPIO_ID, PINS_LOCK, bytes([OUT_PIN]))
    check("PINS LOCK", r is not None and r.ok, str(r))
    r = dev(link, GPIO_ID, IO_SET_LEVEL, bytes([OUT_PIN, 1]))
    check("write of a locked pin -> ERR_DEV_PIN_LOCKED", err_cause(r) == "ERR_DEV_PIN_LOCKED", str(r))
    r = dev(link, GPIO_ID, PINS_SET_LOCKED_LEVEL, bytes([OUT_PIN, 1]))
    check("SET_LOCKED_LEVEL drives it", r is not None and r.ok, str(r))

    r = dev(link, GPIO_ID, OP_SUSPEND)
    check("SUSPEND", r is not None and r.ok, str(r))
    r = dev(link, GPIO_ID, IO_SET_DUTY, struct.pack("<BI", PWM_PIN, 100))
    check("write while suspended -> ERR_DEV_SUSPENDED", err_cause(r) == "ERR_DEV_SUSPENDED", str(r))
    r = dev(link, GPIO_ID, PINS_SET_LOCKED_LEVEL, bytes([OUT_PIN, 0]))
    check("the owner still drives its locked pin while suspended", r is not None and r.ok, str(r))
    r = dev(link, GPIO_ID, OP_RESUME)
    check("RESUME", r is not None and r.ok, str(r))
    r = dev(link, GPIO_ID, IO_SET_DUTY, struct.pack("<BI", PWM_PIN, 100))
    check("write after RESUME", r is not None and r.ok, str(r))

    r = dev(link, GPIO_ID, OP_RESET)
    check("RESET releases the unlocked pins", r is not None and r.ok, str(r))
    r = dev(link, GPIO_ID, IO_SET_DUTY, struct.pack("<BI", PWM_PIN, 100))
    check("PWM pin is unconfigured after RESET", err_cause(r) == "ERR_IO_PIN_UNCONFIGURED", str(r))
    r = dev(link, GPIO_ID, IO_GET_LEVEL, bytes([OUT_PIN]))
    check("the locked pin survived RESET", r is not None and r.ok, str(r))

    r = dev(link, GPIO_ID, OP_REMOVE)
    check("REMOVE GPIO ESP", r is not None and r.ok, str(r))
    r = dev(link, GPIO_ID, OP_INFO)
    check("INFO after REMOVE -> ERR_DEV_NOT_FOUND", err_cause(r) == "ERR_DEV_NOT_FOUND", str(r))


def intr_cfg(mode: int, debounce: int = 0, up: int = 0, down: int = 0, hyst: int = 0, count: int = 0) -> bytes:
    return struct.pack("<BBHHHH", mode, debounce, up, down, hyst, count)


INTR_DISABLE, INTR_RISING, INTR_FALLING, INTR_BOTH, INTR_OUTSIDE = 0, 1, 2, 3, 4


def test_gpio_intr(link: Link, target: str) -> None:
    """OUT_PIN is a push-pull output at level 0: the GPIO matrix sees its own edges, so ISR -> event -> rule -> action
    runs without a wire. The rule's action creates connector EV_CONN."""
    r = dev(link, GPIO_ID, IO_CONFIGURE_INTR, bytes([PWM_PIN]) + intr_cfg(INTR_FALLING))
    check("CONFIGURE_INTR of an unconfigured pin -> ERR_IO_PIN_UNCONFIGURED", err_cause(r) == "ERR_IO_PIN_UNCONFIGURED", str(r))
    r = dev(link, GPIO_ID, IO_CONFIGURE_INTR, bytes([OUT_PIN]) + intr_cfg(9))
    check("CONFIGURE_INTR mode 9 -> ERR_CMD_BAD_ARG", err_cause(r) == "ERR_CMD_BAD_ARG", str(r))
    r = dev(link, GPIO_ID, IO_CONFIGURE_INTR, bytes([OUT_PIN]) + intr_cfg(INTR_OUTSIDE, up=2000, down=1000))
    check("ADC window on a digital pin -> ERR_IO_FEATURE_UNSUPPORTED", err_cause(r) == "ERR_IO_FEATURE_UNSUPPORTED", str(r))
    r = dev(link, GPIO_ID, IO_CONFIGURE_INTR, bytes([OUT_PIN]) + intr_cfg(INTR_FALLING))
    check("CONFIGURE_INTR falling edge", r is not None and r.ok, str(r))
    if target == "qemu":  # no GPIO readback, so no edges reach the ISR
        r = dev(link, GPIO_ID, IO_CONFIGURE_INTR, bytes([OUT_PIN]) + intr_cfg(INTR_DISABLE))
        check("CONFIGURE_INTR disable", r is not None and r.ok, str(r))
        return

    ev(link, E_UNSUB, b"\xff")
    link.request(bytes([KEY_CONN, 0x02]), payload=bytes([EV_CONN]))
    r = upload(link, EV_ACTION, "mk conn", image(bytes([KEY_CONN, 0x01, EV_CONN, 0x11]) + b"evt\0"))
    check("upload action 10 (creates connector 5)", r is not None and r.ok, str(r))
    r = ev(link, E_SUB, rule(2, GPIO_ID, IO_FALLING, OUT_PIN, EV_ACTION))
    check("rule: GPIO ESP falling edge on OUT_PIN -> action 10", r is not None and r.ok, str(r))

    dev(link, GPIO_ID, IO_SET_LEVEL, bytes([OUT_PIN, 1]))
    time.sleep(0.4)
    check("rising edge: falling-edge rule silent", EV_CONN not in conn_ids(link))
    dev(link, GPIO_ID, IO_SET_LEVEL, bytes([OUT_PIN, 0]))
    time.sleep(0.4)
    check("falling edge -> ISR event -> rule ran the action", EV_CONN in conn_ids(link))
    link.request(bytes([KEY_CONN, 0x02]), payload=bytes([EV_CONN]))

    r = dev(link, GPIO_ID, IO_CONFIGURE_INTR, bytes([OUT_PIN]) + intr_cfg(INTR_DISABLE))
    check("CONFIGURE_INTR disable", r is not None and r.ok, str(r))
    dev(link, GPIO_ID, IO_SET_LEVEL, bytes([OUT_PIN, 1]))
    dev(link, GPIO_ID, IO_SET_LEVEL, bytes([OUT_PIN, 0]))
    time.sleep(0.4)
    check("disarmed: no event", EV_CONN not in conn_ids(link))

    ev(link, E_UNSUB, bytes([2]))
    act(link, A_REMOVE, bytes([EV_ACTION]))


def test_device(link: Link) -> None:
    r = dev(link, 10, OP_INFO)
    check("device INFO of an unregistered id -> ERR_DEV_NOT_FOUND", err_cause(r) == "ERR_DEV_NOT_FOUND", str(r))
    names = r.record.names() if r and r.record else []
    check("route: KEY 05 -> ERR_DEV_ID -> cause", names[:2] == ["ERR_ROUTER_KEY", "ERR_DEV_ID"], str(names))
    r = dev(link, 0x1234, OP_INFO)
    check("16-bit device id routed (0x1234 -> ERR_DEV_NOT_FOUND)", err_cause(r) == "ERR_DEV_NOT_FOUND", str(r))
    r = link.request(bytes([KEY_DEVICE, 3, 0]))
    check("device command without op -> ERR_CMD_BAD_LEN", err_cause(r) == "ERR_CMD_BAD_LEN", str(r))
    r = link.request(bytes([KEY_DEVICE, 3, 0, 5]))
    check("device command with a 1-byte op -> ERR_CMD_BAD_LEN", err_cause(r) == "ERR_CMD_BAD_LEN", str(r))

    # CREATE [type][cfg]: the registry looks the type up, checks the size and builds the device
    r = dev(link, 3, OP_CREATE)
    check("CREATE without type -> ERR_CMD_BAD_LEN", err_cause(r) == "ERR_CMD_BAD_LEN", str(r))
    r = dev(link, 3, OP_CREATE, bytes([0x99]))
    check("CREATE with a 1-byte type -> ERR_CMD_BAD_LEN", err_cause(r) == "ERR_CMD_BAD_LEN", str(r))
    r = dev(link, 3, OP_CREATE, struct.pack("<H", 0x99))
    check("CREATE unknown type -> ERR_DEV_TYPE_UNKNOWN", err_cause(r) == "ERR_DEV_TYPE_UNKNOWN", str(r))
    r = dev(link, 3, OP_CREATE, struct.pack("<HB", 0x0141, 0))
    check("CREATE type 0x0141 (low byte is PCA's) -> ERR_DEV_TYPE_UNKNOWN", err_cause(r) == "ERR_DEV_TYPE_UNKNOWN", str(r))
    r = dev(link, 3, OP_CREATE, struct.pack("<HB", PCA_TYPE, 0))
    check("CREATE wrong size -> ERR_CMD_BAD_LEN", err_cause(r) == "ERR_CMD_BAD_LEN", str(r))
    r = create_pca(link, 0xFFFF, 1, 0x40)
    check("CREATE at 0xFFFF -> ERR_DEV_BAD_ID", err_cause(r) == "ERR_DEV_BAD_ID", str(r))
    r = create_pca(link, 3, 7, 0x40)
    check("CREATE bad bus -> ERR_DEV_RANGE", err_cause(r) == "ERR_DEV_RANGE", str(r))
    r = create_pca(link, 3, 1, 0x40, oe=struct.pack("<HB", 5, 0))
    check("CREATE OE on a missing device -> ERR_DEV_NOT_FOUND", err_cause(r) == "ERR_DEV_NOT_FOUND", str(r))
    # Dependencies are declared, not ID-ordered: OE on a higher ID passes once that device exists
    r = dev(link, 9, OP_CREATE, struct.pack("<H", GPIO_TYPE))
    check("CREATE GPIO ESP at id 9", r is not None and r.ok, str(r))
    r = create_pca(link, 3, 1, 0x40, oe=struct.pack("<HB", 9, 4))  # nothing on bus 1: fails later, at the probe
    check("CREATE OE on a higher id gets past the dependency check",
          r is not None and not r.ok and err_cause(r) not in ("ERR_DEV_NOT_FOUND", "ERR_DEV_IN_USE"), str(r))
    r = dev(link, 9, OP_REMOVE)
    check("failed CREATE left no dependency: REMOVE GPIO 9", r is not None and r.ok, str(r))
    r = create_pca(link, 3, 1, 0x40)  # nothing on bus 1
    names = r.record.names() if r and r.record else []
    check("CREATE with no chip fails inside ERR_DEV_INSTALL", "ERR_DEV_INSTALL" in names, str(r))
    print(f"      chain: {' -> '.join(names)}")
    r = dev(link, 3, OP_INFO)
    check("failed CREATE registers nothing", err_cause(r) == "ERR_DEV_NOT_FOUND", str(r))

    r = create_pca(link, PCA_ID, PCA_BUS, PCA_ADDR)
    if r is None or not r.ok:
        print(f"      no PCA9685 at bus {PCA_BUS} 0x{PCA_ADDR:02x} ({err_cause(r)}): chip part skipped")
        return
    check(f"CREATE PCA9685 id {PCA_ID}", True)
    r = create_pca(link, PCA_ID, PCA_BUS, 0x61)
    check("CREATE on a used id -> ERR_DEV_ID_TAKEN", err_cause(r) == "ERR_DEV_ID_TAKEN", str(r))
    r = dev(link, PCA_ID, OP_INFO)
    check("INFO: type 0x0041, not suspended", r is not None and r.ok and r.data == struct.pack("<HB", PCA_TYPE, 0), str(r))
    test_pca(link)


def test_pca(link: Link) -> None:
    r = dev(link, PCA_ID, IO_SET_FREQ, struct.pack("<BI", 0, 50))
    check("SET_PWM_FREQ 50 Hz", r is not None and r.ok, str(r))
    r = dev(link, PCA_ID, IO_SET_FREQ, struct.pack("<BI", 0, 5000))
    check("SET_PWM_FREQ 5000 Hz -> ERR_DEV_RANGE", err_cause(r) == "ERR_DEV_RANGE", str(r))
    r = dev(link, PCA_ID, IO_SET_DUTY, struct.pack("<BI", 1, 3000))
    check("SET_PWM_DUTY ch1 3000", r is not None and r.ok, str(r))
    r = dev(link, PCA_ID, IO_GET_LEVEL, bytes([1]))
    check("GET_LEVEL ch1 = 1", r is not None and r.ok and r.data == bytes([1]), str(r))
    r = dev(link, PCA_ID, IO_TOGGLE, bytes([1]))
    check("TOGGLE ch1", r is not None and r.ok, str(r))
    r = dev(link, PCA_ID, IO_GET_LEVEL, bytes([1]))
    check("GET_LEVEL ch1 = 0", r is not None and r.ok and r.data == bytes([0]), str(r))
    r = dev(link, PCA_ID, IO_SET_DUTY, struct.pack("<BI", 1, 4096))
    check("duty 4096 -> ERR_DEV_RANGE", err_cause(r) == "ERR_DEV_RANGE", str(r))
    r = dev(link, PCA_ID, IO_SET_DUTY, struct.pack("<BI", 16, 100))
    check("channel 16 -> ERR_DEV_BAD_PIN", err_cause(r) == "ERR_DEV_BAD_PIN", str(r))
    r = dev(link, PCA_ID, 0x0300)
    names = r.record.names() if r and r.record else []
    check("contract the device lacks -> ERR_DEV_UNKNOWN_OP", err_cause(r) == "ERR_DEV_UNKNOWN_OP", str(r))
    check("unknown op chain: KEY 05 -> DEV_ID -> UNKNOWN_OP", names == ["ERR_ROUTER_KEY", "ERR_DEV_ID", "ERR_DEV_UNKNOWN_OP"], str(names))
    r = dev(link, PCA_ID, IO_GET_VOLTAGE, bytes([1]))
    check("IO op the PCA9685 lacks (GET_VOLTAGE) -> ERR_DEV_UNKNOWN_OP", err_cause(r) == "ERR_DEV_UNKNOWN_OP", str(r))
    r = dev(link, PCA_ID, 0xFF00)
    check("own op the device lacks -> ERR_DEV_UNKNOWN_OP", err_cause(r) == "ERR_DEV_UNKNOWN_OP", str(r))
    r = dev(link, PCA_ID, IO_SET_DUTY, struct.pack("<BI", 1, 5000))
    names = r.record.names() if r and r.record else []
    check("failing op -> ERR_DEV_OP route node", names[2:3] == ["ERR_DEV_OP"], str(names))

    r = dev(link, PCA_ID, OP_OPS)
    ops = set(struct.unpack(f"<{len(r.data) // 2}H", r.data)) if r is not None and r.ok else set()
    check("OPS lists the device's op table", ops >= {OP_OPS, OP_INFO, IO_SET_LEVEL, PINS_LOCK} and IO_GET_VOLTAGE not in ops, str(sorted(ops)))
    r = dev(link, PCA_ID, OP_OPS, bytes(1))
    check("OPS with args -> ERR_CMD_BAD_LEN", err_cause(r) == "ERR_CMD_BAD_LEN", str(r))

    r = dev(link, PCA_ID, PINS_AVAILABLE)
    check("PINS AVAILABLE = 16 channels", r is not None and r.ok and r.data == struct.pack("<Q", 0xFFFF), str(r))
    r = dev(link, PCA_ID, PINS_LOCK, bytes([8]))
    check("PINS LOCK ch8", r is not None and r.ok, str(r))
    r = dev(link, PCA_ID, PINS_LOCKED)
    check("PINS LOCKED = ch8", r is not None and r.ok and r.data == struct.pack("<Q", 1 << 8), str(r))
    r = dev(link, PCA_ID, PINS_LOCK, bytes([8]))
    check("LOCK of a locked pin -> ERR_DEV_PIN_LOCKED", err_cause(r) == "ERR_DEV_PIN_LOCKED", str(r))
    r = dev(link, PCA_ID, IO_SET_LEVEL, bytes([8, 1]))
    check("IO write of a locked pin -> ERR_DEV_PIN_LOCKED", err_cause(r) == "ERR_DEV_PIN_LOCKED", str(r))
    r = dev(link, PCA_ID, IO_GET_LEVEL, bytes([8]))
    check("IO read of a locked pin is allowed", r is not None and r.ok, str(r))
    r = dev(link, PCA_ID, PINS_SET_LOCKED_LEVEL, bytes([8, 1]))
    check("SET_LOCKED_LEVEL drives the locked pin", r is not None and r.ok, str(r))
    r = dev(link, PCA_ID, PINS_LOCK, bytes([16]))
    check("LOCK ch16 -> ERR_DEV_BAD_PIN", err_cause(r) == "ERR_DEV_BAD_PIN", str(r))
    r = dev(link, PCA_ID, PINS_UNLOCK, bytes([8]))
    check("PINS UNLOCK ch8", r is not None and r.ok, str(r))
    r = dev(link, PCA_ID, PINS_LOCKED)
    check("PINS LOCKED empty again", r is not None and r.ok and r.data == bytes(8), str(r))
    r = dev(link, PCA_ID, IO_SET_LEVEL, bytes([8, 0]))
    check("IO write after UNLOCK", r is not None and r.ok, str(r))

    r = dev(link, PCA_ID, OP_SUSPEND)
    check("SUSPEND", r is not None and r.ok, str(r))
    r = dev(link, PCA_ID, OP_INFO)
    check("INFO: suspended", r is not None and r.ok and r.data == struct.pack("<HB", PCA_TYPE, 1), str(r))
    r = dev(link, PCA_ID, IO_SET_LEVEL, bytes([0, 1]))
    check("write while suspended -> ERR_DEV_SUSPENDED", err_cause(r) == "ERR_DEV_SUSPENDED", str(r))
    r = dev(link, PCA_ID, IO_GET_LEVEL, bytes([1]))
    check("read while suspended is allowed", r is not None and r.ok, str(r))
    r = dev(link, PCA_ID, OP_RESUME)
    check("RESUME", r is not None and r.ok, str(r))

    t = link.mark()
    r = link.request(bytes([KEY_SYS, 0x02]))
    check("sys STOP suspends devices", r is not None and r.ok and link.wait_log("suspending devices", 2.0, since=t))
    r = dev(link, PCA_ID, OP_INFO)
    check("PCA suspended by the safe state", r is not None and r.ok and r.data[2] == 1, str(r))
    r = dev(link, PCA_ID, OP_RESUME)
    check("RESUME after the safe state", r is not None and r.ok, str(r))
    r = dev(link, PCA_ID, OP_RESET)
    check("RESET (all channels off)", r is not None and r.ok, str(r))

    r = dev(link, PCA_ID, OP_REMOVE)
    check("REMOVE", r is not None and r.ok, str(r))
    r = dev(link, PCA_ID, OP_INFO)
    check("INFO after REMOVE -> ERR_DEV_NOT_FOUND", err_cause(r) == "ERR_DEV_NOT_FOUND", str(r))


A_RUN, A_REC_START, A_REC_STOP, A_BEGIN, A_WRITE, A_COMMIT = 0x01, 0x02, 0x03, 0x04, 0x05, 0x06
A_READ, A_INFO, A_LIST, A_REMOVE, A_ABORT, A_RENAME = 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C
CONN_INFO = bytes([KEY_CONN, 0x07])  # succeeds on any target, not a sys frame (recordable)
T1, T2, T3, LOOP, KEEP = 1, 2, 3, 200, 255


def act(link: Link, op: int, payload: bytes = b"") -> Response | None:
    return link.request(bytes([KEY_ACTIONS, op]), payload=payload)


def image(*frames: bytes) -> bytes:
    return b"".join(struct.pack("<H", len(f)) + f for f in frames)


def upload(link: Link, aid: int, name: str, img: bytes, chunk: int = 64, crc: int | None = None) -> Response | None:
    r = act(link, A_BEGIN, struct.pack("<BH", aid, len(img)) + name.encode() + b"\0")
    if r is None or not r.ok:
        return r
    for off in range(0, len(img), chunk):
        r = act(link, A_WRITE, struct.pack("<H", off) + img[off : off + chunk])
        if r is None or not r.ok:
            return r
    return act(link, A_COMMIT, struct.pack("<I", zlib.crc32(img) if crc is None else crc))


def info(link: Link, aid: int) -> tuple[int, int, int, str] | None:
    r = act(link, A_INFO, bytes([aid]))
    if r is None or not r.ok or len(r.data) < 9:
        return None
    b, c, crc = struct.unpack_from("<HHI", r.data)
    return b, c, crc, r.data[8:-1].decode()


def actions_list(link: Link, start: int = 0) -> tuple[int, list[tuple[int, str]]] | None:
    r = act(link, A_LIST, bytes([start]))
    if r is None or not r.ok:
        return None
    total, d, pos, out = struct.unpack_from("<H", r.data)[0], r.data, 2, []
    while pos < len(d):
        end = d.index(0, pos + 1)
        out.append((d[pos], d[pos + 1 : end].decode()))
        pos = end + 1
    return total, out


def test_actions(link: Link) -> None:
    act(link, A_ABORT)
    for aid in (T1, T2, T3, LOOP, KEEP):
        act(link, A_REMOVE, bytes([aid]))

    # Record, executing: only the client's successful, non-sys frames are kept
    r = act(link, A_REC_START, bytes([T1, 1]) + b"blink\0")
    check("actions RECORD_START 1 'blink' (execute)", r is not None and r.ok, str(r))
    r = act(link, A_REC_START, bytes([9, 1]) + b"\0")
    check("second RECORD_START -> ERR_ACTION_BUSY", err_cause(r) == "ERR_ACTION_BUSY", str(r))
    r = link.request(CONN_INFO)
    check("recorded frame runs (conn INFO answers data)", r is not None and r.ok and len(r.data) > 1, str(r))
    link.request(bytes([0x7E]))  # fails: not recorded
    ping(link)  # sys: not recorded
    r = act(link, A_REC_STOP)
    want = image(CONN_INFO)
    ok = r is not None and r.ok and len(r.data) >= 9
    check("RECORD_STOP", ok, str(r))
    if ok:
        b, c, crc = struct.unpack_from("<HHI", r.data)
        check("recording kept 1 frame (failed + sys frames skipped)", (b, c) == (len(want), 1), f"{b} B, {c} frames")
        check("RECORD_STOP crc = zlib crc32, name 'blink'", crc == zlib.crc32(want) and r.data[8:] == b"blink\0", str(r))
    r = act(link, A_READ, struct.pack("<BHH", T1, 0, len(want)))
    check("READ 1 = the full image (no name)", r is not None and r.ok and r.data == want, str(r))
    r = act(link, A_READ, struct.pack("<BHH", T1, 1, len(want)))
    check("READ past the end -> ERR_ACTION_READ_RANGE", err_cause(r) == "ERR_ACTION_READ_RANGE", str(r))
    r = act(link, A_RUN, bytes([T1]))
    check("RUN 1", r is not None and r.ok, str(r))

    # Record without executing: frames are taken (answered OK, not run); no name
    r = act(link, A_REC_START, bytes([T2, 0]) + b"\0")
    check("RECORD_START 2 (no execute, no name)", r is not None and r.ok, str(r))
    r = dev(link, 10, OP_INFO)  # no device 10: would fail if it ran
    check("frame taken without running (OK, no data)", r is not None and r.ok and r.data == b"", str(r))
    r = act(link, A_REC_STOP)
    check("RECORD_STOP 2: 1 frame, empty name", r is not None and r.ok and struct.unpack_from("<HHI", r.data)[1] == 1 and r.data[8:] == b"\0", str(r))
    r = act(link, A_RUN, bytes([T2]))
    names = r.record.names() if r and r.record else []
    check("RUN 2 stops at the failing frame: STEP over its chain", "ERR_ACTION_STEP" in names and err_cause(r) == "ERR_DEV_NOT_FOUND", str(names))
    step = next((n for n in r.record.nodes if n.name == "ERR_ACTION_STEP"), None) if r and r.record else None
    check("STEP payload: id 2, step 0", step is not None and step.data == bytes([T2, 0, 0]), str(step))

    r = act(link, A_REC_START, bytes([9, 1]) + b"\0")
    r = act(link, A_REC_STOP)
    check("empty recording -> ERR_ACTION_EMPTY", err_cause(r) == "ERR_ACTION_EMPTY", str(r))

    # Upload: full image, nested RUN
    img = image(CONN_INFO, bytes([KEY_ACTIONS, A_RUN, T1]), CONN_INFO)
    r = upload(link, T3, "nested", img, chunk=5)
    check("upload 3 'nested' (3 frames, 5-byte chunks)", r is not None and r.ok, str(r))
    i = info(link, T3)
    check("INFO 3", i == (len(img), 3, zlib.crc32(img), "nested"), str(i))
    r = act(link, A_RUN, bytes([T3]))
    check("RUN 3 (runs 1 nested)", r is not None and r.ok, str(r))
    r = act(link, A_RENAME, bytes([T3]) + b"renamed\0")
    i = info(link, T3)
    check("RENAME 3: name changes, frames stay", r is not None and r.ok and i == (len(img), 3, zlib.crc32(img), "renamed"), str(i))
    r = upload(link, 9, "", img, crc=1)
    check("upload with a wrong crc -> ERR_ACTION_CRC", err_cause(r) == "ERR_ACTION_CRC", str(r))
    r = upload(link, 9, "", image(CONN_INFO)[:-1] + b"\x00\x05")
    check("upload with broken framing -> ERR_ACTION_BAD_FRAME", err_cause(r) == "ERR_ACTION_BAD_FRAME", str(r))
    r = act(link, A_BEGIN, struct.pack("<BH", 9, 8) + b"\0")
    r = act(link, A_WRITE, struct.pack("<H", 4) + b"xx")
    check("WRITE out of order -> ERR_ACTION_OFFSET", err_cause(r) == "ERR_ACTION_OFFSET", str(r))
    r = act(link, A_COMMIT, struct.pack("<I", 0))
    check("COMMIT before every byte -> ERR_ACTION_INCOMPLETE", err_cause(r) == "ERR_ACTION_INCOMPLETE", str(r))
    r = act(link, A_INFO, bytes([9]))
    check("failed uploads store nothing", err_cause(r) == "ERR_ACTION_NOT_FOUND", str(r))
    r = act(link, A_BEGIN, struct.pack("<BH", 9, 20000) + b"\0")
    check("BEGIN over the limit -> ERR_ACTION_TOO_BIG", err_cause(r) == "ERR_ACTION_TOO_BIG", str(r))

    r = upload(link, LOOP, "loop", image(bytes([KEY_ACTIONS, A_RUN, LOOP])))
    check("upload 200 (runs itself)", r is not None and r.ok, str(r))
    r = act(link, A_RUN, bytes([LOOP]))
    names = r.record.names() if r and r.record else []
    check("self-RUN stops at ERR_ACTION_DEPTH", err_cause(r) == "ERR_ACTION_DEPTH" and names.count("ERR_ACTION_STEP") >= 2, str(names))

    # IDs, names, listing
    r = act(link, A_RUN, bytes([77]))
    check("RUN unknown id -> ERR_ACTION_NOT_FOUND", err_cause(r) == "ERR_ACTION_NOT_FOUND", str(r))
    r = act(link, A_RUN, bytes([0]))
    check("RUN id 0 -> ERR_ACTION_BAD_ID", err_cause(r) == "ERR_ACTION_BAD_ID", str(r))
    r = act(link, A_RENAME, bytes([T1]) + b"0123456789abcdef\0")
    check("16-char name -> ERR_ACTION_BAD_NAME", err_cause(r) == "ERR_ACTION_BAD_NAME", str(r))
    r = act(link, A_RENAME, bytes([T1]) + b"x")
    check("name without NUL -> ERR_CMD_BAD_ARG", err_cause(r) == "ERR_CMD_BAD_ARG", str(r))
    l = actions_list(link)
    check("LIST: 1 blink, 2 '', 3 renamed, 200 loop (by ID)",
          l == (4, [(T1, "blink"), (T2, ""), (T3, "renamed"), (LOOP, "loop")]), str(l))
    l2 = actions_list(link, T3)
    check("LIST from 3", l2 == (4, [(T3, "renamed"), (LOOP, "loop")]), str(l2))

    for aid in (T1, T2, T3, LOOP):
        r = act(link, A_REMOVE, bytes([aid]))
        check(f"REMOVE {aid}", r is not None and r.ok, str(r))
    r = act(link, A_REMOVE, bytes([T1]))
    check("REMOVE again -> ERR_ACTION_NOT_FOUND", err_cause(r) == "ERR_ACTION_NOT_FOUND", str(r))
    r = upload(link, KEEP, "keep", image(CONN_INFO))
    check("upload 255 'keep' (checked after the restart)", r is not None and r.ok, str(r))


KEY_EVENTS = 0x07
E_SUB, E_UNSUB, E_LIST, E_PUB = 0x01, 0x02, 0x03, 0x04
STREAM_EVENTS = 0x06
EV_ACTION, EV_CONN = 10, 5
IO_FALLING = 0x0101


def conn_ids(link: Link) -> set[int]:
    r = link.request(bytes([KEY_CONN, 0x07]))
    out, d, pos = set(), r.data if r and r.ok else b"\0", 1
    for _ in range(d[0]):
        out.add(d[pos])
        pos += 4 + d[pos + 3] * 5
        pos = d.index(0, pos) + 1
    return out


def ev(link: Link, op: int, payload: bytes = b"") -> Response | None:
    return link.request(bytes([KEY_EVENTS, op]), payload=payload)


def rule(rid: int, source: int, event: int, channel: int, action: int, flags: int = 0) -> bytes:
    return struct.pack("<BHHBBB", rid, source, event, channel, action, flags)


def publish(link: Link, source: int, event: int, channel: int, value: int, flags: int = 0) -> Response | None:
    return ev(link, E_PUB, struct.pack("<HHBiB", source, event, channel, value, flags))


def rules(link: Link) -> tuple[int, list[tuple]] | None:
    r = ev(link, E_LIST, b"\x00")
    if r is None or not r.ok:
        return None
    return r.data[0], [struct.unpack_from("<BHHBBB", r.data, p) for p in range(1, len(r.data), 8)]


def test_events(link: Link) -> None:
    ev(link, E_UNSUB, b"\xff")
    act(link, A_REMOVE, bytes([EV_ACTION]))
    link.request(bytes([KEY_CONN, 0x02]), payload=bytes([EV_CONN]))
    check("events LIST empty", rules(link) == (0, []), str(rules(link)))

    # The action a rule runs: create connector 5 (visible in conn INFO)
    r = upload(link, EV_ACTION, "mk conn", image(bytes([KEY_CONN, 0x01, EV_CONN, 0x11]) + b"evt\0"))
    check("upload action 10 (creates connector 5)", r is not None and r.ok, str(r))

    r = ev(link, E_SUB, rule(1, 0x0123, 0x01FF, 0xFF, EV_ACTION))
    check("SUBSCRIBE rule 1: source 0x123, any IO event, any channel -> action 10", r is not None and r.ok, str(r))
    r = publish(link, 0x0124, IO_FALLING, 4, 0)
    time.sleep(0.4)
    check("event from another source: rule silent", r is not None and r.ok and EV_CONN not in conn_ids(link), str(r))
    r = publish(link, 0x0123, 0x0201, 4, 0)
    time.sleep(0.4)
    check("event of another contract: rule silent", r is not None and r.ok and EV_CONN not in conn_ids(link), str(r))
    r = publish(link, 0x0123, IO_FALLING, 4, 0)
    time.sleep(0.4)
    check("matching event runs the action (connector 5 created)", r is not None and r.ok and EV_CONN in conn_ids(link), str(r))
    link.request(bytes([KEY_CONN, 0x02]), payload=bytes([EV_CONN]))

    r = ev(link, E_SUB, rule(1, 0x0123, IO_FALLING, 4, 99, 1))
    l = rules(link)
    check("SUBSCRIBE same ID replaces the rule", l == (1, [(1, 0x0123, IO_FALLING, 4, 99, 1)]), str(l))
    t = link.mark()
    publish(link, 0x0123, IO_FALLING, 4, 0)
    time.sleep(0.5)
    recs = [x for x in link.error_records_since(t) if "ERR_ACTION_NOT_FOUND" in x.names()]
    check("rule -> missing action: error on the stream, internal link, RUN 99",
          len(recs) == 1 and recs[0].link == 0 and recs[0].request == bytes([0x06, 0x01, 99]),
          str(recs[0]) if recs else "no record")

    # Notify: the event itself on stream 0x06, rule or not
    t = link.mark()
    publish(link, 0x0042, 0x0201, 2, -1500, flags=1)
    publish(link, 0x0042, 0x0202, 2, 77, flags=0)
    time.sleep(0.4)
    frames = link.stream_since(STREAM_EVENTS, t)
    ok = len(frames) == 1 and len(frames[0]) == 13
    check("NOTIFY event on stream 0x06 (13 B), the other one not", ok, str([f.hex(" ") for f in frames]))
    if ok:
        src, evt, ch, val, t_ms = struct.unpack("<HHBiI", frames[0])
        check("notify record: source, event, channel, value, time", (src, evt, ch, val) == (0x42, 0x0201, 2, -1500) and t_ms > 0,
              f"{src:#x} {evt:#x} {ch} {val} {t_ms}")

    r = ev(link, E_SUB, rule(0, 0xFFFF, 0xFFFF, 0xFF, 1))
    check("rule ID 0 -> ERR_EVENT_BAD_ID", err_cause(r) == "ERR_EVENT_BAD_ID", str(r))
    r = ev(link, E_SUB, rule(0xFF, 0xFFFF, 0xFFFF, 0xFF, 1))
    check("rule ID 0xFF -> ERR_EVENT_BAD_ID", err_cause(r) == "ERR_EVENT_BAD_ID", str(r))
    r = ev(link, E_UNSUB, b"\x05")
    check("UNSUBSCRIBE unknown -> ERR_EVENT_NOT_FOUND", err_cause(r) == "ERR_EVENT_NOT_FOUND", str(r))
    for rid in range(2, 17):
        ev(link, E_SUB, rule(rid, 0x7000 + rid, 0xFFFF, 0xFF, 1))
    r = ev(link, E_SUB, rule(17, 0x7000, 0xFFFF, 0xFF, 1))
    check("17th rule -> ERR_EVENT_TABLE_FULL", err_cause(r) == "ERR_EVENT_TABLE_FULL", str(r))
    l = rules(link)
    check("LIST: 16 rules by ascending ID", l is not None and l[0] == 16 and [x[0] for x in l[1]] == list(range(1, 17)), str(l and l[0]))
    r = ev(link, E_UNSUB, b"\xff")
    check("UNSUBSCRIBE 0xFF removes every rule", r is not None and r.ok and rules(link) == (0, []), str(r))
    act(link, A_REMOVE, bytes([EV_ACTION]))


def test_actions_after_restart(link: Link) -> None:
    i = info(link, KEEP)
    check("action survives the restart (id 255, name 'keep')", i is not None and i[1] == 1 and i[3] == "keep", str(i))
    r = act(link, A_REMOVE, bytes([KEEP]))
    check("REMOVE 255", r is not None and r.ok, str(r))


def test_restart(link: Link) -> None:
    t = link.mark()
    r = link.request(bytes([KEY_SYS, 0x03]))
    check("RESTART answered", r is not None and r.ok, str(r))
    check("board boots again", link.wait_log("runIT up", 20.0, since=t))
    check("PING after restart", ping(link) is not None)


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", required=True, help="COM5 or socket://localhost:5555")
    ap.add_argument("--target", choices=["qemu", "board"], default="board")
    ap.add_argument("--boot-wait", type=float, default=15.0)
    ap.add_argument("--echo", action="store_true", help="print the device log")
    a = ap.parse_args()

    with Link(a.port, echo=a.echo) as link:
        booted = wait_boot(link, a.boot_wait)
        if not booted:
            print("note: no 'runIT up' seen (already running?) - continuing")
        boot_log = link.logs_since(0)
        failed_steps = [l for l in boot_log if "boot step" in l]
        for l in failed_steps:
            print("      boot:", l)

        test_sys(link)
        test_router_errors(link)
        test_errors_stream(link)
        test_deadman(link)
        test_stop(link)
        test_i2c(link, a.target)
        test_conn(link)
        test_ble(link, a.target)
        test_gpio(link, a.target)
        test_device(link)
        test_actions(link)
        test_events(link)
        test_restart(link)
        test_actions_after_restart(link)

    failed = [r for r in RESULTS if not r[1]]
    print(f"\n{len(RESULTS) - len(failed)}/{len(RESULTS)} passed")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
