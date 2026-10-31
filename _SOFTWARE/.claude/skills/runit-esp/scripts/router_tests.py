"""Router / streams test suite over the console link.

  QEMU:   python router_tests.py --port socket://localhost:5555 --target qemu
  Board:  python router_tests.py --port COM5

Checks the router (keys, the direction byte, payload sizes, error records,
urgent path, dead-man), the sys / i2c / conn / ble routers, and the errors stream.
Target "qemu" skips what QEMU can't do (BLE controller, I2C devices).

Requests are [key][dir][...] (dir 0 read, 1 write): callers pass the keys
without dir and router_link adds it from the descriptors; a command with both
sides gets dir=READ / dir=WRITE. Frames stored in records, images and project
streams are built with frame(keys, dir): they carry dir as the router takes it.
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
    READ,
    WRITE,
    Link,
    Response,
    frame,
)

RESULTS: list[tuple[str, bool, str]] = []


def check(name: str, ok: bool, detail: str = "") -> bool:
    RESULTS.append((name, ok, detail))
    print(f"{'PASS' if ok else 'FAIL'}  {name}" + (f"  -- {detail}" if detail and not ok else ""))
    return ok


#: set by main(): the board has its onboard devices (the runIT PCB), not a bare devkit. Checks that need the devkit's empty
#: device table or its missing input monitor are skipped there (board_tests.py covers the PCB).
ONBOARD = False
MISSING_ID = 77  # a device id nobody creates (10 is the PCB's TPS55289 rail A)
SKIPPED: list[str] = []


def skip(name: str, why: str) -> None:
    SKIPPED.append(name)
    print(f"SKIP  {name}  -- {why}")


def devkit_check(name: str, ok: bool, detail: str = "") -> bool:
    """A check that only holds on a bare devkit: on the PCB it is skipped, not failed."""
    if ONBOARD:
        skip(name, "bare devkit only")
        return True
    return check(name, ok, detail)


def err_cause(r: Response | None) -> str:
    return r.record.cause.name if r and r.record and r.record.cause else ""


def wait_boot(link: Link, timeout: float) -> bool:
    return link.wait_log("runIT up", timeout)


# sys ops (contract_sys.json)
SYS_PING, SYS_STOP, SYS_RESTART, SYS_DEADMAN, SYS_BYE, SYS_TIME, SYS_SAFE, SYS_SAFE_LEAVE = 1, 2, 3, 4, 6, 7, 9, 10


def ping(link: Link, urgent: bool = False) -> int | None:
    r = link.request(bytes([KEY_SYS, SYS_PING]), urgent=urgent)
    return struct.unpack("<I", r.data)[0] if r and r.ok and len(r.data) == 4 else None


def deadman_get(link: Link) -> Response | None:
    return link.request(bytes([KEY_SYS, SYS_DEADMAN]), dir=READ)


def deadman_set(link: Link, ms: int, on_lost: int) -> Response | None:
    return link.request(bytes([KEY_SYS, SYS_DEADMAN]), payload=struct.pack("<IB", ms, on_lost), dir=WRITE)


# ---------------------------------------------------------------------------


def test_sys(link: Link) -> None:
    t1 = ping(link)
    check("sys PING (normal)", t1 is not None)
    t2 = ping(link, urgent=True)
    check("sys PING (urgent)", t2 is not None)
    check("uptime grows", t1 is not None and t2 is not None and t2 >= t1, f"{t1} -> {t2}")

    r = link.request(bytes([KEY_SYS, SYS_PING]), payload=b"\x00")
    check("PING with payload -> ERR_CMD_BAD_LEN", err_cause(r) == "ERR_CMD_BAD_LEN", str(r))

    r = deadman_get(link)
    ok = r is not None and r.ok and len(r.data) == 5
    check("DEADMAN read", ok, str(r))
    if ok:
        ms, on_lost = struct.unpack("<IB", r.data)
        check("dead-man off at boot", ms == 0 and on_lost == 0, f"{ms} {on_lost}")

    # BYE: the client leaves on purpose, the dead-man it armed is off.
    r = deadman_set(link, 60000, 0)
    check("DEADMAN write 60 s: ACK without data", r is not None and r.ok and r.data == b"", str(r))
    r = deadman_get(link)
    check("DEADMAN read = what was written", r is not None and r.ok and r.data == struct.pack("<IB", 60000, 0), str(r))
    r = link.request(bytes([KEY_SYS, SYS_BYE]))
    check("BYE", r is not None and r.ok, str(r))
    r = deadman_get(link)
    check("BYE disarmed the dead-man", r is not None and r.ok and len(r.data) == 5 and struct.unpack("<IB", r.data)[0] == 0, str(r))

    # TIME: the wall clock the client sets (read 0 = never set)
    now_ms = int(time.time() * 1000)
    r = link.request(bytes([KEY_SYS, SYS_TIME]), payload=struct.pack("<Q", now_ms), dir=WRITE)
    check("TIME write", r is not None and r.ok and r.data == b"", str(r))
    r = link.request(bytes([KEY_SYS, SYS_TIME]), dir=READ)
    got = struct.unpack("<Q", r.data)[0] if r is not None and r.ok and len(r.data) == 8 else None
    check("TIME read: the clock runs from what was written", got is not None and now_ms <= got < now_ms + 5000, f"{got} {now_ms}")


def test_router_errors(link: Link) -> None:
    r = link.request(bytes([0x7E]))
    rec = r.record if r else None
    check("unknown root key -> ERR_ROUTER_UNKNOWN_KEY", err_cause(r) == "ERR_ROUTER_UNKNOWN_KEY", str(r))
    if rec:
        check("record: src command, link UART", rec.src == 1 and rec.link == TRANSPORT_UART, str(rec))
        check("record: echoes seq and request (key + dir)", rec.seq == r.seq and rec.request == bytes([0x7E, WRITE]), str(rec))

    r = link.request(bytes([KEY_SYS, 0x7E]))
    names = r.record.names() if r and r.record else []
    check(
        "unknown op -> route node + ERR_ROUTER_UNKNOWN_KEY",
        names == ["ERR_ROUTER_KEY", "ERR_ROUTER_UNKNOWN_KEY"],
        str(r),
    )

    # Frame without a root key, and a root key without the direction byte: refused by router_submit, answered by the link all the same.
    seq = link.next_seq()
    link.send_frame(bytes([seq, 0x00]))
    r = link.wait_response(seq)
    check("frame without key -> ERR_ROUTER_FRAME_LEN", err_cause(r) == "ERR_ROUTER_FRAME_LEN", str(r))
    seq = link.next_seq()
    link.send_frame(bytes([seq, 0x00, KEY_SYS]))
    r = link.wait_response(seq)
    check("frame with a key but no dir -> ERR_ROUTER_FRAME_LEN", err_cause(r) == "ERR_ROUTER_FRAME_LEN", str(r))

    # The direction byte: a side the command lacks, or a dir that is neither read nor write
    r = link.request(bytes([KEY_SYS, READ, SYS_BYE]), raw=True)
    names = r.record.names() if r and r.record else []
    check("write-only command (BYE) sent as a read -> ERR_CMD_DIRECTION",
          names == ["ERR_ROUTER_KEY", "ERR_ROUTER_KEY", "ERR_CMD_DIRECTION"], str(r))
    if r and r.record and r.record.cause:
        check("ERR_CMD_DIRECTION names the dir", r.record.cause.data == bytes([READ]), str(r.record.cause))
    r = link.request(bytes([KEY_CONN, WRITE, 0x07]), raw=True)
    check("read-only command (conn INFO) sent as a write -> ERR_CMD_DIRECTION", err_cause(r) == "ERR_CMD_DIRECTION", str(r))
    r = link.request(bytes([KEY_SYS, 2, SYS_PING]), raw=True)
    check("dir 2 -> ERR_CMD_DIRECTION", err_cause(r) == "ERR_CMD_DIRECTION", str(r))
    r = link.request(bytes([KEY_DEVICE, READ]) + struct.pack("<HH", MISSING_ID, OP_CREATE) + struct.pack("<H", GPIO_TYPE), raw=True)
    check("device CREATE sent as a read -> ERR_CMD_DIRECTION", err_cause(r) == "ERR_CMD_DIRECTION", str(r))
    try:
        link.request(bytes([KEY_SYS, SYS_DEADMAN]))
        check("a command with both sides needs dir (router_link refuses it)", False)
    except ValueError:
        check("a command with both sides needs dir (router_link refuses it)", True)


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
    r = deadman_set(link, 300, 0)
    check("DEADMAN write 300 ms", r is not None and r.ok, str(r))
    fired = link.wait_log("safe state", 2.0, since=t)
    check("dead-man fires without frames", fired)
    r = deadman_set(link, 0, 0)
    check("DEADMAN write off", r is not None and r.ok, str(r))
    r = deadman_set(link, 0, 2)
    check("DEADMAN write bad flag -> ERR_CMD_BAD_ARG", err_cause(r) == "ERR_CMD_BAD_ARG", str(r))


def test_stop(link: Link) -> None:
    t = link.mark()
    r = link.request(bytes([KEY_SYS, SYS_STOP]), urgent=True)
    check("STOP (urgent) answered", r is not None and r.ok, str(r))
    check("STOP runs the safe-state handler", link.wait_log("safe state", 1.0, since=t))
    r = link.request(bytes([KEY_SYS, SYS_SAFE]))
    check("SAFE read: in the safe state (reason set)", r is not None and r.ok and len(r.data) == 5 and r.data[0] != 0, str(r))
    r = link.request(bytes([KEY_SYS, SYS_SAFE_LEAVE]))
    check("SAFE_LEAVE", r is not None and r.ok, str(r))
    r = link.request(bytes([KEY_SYS, SYS_SAFE]))
    check("SAFE read after SAFE_LEAVE: reason 0", r is not None and r.ok and len(r.data) == 5 and r.data[0] == 0, str(r))


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
    def info() -> dict[int, dict]:  # by connector
        r = link.request(bytes([KEY_CONN, 0x07]))
        out = {}
        if not (r and r.ok):
            return out
        d, pos = r.data, 1
        for _ in range(d[0]):
            connector, flags, n = d[pos : pos + 3]
            pos += 3
            binds = []
            for _ in range(n):
                prov, ep = struct.unpack_from("<BI", d, pos)
                pos += 5
                binds.append((prov, ep))
            out[connector] = {"flags": flags, "binds": binds}
        return out

    i = info()
    check("conn INFO: 4 system connectors (0x02 0x03 0x04 0x06)", all(k in i and i[k]["flags"] & 1 for k in (2, 3, 4, 6)), str(i))
    if 4 in i:
        check("errors 0x04 -> UART + BLE 0xFFE3", sorted(i[4]["binds"]) == [(TRANSPORT_UART, 0), (TRANSPORT_BLE, 0xFFE3)], str(i[4]))
    if 6 in i:
        check("events 0x06 -> UART + BLE 0xFFE1", sorted(i[6]["binds"]) == [(TRANSPORT_UART, 0), (TRANSPORT_BLE, 0xFFE1)], str(i[6]))

    r = link.request(bytes([KEY_CONN, 0x01]), payload=bytes([0x06]))
    check("conn CREATE on the events byte -> ERR_CONN_EXISTS", err_cause(r) == "ERR_CONN_EXISTS", str(r))
    r = link.request(bytes([KEY_CONN, 0x01]), payload=bytes([0x05]))
    check("conn CREATE on the answer byte -> ERR_CONN_BAD_ARG", err_cause(r) == "ERR_CONN_BAD_ARG", str(r))
    r = link.request(bytes([KEY_CONN, 0x01]), payload=bytes([0x10]))
    check("conn CREATE user 0x10", r is not None and r.ok, str(r))
    r = link.request(bytes([KEY_CONN, 0x03]), payload=struct.pack("<BBI", 0x10, TRANSPORT_UART, 0))
    check("conn BIND 0x10 -> UART", r is not None and r.ok, str(r))
    i = info()
    check("conn INFO shows user 0x10", 0x10 in i and not i[0x10]["flags"] & 1 and i[0x10]["binds"] == [(TRANSPORT_UART, 0)], str(i.get(0x10)))
    r = link.request(bytes([KEY_CONN, 0x02]), payload=bytes([0x04]))
    check("conn REMOVE errors -> ERR_CONN_PROTECTED", err_cause(r) == "ERR_CONN_PROTECTED", str(r))
    r = link.request(bytes([KEY_CONN, 0x02]), payload=bytes([0x10]))
    check("conn REMOVE user 0x10", r is not None and r.ok, str(r))
    check("conn INFO: 0x10 gone", 0x10 not in info())


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
# Ops are u16: high byte = contract (0x00 device, 0x01 IO, 0x02 PINS, 0xFF the device's own), low byte = op
# (generated/contracts/contract_*.json). LEVEL, VOLTAGE and ERROR_POLICY have a read and a write side: dir is passed.
OP_CREATE, OP_RESET, OP_SUSPEND, OP_RESUME, OP_REMOVE, OP_INFO, OP_OPS = 0x0000, 0x0001, 0x0002, 0x0003, 0x0004, 0x0005, 0x0006
IO_LEVEL, IO_TOGGLE, IO_PWM_DUTY, IO_PWM_FREQ = 0x0100, 0x0101, 0x0103, 0x0104
IO_MODE, IO_RESET, IO_INTR, IO_VOLTAGE = 0x0105, 0x0106, 0x0107, 0x0109
PINS_AVAILABLE, PINS_LOCKED, PINS_LOCK, PINS_UNLOCK, PINS_LOCKED_LEVEL = 0x0200, 0x0201, 0x0202, 0x0203, 0x0204
SIDE_READ, SIDE_WRITE = 0x01, 0x02  # OPS: the sides of an op
PCA_ID = 3


def dev(link: Link, dev_id: int, op: int, payload: bytes = b"", dir: int | None = None) -> Response | None:
    return link.request(bytes([KEY_DEVICE]) + struct.pack("<HH", dev_id, op), payload=payload, dir=dir)


def set_level(link: Link, dev_id: int, pin: int, level: int) -> Response | None:
    return dev(link, dev_id, IO_LEVEL, bytes([pin, level]), dir=WRITE)


def get_level(link: Link, dev_id: int, pin: int) -> Response | None:
    """IO LEVEL read: the key {pin}, answered {pin, level}."""
    return dev(link, dev_id, IO_LEVEL, bytes([pin]), dir=READ)


def ops_of(r: Response | None) -> dict[int, int]:
    """OPS: {op: sides} from its [op u16][sides u8] rows."""
    if r is None or not r.ok or len(r.data) % 3:
        return {}
    return {op: sides for op, sides in struct.iter_unpack("<HB", r.data)}


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
    check(f"CREATE ESP32-S3 GPIO id {GPIO_ID}", r is not None and r.ok, str(r))
    if r is None or not r.ok:
        return
    r = dev(link, 1, OP_CREATE, struct.pack("<H", GPIO_TYPE))
    check("second ESP32-S3 GPIO -> ERR_DEV_ID_TAKEN", err_cause(r) == "ERR_DEV_ID_TAKEN", str(r))
    r = dev(link, GPIO_ID, OP_INFO)
    check("INFO: type 0x0040", r is not None and r.ok and r.data == struct.pack("<HB", GPIO_TYPE, 0), str(r))
    ops = ops_of(dev(link, GPIO_ID, OP_OPS))
    check("OPS: LEVEL read + write, MODE, INTR, PINS; VOLTAGE read only (no write side)",
          ops.get(IO_LEVEL) == SIDE_READ | SIDE_WRITE and ops.get(IO_MODE) == SIDE_WRITE and ops.get(IO_INTR) == SIDE_WRITE
          and ops.get(PINS_LOCK) == SIDE_WRITE and ops.get(IO_VOLTAGE) == SIDE_READ,
          str({hex(k): v for k, v in ops.items()}))
    r = dev(link, GPIO_ID, PINS_AVAILABLE)
    check("PINS AVAILABLE = the S3's valid GPIOs", r is not None and r.ok and r.data == struct.pack("<Q", GPIO_VALID), str(r))
    r = dev(link, GPIO_ID, IO_MODE, bytes([22, 3]))
    check("GPIO 22 (doesn't exist) -> ERR_DEV_BAD_PIN", err_cause(r) == "ERR_DEV_BAD_PIN", str(r))
    r = set_level(link, GPIO_ID, OUT_PIN, 1)
    check("write of an unconfigured pin -> ERR_IO_PIN_UNCONFIGURED", err_cause(r) == "ERR_IO_PIN_UNCONFIGURED", str(r))

    r = dev(link, GPIO_ID, IO_MODE, bytes([OUT_PIN, 3]))
    check("MODE push-pull", r is not None and r.ok, str(r))
    r = dev(link, GPIO_ID, IO_MODE, bytes([OUT_PIN, 3]))
    check("MODE of a configured pin -> ERR_IO_PIN_IN_USE", err_cause(r) == "ERR_IO_PIN_IN_USE", str(r))
    r = dev(link, GPIO_ID, IO_MODE, bytes([PWM_PIN, 8]))
    check("MODE DAC -> ERR_IO_MODE_UNSUPPORTED", err_cause(r) == "ERR_IO_MODE_UNSUPPORTED", str(r))
    r = set_level(link, GPIO_ID, OUT_PIN, 1)
    check("LEVEL write 1: ACK without data", r is not None and r.ok and r.data == b"", str(r))
    r = get_level(link, GPIO_ID, OUT_PIN)
    check("LEVEL read = {pin, 1}", r is not None and r.ok and r.data == bytes([OUT_PIN, 1]), str(r))
    r = dev(link, GPIO_ID, IO_TOGGLE, bytes([OUT_PIN]))
    check("TOGGLE", r is not None and r.ok, str(r))
    r = get_level(link, GPIO_ID, OUT_PIN)
    check("LEVEL read = {pin, 0}", r is not None and r.ok and r.data == bytes([OUT_PIN, 0]), str(r))
    r = dev(link, GPIO_ID, IO_VOLTAGE, struct.pack("<Bi", OUT_PIN, 1000), dir=WRITE)
    check("VOLTAGE write on the GPIO (read side only) -> ERR_CMD_DIRECTION", err_cause(r) == "ERR_CMD_DIRECTION", str(r))
    test_gpio_intr(link, target)

    r = dev(link, GPIO_ID, IO_MODE, bytes([PWM_PIN, 6]))
    check("MODE PWM", r is not None and r.ok, str(r))
    r = set_level(link, GPIO_ID, PWM_PIN, 1)
    check("LEVEL write of a PWM pin -> ERR_IO_WRONG_MODE", err_cause(r) == "ERR_IO_WRONG_MODE", str(r))
    r = dev(link, GPIO_ID, IO_PWM_FREQ, struct.pack("<BI", PWM_PIN, 4))
    check("PWM 4 Hz -> ERR_DEV_RANGE", err_cause(r) == "ERR_DEV_RANGE", str(r))
    r = dev(link, GPIO_ID, IO_PWM_FREQ, struct.pack("<BI", PWM_PIN, 1000))
    check("PWM_FREQ 1 kHz", r is not None and r.ok, str(r))
    r = dev(link, GPIO_ID, IO_PWM_DUTY, struct.pack("<BI", PWM_PIN, 4096))
    check("duty 4096 -> ERR_DEV_RANGE", err_cause(r) == "ERR_DEV_RANGE", str(r))
    r = dev(link, GPIO_ID, IO_PWM_DUTY, struct.pack("<BI", PWM_PIN, 2048))
    check("PWM_DUTY 2048", r is not None and r.ok, str(r))

    r = dev(link, GPIO_ID, PINS_LOCK, bytes([OUT_PIN]))
    check("PINS LOCK", r is not None and r.ok, str(r))
    r = set_level(link, GPIO_ID, OUT_PIN, 1)
    check("write of a locked pin -> ERR_DEV_PIN_LOCKED", err_cause(r) == "ERR_DEV_PIN_LOCKED", str(r))
    r = dev(link, GPIO_ID, PINS_LOCKED_LEVEL, bytes([OUT_PIN, 1]))
    check("LOCKED_LEVEL drives it", r is not None and r.ok, str(r))

    r = dev(link, GPIO_ID, OP_SUSPEND)
    check("SUSPEND", r is not None and r.ok, str(r))
    r = dev(link, GPIO_ID, IO_PWM_DUTY, struct.pack("<BI", PWM_PIN, 100))
    check("write while suspended -> ERR_DEV_SUSPENDED", err_cause(r) == "ERR_DEV_SUSPENDED", str(r))
    r = dev(link, GPIO_ID, PINS_LOCKED_LEVEL, bytes([OUT_PIN, 0]))
    check("the owner still drives its locked pin while suspended", r is not None and r.ok, str(r))
    r = dev(link, GPIO_ID, OP_RESUME)
    check("RESUME", r is not None and r.ok, str(r))
    r = dev(link, GPIO_ID, IO_PWM_DUTY, struct.pack("<BI", PWM_PIN, 100))
    check("write after RESUME", r is not None and r.ok, str(r))

    r = dev(link, GPIO_ID, OP_RESET)
    check("RESET releases the unlocked pins", r is not None and r.ok, str(r))
    r = dev(link, GPIO_ID, IO_PWM_DUTY, struct.pack("<BI", PWM_PIN, 100))
    check("PWM pin is unconfigured after RESET", err_cause(r) == "ERR_IO_PIN_UNCONFIGURED", str(r))
    r = get_level(link, GPIO_ID, OUT_PIN)
    check("the locked pin survived RESET", r is not None and r.ok, str(r))

    r = dev(link, GPIO_ID, OP_REMOVE)
    check("REMOVE ESP32-S3 GPIO", r is not None and r.ok, str(r))
    r = dev(link, GPIO_ID, OP_INFO)
    check("INFO after REMOVE -> ERR_DEV_NOT_FOUND", err_cause(r) == "ERR_DEV_NOT_FOUND", str(r))


def intr_cfg(mode: int, debounce: int = 0, up: int = 0, down: int = 0, hyst: int = 0, count: int = 0) -> bytes:
    return struct.pack("<BBHHHH", mode, debounce, up, down, hyst, count)


INTR_DISABLE, INTR_RISING, INTR_FALLING, INTR_BOTH, INTR_OUTSIDE = 0, 1, 2, 3, 4


def test_gpio_intr(link: Link, target: str) -> None:
    """OUT_PIN is a push-pull output at level 0: the GPIO matrix sees its own edges, so the ISR runs without a wire.
    A pin edge is code flow (no NOTIFY): it never reaches the events stream, so only "armed edges cause no traffic and
    no fault" is checked here; the delivery itself is seen once a VM program can send it."""
    r = dev(link, GPIO_ID, IO_INTR, bytes([PWM_PIN]) + intr_cfg(INTR_FALLING))
    check("INTR of an unconfigured pin -> ERR_IO_PIN_UNCONFIGURED", err_cause(r) == "ERR_IO_PIN_UNCONFIGURED", str(r))
    r = dev(link, GPIO_ID, IO_INTR, bytes([OUT_PIN]) + intr_cfg(9))
    check("INTR mode 9 -> ERR_CMD_BAD_ARG", err_cause(r) == "ERR_CMD_BAD_ARG", str(r))
    r = dev(link, GPIO_ID, IO_INTR, bytes([OUT_PIN]) + intr_cfg(INTR_OUTSIDE, up=2000, down=1000))
    check("ADC window on a digital pin -> ERR_IO_FEATURE_UNSUPPORTED", err_cause(r) == "ERR_IO_FEATURE_UNSUPPORTED", str(r))
    r = dev(link, GPIO_ID, IO_INTR, bytes([OUT_PIN]) + intr_cfg(INTR_FALLING))
    check("INTR falling edge", r is not None and r.ok, str(r))
    if target == "qemu":  # no GPIO readback, so no edges reach the ISR
        r = dev(link, GPIO_ID, IO_INTR, bytes([OUT_PIN]) + intr_cfg(INTR_DISABLE))
        check("INTR disable", r is not None and r.ok, str(r))
        return

    t = link.mark()
    for level in (1, 0, 1, 0):
        r = set_level(link, GPIO_ID, OUT_PIN, level)
    time.sleep(0.4)
    check("armed edges: device still answers", r is not None and r.ok, str(r))
    check("edge without NOTIFY: nothing on stream 0x06", event_records(link, t) == [], str(event_records(link, t)))

    r = dev(link, GPIO_ID, IO_INTR, bytes([OUT_PIN]) + intr_cfg(INTR_DISABLE))
    check("INTR disable", r is not None and r.ok, str(r))


MUX_TYPE = 0x004A
MUX_ID = 7
SUPPLY_SOURCE = 0x0800  # POWER_SUPPLY SOURCE: the write selects, the read answers the source
MUX_A_PIN, MUX_B_PIN = 6, 7  # free on the devkit and the runIT board


def test_power_mux(link: Link, target: str) -> None:
    """The mux switches two ESP32-S3 GPIO pins (own GPIO device, removed at the end). Levels are read back on a board only:
    QEMU has no GPIO readback."""
    r = dev(link, GPIO_ID, OP_CREATE, struct.pack("<H", GPIO_TYPE))
    check("power mux: CREATE ESP32-S3 GPIO for its pins", r is not None and r.ok, str(r))
    if r is None or not r.ok:
        return

    def pin(n: int) -> bytes:
        return struct.pack("<HB", GPIO_ID, n)

    def create(a: bytes, b: bytes, level: int = 1, delay: int = 5) -> Response | None:
        return dev(link, MUX_ID, OP_CREATE, struct.pack("<H", MUX_TYPE) + a + b + bytes([level, delay]))

    def source() -> int | None:
        r = dev(link, MUX_ID, SUPPLY_SOURCE, dir=READ)
        return r.data[0] if r is not None and r.ok and len(r.data) == 1 else None

    def select(*args: int) -> Response | None:
        return dev(link, MUX_ID, SUPPLY_SOURCE, bytes(args), dir=WRITE)

    def levels() -> tuple[int, int] | None:
        a = get_level(link, GPIO_ID, MUX_A_PIN)
        b = get_level(link, GPIO_ID, MUX_B_PIN)
        if a is None or b is None or not a.ok or not b.ok or len(a.data) != 2 or len(b.data) != 2:
            return None
        return a.data[1], b.data[1]

    r = create(pin(MUX_A_PIN), pin(MUX_B_PIN), level=2)
    check("power mux: CREATE enable level 2 -> ERR_DEV_RANGE", err_cause(r) == "ERR_DEV_RANGE", str(r))
    r = create(pin(MUX_A_PIN), pin(MUX_B_PIN), delay=0)
    check("power mux: CREATE switch delay 0 -> ERR_DEV_RANGE", err_cause(r) == "ERR_DEV_RANGE", str(r))
    r = create(pin(MUX_A_PIN), pin(MUX_B_PIN), delay=101)
    check("power mux: CREATE switch delay 101 -> ERR_DEV_RANGE", err_cause(r) == "ERR_DEV_RANGE", str(r))
    r = create(PIN_NONE, pin(MUX_B_PIN))
    check("power mux: CREATE without pin A -> ERR_DEV_BAD_PIN", err_cause(r) == "ERR_DEV_BAD_PIN", str(r))
    r = create(pin(MUX_A_PIN), pin(MUX_A_PIN))
    check("power mux: CREATE one pin for both -> ERR_DEV_BAD_PIN", err_cause(r) == "ERR_DEV_BAD_PIN", str(r))
    r = create(struct.pack("<HB", 5, 0), pin(MUX_B_PIN))
    check("power mux: CREATE pin on a missing device -> ERR_DEV_NOT_FOUND", err_cause(r) == "ERR_DEV_NOT_FOUND", str(r))
    r = create(pin(MUX_A_PIN), pin(22))  # GPIO 22 doesn't exist: fails at the pin, after pin A was built
    check("power mux: CREATE with a bad pin B fails inside ERR_DEV_INSTALL",
          "ERR_DEV_INSTALL" in (r.record.names() if r and r.record else []), str(r))
    r = dev(link, MUX_ID, OP_INFO)
    check("power mux: a failed CREATE leaves no device", err_cause(r) == "ERR_DEV_NOT_FOUND", str(r))
    r = dev(link, GPIO_ID, IO_MODE, bytes([MUX_A_PIN, 3]))
    check("power mux: the failed CREATE released pin A (MODE works again)", r is not None and r.ok, str(r))
    r = dev(link, GPIO_ID, IO_RESET, bytes([MUX_A_PIN]))
    check("power mux: IO RESET pin A", r is not None and r.ok, str(r))

    r = create(pin(MUX_A_PIN), pin(MUX_B_PIN))
    check(f"power mux: CREATE id {MUX_ID}", r is not None and r.ok, str(r))
    if r is None or not r.ok:
        dev(link, GPIO_ID, OP_REMOVE)
        return
    r = dev(link, MUX_ID, OP_INFO)
    check("power mux: INFO type 0x004A", r is not None and r.ok and r.data == struct.pack("<HB", MUX_TYPE, 0), str(r))
    ops = ops_of(dev(link, MUX_ID, OP_OPS))
    check("power mux: OPS = lifecycle + SOURCE (read + write), no IO",
          ops.get(SUPPLY_SOURCE) == SIDE_READ | SIDE_WRITE and {OP_SUSPEND, OP_RESUME} <= ops.keys() and IO_LEVEL not in ops,
          str({hex(k): v for k, v in ops.items()}))
    check("power mux: both sources off after CREATE", source() == 0)
    if target != "qemu":
        check("power mux: both enable pins low after CREATE", levels() == (0, 0), str(levels()))

    r = select(3)
    check("power mux: SOURCE write 3 -> ERR_DEV_RANGE", err_cause(r) == "ERR_DEV_RANGE", str(r))
    r = select()
    check("power mux: SOURCE write without args -> ERR_CMD_BAD_LEN", err_cause(r) == "ERR_CMD_BAD_LEN", str(r))
    r = set_level(link, GPIO_ID, MUX_A_PIN, 1)
    check("power mux: its pin is locked on the GPIO device -> ERR_DEV_PIN_LOCKED", err_cause(r) == "ERR_DEV_PIN_LOCKED", str(r))
    r = dev(link, GPIO_ID, OP_REMOVE)
    check("power mux: REMOVE of the GPIO device while the mux uses it -> ERR_DEV_IN_USE", err_cause(r) == "ERR_DEV_IN_USE", str(r))

    r = select(1)
    check("power mux: SELECT A: ACK without data", r is not None and r.ok and r.data == b"", str(r))
    check("power mux: source = A", source() == 1)
    if target != "qemu":
        check("power mux: A pin high, B pin low", levels() == (1, 0), str(levels()))
    r = select(1)
    check("power mux: SELECT A again does nothing", r is not None and r.ok and source() == 1, str(r))
    r = select(2)
    check("power mux: SELECT B (A off first)", r is not None and r.ok, str(r))
    check("power mux: source = B", source() == 2)
    if target != "qemu":
        check("power mux: A pin low, B pin high", levels() == (0, 1), str(levels()))

    r = dev(link, MUX_ID, OP_SUSPEND)
    check("power mux: SUSPEND", r is not None and r.ok, str(r))
    check("power mux: suspended = both off", source() == 0)
    if target != "qemu":
        check("power mux: suspended = both pins low", levels() == (0, 0), str(levels()))
    r = select(1)
    check("power mux: SELECT while suspended -> ERR_DEV_SUSPENDED", err_cause(r) == "ERR_DEV_SUSPENDED", str(r))
    r = dev(link, MUX_ID, OP_RESUME)
    check("power mux: RESUME", r is not None and r.ok, str(r))
    check("power mux: RESUME restores the wanted source (B)", source() == 2)

    r = dev(link, MUX_ID, OP_RESET)
    check("power mux: RESET", r is not None and r.ok, str(r))
    check("power mux: RESET = both off", source() == 0)
    r = dev(link, MUX_ID, OP_SUSPEND)
    r = dev(link, MUX_ID, OP_RESUME)
    check("power mux: after RESET nothing is wanted: RESUME stays off", source() == 0)

    r = select(1)
    r = dev(link, MUX_ID, OP_REMOVE)
    check("power mux: REMOVE", r is not None and r.ok, str(r))
    if target != "qemu":
        r = get_level(link, GPIO_ID, MUX_A_PIN)
        check("power mux: REMOVE released pin A (unconfigured again)", err_cause(r) == "ERR_IO_PIN_UNCONFIGURED", str(r))
    r = dev(link, MUX_ID, OP_INFO)
    check("power mux: INFO after REMOVE -> ERR_DEV_NOT_FOUND", err_cause(r) == "ERR_DEV_NOT_FOUND", str(r))
    r = dev(link, GPIO_ID, OP_REMOVE)
    check("power mux: REMOVE the GPIO device", r is not None and r.ok, str(r))


INA_TYPE = 0x0044
AP_TYPE = 0x0045
TCA_TYPE = 0x0042


def test_device(link: Link) -> None:
    r = dev(link, MISSING_ID, OP_INFO)
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
    check("CREATE ESP32-S3 GPIO at id 9", r is not None and r.ok, str(r))
    r = create_pca(link, 3, 1, 0x40, oe=struct.pack("<HB", 9, 4))  # nothing on bus 1: fails later, at the probe
    check("CREATE OE on a higher id gets past the dependency check",
          r is not None and not r.ok and err_cause(r) not in ("ERR_DEV_NOT_FOUND", "ERR_DEV_IN_USE"), str(r))
    r = dev(link, 9, OP_REMOVE)
    check("failed CREATE left no dependency: REMOVE GPIO 9", r is not None and r.ok, str(r))
    r = create_pca(link, 3, 1, 0x40)  # nothing on bus 1
    names = r.record.names() if r and r.record else []
    check("CREATE with no chip fails inside ERR_DEV_INSTALL", "ERR_DEV_INSTALL" in names, str(r))
    print(f"      chain: {' -> '.join(names)}")

    # INA3221 (0x0044): cfg = bus, addr, crit pin (u16 device, u8 pin), warn pin, inverted mask
    none = struct.pack("<HB", 0xFFFF, 0xFF)

    def create_ina(bus: int, addr: int, mask: int = 0, crit: bytes = none, warn: bytes = none):
        return dev(link, 4, OP_CREATE, struct.pack("<H", INA_TYPE) + bytes([bus, addr]) + crit + warn + bytes([mask]) + struct.pack("<H", 10))

    r = create_ina(7, 0x40)
    check("INA3221 CREATE bad bus -> ERR_DEV_RANGE", err_cause(r) == "ERR_DEV_RANGE", str(r))
    r = create_ina(1, 0x50)
    check("INA3221 CREATE address 0x50 -> ERR_DEV_RANGE", err_cause(r) == "ERR_DEV_RANGE", str(r))
    r = create_ina(1, 0x40, mask=8)
    check("INA3221 CREATE inverted mask 8 -> ERR_DEV_RANGE", err_cause(r) == "ERR_DEV_RANGE", str(r))
    r = create_ina(1, 0x40, crit=struct.pack("<HB", 5, 0))
    check("INA3221 CREATE alert pin on a missing device -> ERR_DEV_NOT_FOUND", err_cause(r) == "ERR_DEV_NOT_FOUND", str(r))
    r = create_ina(1, 0x40)  # nothing on bus 1
    names = r.record.names() if r and r.record else []
    check("INA3221 CREATE with no chip fails inside ERR_DEV_INSTALL", "ERR_DEV_INSTALL" in names, str(r))
    r = dev(link, 4, OP_INFO)
    check("failed INA3221 CREATE leaves no device", err_cause(r) == "ERR_DEV_NOT_FOUND", str(r))

    # AP33772S (0x0045): cfg = bus, addr, intr pin (u16 device, u8 pin)
    def create_ap(bus: int, addr: int, intr: bytes = none):
        return dev(link, 4, OP_CREATE, struct.pack("<H", AP_TYPE) + bytes([bus, addr]) + intr)

    r = create_ap(7, 0x52)
    check("AP33772S CREATE bad bus -> ERR_DEV_RANGE", err_cause(r) == "ERR_DEV_RANGE", str(r))
    r = create_ap(1, 0x40)
    check("AP33772S CREATE address 0x40 -> ERR_DEV_RANGE", err_cause(r) == "ERR_DEV_RANGE", str(r))
    r = create_ap(1, 0x52, intr=struct.pack("<HB", 5, 0))
    check("AP33772S CREATE intr pin on a missing device -> ERR_DEV_NOT_FOUND", err_cause(r) == "ERR_DEV_NOT_FOUND", str(r))
    r = create_ap(1, 0x52)  # nothing on bus 1
    names = r.record.names() if r and r.record else []
    check("AP33772S CREATE with no chip fails inside ERR_DEV_INSTALL", "ERR_DEV_INSTALL" in names, str(r))
    r = dev(link, 4, OP_INFO)
    check("failed AP33772S CREATE leaves no device", err_cause(r) == "ERR_DEV_NOT_FOUND", str(r))

    # TCA6424A (0x0042): cfg = bus, addr, intr pin (u16 device, u8 pin), rst pin
    def create_tca(bus: int, addr: int, intr: bytes = none, rst: bytes = none):
        return dev(link, 4, OP_CREATE, struct.pack("<H", TCA_TYPE) + bytes([bus, addr]) + intr + rst)

    r = create_tca(7, 0x23)
    check("TCA6424A CREATE bad bus -> ERR_DEV_RANGE", err_cause(r) == "ERR_DEV_RANGE", str(r))
    r = create_tca(1, 0x21)
    check("TCA6424A CREATE address 0x21 -> ERR_DEV_RANGE", err_cause(r) == "ERR_DEV_RANGE", str(r))
    r = create_tca(1, 0x23, intr=struct.pack("<HB", 5, 0))
    check("TCA6424A CREATE intr pin on a missing device -> ERR_DEV_NOT_FOUND", err_cause(r) == "ERR_DEV_NOT_FOUND", str(r))
    r = create_tca(1, 0x23, rst=struct.pack("<HB", 5, 0))
    check("TCA6424A CREATE rst pin on a missing device -> ERR_DEV_NOT_FOUND", err_cause(r) == "ERR_DEV_NOT_FOUND", str(r))
    r = create_tca(1, 0x23)  # nothing on bus 1
    names = r.record.names() if r and r.record else []
    check("TCA6424A CREATE with no chip fails inside ERR_DEV_INSTALL", "ERR_DEV_INSTALL" in names, str(r))
    r = dev(link, 4, OP_INFO)
    check("failed TCA6424A CREATE leaves no device", err_cause(r) == "ERR_DEV_NOT_FOUND", str(r))
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
    r = dev(link, PCA_ID, IO_PWM_FREQ, struct.pack("<BI", 0, 50))
    check("PWM_FREQ 50 Hz", r is not None and r.ok, str(r))
    r = dev(link, PCA_ID, IO_PWM_FREQ, struct.pack("<BI", 0, 5000))
    check("PWM_FREQ 5000 Hz -> ERR_DEV_RANGE", err_cause(r) == "ERR_DEV_RANGE", str(r))
    r = dev(link, PCA_ID, IO_PWM_DUTY, struct.pack("<BI", 1, 3000))
    check("PWM_DUTY ch1 3000", r is not None and r.ok, str(r))
    r = get_level(link, PCA_ID, 1)
    check("LEVEL read ch1 = {1, 1}", r is not None and r.ok and r.data == bytes([1, 1]), str(r))
    r = dev(link, PCA_ID, IO_TOGGLE, bytes([1]))
    check("TOGGLE ch1", r is not None and r.ok, str(r))
    r = get_level(link, PCA_ID, 1)
    check("LEVEL read ch1 = {1, 0}", r is not None and r.ok and r.data == bytes([1, 0]), str(r))
    r = dev(link, PCA_ID, IO_PWM_DUTY, struct.pack("<BI", 1, 4096))
    check("duty 4096 -> ERR_DEV_RANGE", err_cause(r) == "ERR_DEV_RANGE", str(r))
    r = dev(link, PCA_ID, IO_PWM_DUTY, struct.pack("<BI", 16, 100))
    check("channel 16 -> ERR_DEV_BAD_PIN", err_cause(r) == "ERR_DEV_BAD_PIN", str(r))
    r = dev(link, PCA_ID, 0x0300)
    names = r.record.names() if r and r.record else []
    check("contract the device lacks -> ERR_DEV_UNKNOWN_OP", err_cause(r) == "ERR_DEV_UNKNOWN_OP", str(r))
    check("unknown op chain: KEY 05 -> DEV_ID -> DEV_OP -> UNKNOWN_OP",
          names == ["ERR_ROUTER_KEY", "ERR_DEV_ID", "ERR_DEV_OP", "ERR_DEV_UNKNOWN_OP"], str(names))
    r = dev(link, PCA_ID, IO_VOLTAGE, bytes([1]), dir=READ)
    check("IO op the PCA9685 lacks (VOLTAGE read) -> ERR_DEV_UNKNOWN_OP", err_cause(r) == "ERR_DEV_UNKNOWN_OP", str(r))
    r = dev(link, PCA_ID, 0xFF00, dir=WRITE)
    check("own op the device lacks -> ERR_DEV_UNKNOWN_OP", err_cause(r) == "ERR_DEV_UNKNOWN_OP", str(r))
    r = dev(link, PCA_ID, OP_INFO, dir=WRITE)
    check("INFO sent as a write -> ERR_CMD_DIRECTION", err_cause(r) == "ERR_CMD_DIRECTION", str(r))
    r = dev(link, PCA_ID, IO_PWM_DUTY, struct.pack("<BI", 1, 5000))
    names = r.record.names() if r and r.record else []
    check("failing op -> ERR_DEV_OP route node", names[2:3] == ["ERR_DEV_OP"], str(names))

    ops = ops_of(dev(link, PCA_ID, OP_OPS))
    check("OPS lists the device's op table with its sides",
          ops.get(OP_OPS) == SIDE_READ and ops.get(OP_INFO) == SIDE_READ and ops.get(IO_LEVEL) == SIDE_READ | SIDE_WRITE
          and ops.get(PINS_LOCK) == SIDE_WRITE and IO_VOLTAGE not in ops, str({hex(k): v for k, v in ops.items()}))
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
    r = set_level(link, PCA_ID, 8, 1)
    check("IO write of a locked pin -> ERR_DEV_PIN_LOCKED", err_cause(r) == "ERR_DEV_PIN_LOCKED", str(r))
    r = get_level(link, PCA_ID, 8)
    check("IO read of a locked pin is allowed", r is not None and r.ok, str(r))
    r = dev(link, PCA_ID, PINS_LOCKED_LEVEL, bytes([8, 1]))
    check("LOCKED_LEVEL drives the locked pin", r is not None and r.ok, str(r))
    r = dev(link, PCA_ID, PINS_LOCK, bytes([16]))
    check("LOCK ch16 -> ERR_DEV_BAD_PIN", err_cause(r) == "ERR_DEV_BAD_PIN", str(r))
    r = dev(link, PCA_ID, PINS_UNLOCK, bytes([8]))
    check("PINS UNLOCK ch8", r is not None and r.ok, str(r))
    r = dev(link, PCA_ID, PINS_LOCKED)
    check("PINS LOCKED empty again", r is not None and r.ok and r.data == bytes(8), str(r))
    r = set_level(link, PCA_ID, 8, 0)
    check("IO write after UNLOCK", r is not None and r.ok, str(r))

    r = dev(link, PCA_ID, OP_SUSPEND)
    check("SUSPEND", r is not None and r.ok, str(r))
    r = dev(link, PCA_ID, OP_INFO)
    check("INFO: suspended", r is not None and r.ok and r.data == struct.pack("<HB", PCA_TYPE, 1), str(r))
    r = set_level(link, PCA_ID, 0, 1)
    check("write while suspended -> ERR_DEV_SUSPENDED", err_cause(r) == "ERR_DEV_SUSPENDED", str(r))
    r = get_level(link, PCA_ID, 1)
    check("read while suspended is allowed", r is not None and r.ok, str(r))
    r = dev(link, PCA_ID, OP_RESUME)
    check("RESUME", r is not None and r.ok, str(r))

    t = link.mark()
    r = link.request(bytes([KEY_SYS, SYS_STOP]))
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
A_READ, A_INFO, A_LIST, A_REMOVE, A_ABORT, A_RUN_ALL = 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0D
# Frames as a record holds them: [key][dir][op][args] (built with frame(), not passed through request()).
CONN_INFO_KEYS = bytes([KEY_CONN, 0x07])
CONN_INFO = frame(CONN_INFO_KEYS, READ)  # a read that succeeds on any target: runs in an image, a recording skips it
# Record IDs are u32 = [class u8][index u24] (sys_actions.h): 0 user, 1 system, 2 device
CLASS_USER, CLASS_SYSTEM, CLASS_DEVICE = 0, 1, 2
T1, T2, LOOP, KEEP = 1, 2, 200, 255  # user class
T3 = (CLASS_SYSTEM << 24) | 0xFF03  # any class takes any record (a system-class ID no module uses)
T4 = (CLASS_DEVICE << 24) | 0x0063  # the record of device 99
SPARE = 9


def u32(n: int) -> bytes:
    return struct.pack("<I", n)


def act(link: Link, op: int, payload: bytes = b"") -> Response | None:
    return link.request(bytes([KEY_ACTIONS, op]), payload=payload)


def image(*frames: bytes) -> bytes:
    return b"".join(struct.pack("<H", len(f)) + f for f in frames)


def run_frame(aid: int) -> bytes:
    """actions RUN of `aid` as an image holds it."""
    return frame(bytes([KEY_ACTIONS, A_RUN]), WRITE) + u32(aid)


def upload(link: Link, aid: int, img: bytes, chunk: int = 64, crc: int | None = None) -> Response | None:
    r = act(link, A_BEGIN, struct.pack("<IH", aid, len(img)))
    if r is None or not r.ok:
        return r
    for off in range(0, len(img), chunk):
        r = act(link, A_WRITE, struct.pack("<H", off) + img[off : off + chunk])
        if r is None or not r.ok:
            return r
    return act(link, A_COMMIT, struct.pack("<I", zlib.crc32(img) if crc is None else crc))


def info(link: Link, aid: int) -> tuple[int, int, int] | None:
    r = act(link, A_INFO, u32(aid))
    if r is None or not r.ok or len(r.data) != 8:
        return None
    return struct.unpack("<HHI", r.data)


def readback(link: Link, aid: int) -> list[bytes] | None:
    """READ by offset from 0 up to INFO's size, the CRC checked, split into its frames (each exactly as recorded);
    None when a read fails or the image doesn't match."""
    i = info(link, aid)
    if i is None:
        return None
    size, _, crc = i
    img = b""
    while len(img) < size:
        r = act(link, A_READ, u32(aid) + struct.pack("<H", len(img)))
        if r is None or not r.ok or not r.data:
            return None
        img += r.data
    if len(img) != size or zlib.crc32(img) != crc:
        return None
    frames, pos = [], 0
    while pos < len(img):
        (n,) = struct.unpack_from("<H", img, pos)
        frames.append(img[pos + 2 : pos + 2 + n])
        pos += 2 + n
    return frames


def actions_list(link: Link, start: int = 0) -> tuple[int, list[int]] | None:
    r = act(link, A_LIST, u32(start))
    if r is None or not r.ok or len(r.data) < 2 or (len(r.data) - 2) % 4:
        return None
    n = (len(r.data) - 2) // 4
    return struct.unpack_from("<H", r.data)[0], list(struct.unpack_from(f"<{n}I", r.data, 2))


def test_actions(link: Link) -> None:
    act(link, A_ABORT)
    for aid in (T1, T2, T3, T4, LOOP, KEEP, SPARE):
        act(link, A_REMOVE, u32(aid))

    # Record, executing: only the client's successful writes are kept (no reads, no sys frames)
    r = act(link, A_REC_START, u32(T1) + bytes([1]))
    check("actions RECORD_START 1 (execute)", r is not None and r.ok, str(r))
    r = act(link, A_REC_START, u32(SPARE) + bytes([1]))
    check("second RECORD_START -> ERR_ACTION_BUSY", err_cause(r) == "ERR_ACTION_BUSY", str(r))
    r = log_set(link, *LOG_DEFAULT)
    check("recorded frame runs (log CONFIG write answers ACK)", r is not None and r.ok and r.data == b"", str(r))
    r = link.request(CONN_INFO_KEYS)
    check("a read runs during a recording (conn INFO answers data)", r is not None and r.ok and len(r.data) > 1, str(r))
    link.request(bytes([0x7E]))  # fails: not recorded
    ping(link)  # sys: not recorded
    r = act(link, A_REC_STOP)
    want = image(LOG_DEFAULT_FRAME)
    ok = r is not None and r.ok and len(r.data) == 8
    check("RECORD_STOP", ok, str(r))
    if ok:
        b, c, crc = struct.unpack("<HHI", r.data)
        check("recording kept 1 frame (read, failed and sys frames skipped)", (b, c) == (len(want), 1), f"{b} B, {c} frames")
        check("RECORD_STOP crc = zlib crc32", crc == zlib.crc32(want), str(r))
    got = readback(link, T1)
    check("READ by offset = the image of 1 frame, as recorded (with dir)", got == [LOG_DEFAULT_FRAME], str(got))
    two = [CONN_INFO, CONN_INFO + b"\x00"]
    r = upload(link, T3, image(*two))
    got = readback(link, T3)
    check("upload 2 frames to a system-class ID, READ = the 2 frames in order, as uploaded", r is not None and r.ok and got == two, str(got))
    size = len(image(*two))
    r = act(link, A_READ, u32(T3) + struct.pack("<H", 3))
    check("READ from offset 3 = the image from byte 3", r is not None and r.ok and r.data == image(*two)[3:], str(r))
    r = act(link, A_READ, u32(T3) + struct.pack("<H", size))
    check("READ at the image's end = empty answer", r is not None and r.ok and r.data == b"", str(r))
    r = act(link, A_READ, u32(T3) + struct.pack("<H", size + 1))
    check("READ past the image -> ERR_ACTION_OFFSET", err_cause(r) == "ERR_ACTION_OFFSET", str(r))
    r = act(link, A_READ, u32(KEEP) + struct.pack("<H", 0))
    check("READ of a missing record -> ERR_ACTION_NOT_FOUND", err_cause(r) == "ERR_ACTION_NOT_FOUND", str(r))
    r = act(link, A_READ, bytes([KEEP]))
    check("READ with a 1-byte ID -> ERR_CMD_BAD_LEN", err_cause(r) == "ERR_CMD_BAD_LEN", str(r))
    r = act(link, A_RUN, u32(T1))
    check("RUN 1", r is not None and r.ok, str(r))

    # Record without executing: writes are taken (answered OK, not run)
    r = act(link, A_REC_START, u32(T2) + bytes([0]))
    check("RECORD_START 2 (no execute)", r is not None and r.ok, str(r))
    r = dev(link, MISSING_ID, OP_RESUME)  # no such device: would fail if it ran
    check("frame taken without running (OK, no data)", r is not None and r.ok and r.data == b"", str(r))
    r = act(link, A_REC_STOP)
    check("RECORD_STOP 2: 1 frame", r is not None and r.ok and len(r.data) == 8 and struct.unpack_from("<HHI", r.data)[1] == 1, str(r))
    r = act(link, A_RUN, u32(T2))
    names = r.record.names() if r and r.record else []
    check("RUN 2 stops at the failing frame: STEP over its chain", "ERR_ACTION_STEP" in names and err_cause(r) == "ERR_DEV_NOT_FOUND", str(names))
    step = next((n for n in r.record.nodes if n.name == "ERR_ACTION_STEP"), None) if r and r.record else None
    check("STEP payload: id 2 (u32), step 0", step is not None and step.data == u32(T2) + bytes([0, 0]), str(step))

    r = act(link, A_REC_START, u32(SPARE) + bytes([1]))
    r = act(link, A_REC_STOP)
    check("empty recording -> ERR_ACTION_EMPTY", err_cause(r) == "ERR_ACTION_EMPTY", str(r))

    # Upload: full image, nested RUN
    img = image(CONN_INFO, run_frame(T1), CONN_INFO)
    r = upload(link, T4, img, chunk=5)
    check("upload to a device-class ID (3 frames, 5-byte chunks)", r is not None and r.ok, str(r))
    i = info(link, T4)
    check("INFO of it", i == (len(img), 3, zlib.crc32(img)), str(i))
    r = act(link, A_RUN, u32(T4))
    check("RUN it (runs 1 nested)", r is not None and r.ok, str(r))
    r = upload(link, SPARE, img, crc=1)
    check("upload with a wrong crc -> ERR_ACTION_CRC", err_cause(r) == "ERR_ACTION_CRC", str(r))
    r = upload(link, SPARE, image(CONN_INFO)[:-1] + b"\x00\x05")
    check("upload with broken framing -> ERR_ACTION_BAD_FRAME", err_cause(r) == "ERR_ACTION_BAD_FRAME", str(r))
    r = act(link, A_BEGIN, struct.pack("<IH", SPARE, 8))
    r = act(link, A_WRITE, struct.pack("<H", 4) + b"xx")
    check("WRITE out of order -> ERR_ACTION_OFFSET", err_cause(r) == "ERR_ACTION_OFFSET", str(r))
    r = act(link, A_COMMIT, struct.pack("<I", 0))
    check("COMMIT before every byte -> ERR_ACTION_INCOMPLETE", err_cause(r) == "ERR_ACTION_INCOMPLETE", str(r))
    r = act(link, A_INFO, u32(SPARE))
    check("failed uploads store nothing", err_cause(r) == "ERR_ACTION_NOT_FOUND", str(r))
    r = act(link, A_BEGIN, struct.pack("<IH", SPARE, 20000))
    check("BEGIN over the limit -> ERR_ACTION_TOO_BIG", err_cause(r) == "ERR_ACTION_TOO_BIG", str(r))
    r = act(link, A_BEGIN, struct.pack("<IH", T4, 513))
    check("BEGIN of 513 B in the device class -> ERR_ACTION_TOO_BIG (limit 512)", err_cause(r) == "ERR_ACTION_TOO_BIG", str(r))
    r = act(link, A_BEGIN, struct.pack("<IH", T4, 512))
    check("BEGIN of 512 B in the device class is accepted", r is not None and r.ok, str(r))
    act(link, A_ABORT)
    r = act(link, A_BEGIN, struct.pack("<IH", SPARE, 513))
    check("BEGIN of 513 B in the user class is accepted (limit 4096)", r is not None and r.ok, str(r))
    act(link, A_ABORT)

    r = upload(link, LOOP, image(run_frame(LOOP)))
    check("upload 200 (runs itself)", r is not None and r.ok, str(r))
    r = act(link, A_RUN, u32(LOOP))
    names = r.record.names() if r and r.record else []
    check("self-RUN stops at ERR_ACTION_DEPTH", err_cause(r) == "ERR_ACTION_DEPTH" and names.count("ERR_ACTION_STEP") >= 2, str(names))

    # RUN stops at a failing frame, RUN_ALL runs them all and returns the first failure
    r = upload(link, SPARE, image(frame(bytes([0x7E]), WRITE), log_frame(2, 1, 1, 1, 1)))
    log_set(link, *LOG_DEFAULT)
    r = act(link, A_RUN, u32(SPARE))
    check("RUN stops at the failing frame: the next one did not run",
          err_cause(r) == "ERR_ROUTER_UNKNOWN_KEY" and log_get(link) == LOG_DEFAULT, f"{r} {log_get(link)}")
    r = act(link, A_RUN_ALL, u32(SPARE))
    check("RUN_ALL runs the frame after the failing one and returns the first failure",
          err_cause(r) == "ERR_ROUTER_UNKNOWN_KEY" and "ERR_ACTION_STEP" in (r.record.names() if r and r.record else []) and (log_get(link) or (0,))[0] == 2,
          f"{r} {log_get(link)}")
    log_set(link, *LOG_DEFAULT)
    act(link, A_REMOVE, u32(SPARE))

    # IDs and listing
    r = act(link, A_RUN, u32(77))
    check("RUN unknown id -> ERR_ACTION_NOT_FOUND", err_cause(r) == "ERR_ACTION_NOT_FOUND", str(r))
    r = act(link, A_RUN, u32(0))
    check("RUN id 0 -> ERR_ACTION_BAD_ID", err_cause(r) == "ERR_ACTION_BAD_ID", str(r))
    l = actions_list(link)
    ours = [T1, T2, LOOP, T3, T4]  # ascending: user 1, 2, 200, then system, then device class
    got_ids = [x for x in (l[1] if l else []) if x in ours]
    check("LIST: ascending by u32 ID across classes", got_ids == sorted(ours), str(l))
    l2 = actions_list(link, T3)
    check("LIST from the system-class ID: only IDs >= it", l2 is not None and T3 in l2[1] and T4 in l2[1] and T1 not in l2[1], str(l2))
    check("LIST total counts every stored record", l is not None and l[0] >= len(ours), str(l))

    for aid in (T1, T2, T3, T4, LOOP):
        r = act(link, A_REMOVE, u32(aid))
        check(f"REMOVE {aid:#x}", r is not None and r.ok, str(r))
    r = act(link, A_REMOVE, u32(T1))
    check("REMOVE again -> ERR_ACTION_NOT_FOUND", err_cause(r) == "ERR_ACTION_NOT_FOUND", str(r))
    r = upload(link, KEEP, image(CONN_INFO))
    check("upload 255 (checked after the restart)", r is not None and r.ok, str(r))


KEY_PROJECT = 0x0C
P_BEGIN, P_WRITE, P_COMMIT, P_ABORT, P_INFO, P_RUN, P_ERASE, P_OPTIONS, P_SECTION, P_LOAD = 1, 2, 3, 4, 5, 6, 7, 8, 0x10, 0x12
BOOT_NONE, BOOT_LOAD, BOOT_RUN = 0, 1, 2  # sys_project_boot_e
CLASS_PROJECT = 3
PROJECT_RECORD = CLASS_PROJECT << 24
SEC_PROGRAM, SEC_START = 0, 0xFFFFFFFF
T5 = (CLASS_SYSTEM << 24) | 0xFF05  # a system-class ID no module uses: a project section's record


def prj(link: Link, op: int, payload: bytes = b"", dir: int | None = None) -> Response | None:
    return link.request(bytes([KEY_PROJECT, op]), payload=payload, dir=dir)


def section(sid: int, *frames: bytes) -> bytes:
    return image(frame(bytes([KEY_PROJECT, P_SECTION]), WRITE) + u32(sid), *frames)


def prj_save(link: Link, stream: bytes, boot: int = BOOT_NONE, chunk: int = 400, crc: int | None = None) -> Response | None:
    r = prj(link, P_BEGIN, struct.pack("<IB", len(stream), boot))
    if r is None or not r.ok:
        return r
    for off in range(0, len(stream), chunk):
        r = prj(link, P_WRITE, struct.pack("<I", off) + stream[off : off + chunk])
        if r is None or not r.ok:
            return r
    return prj(link, P_COMMIT, struct.pack("<I", zlib.crc32(stream) if crc is None else crc))


def prj_info(link: Link) -> tuple[int, int, int, int, int] | None:
    r = prj(link, P_INFO)
    if r is None or not r.ok or len(r.data) != 11:
        return None
    return struct.unpack("<BBBII", r.data)  # stored, boot, bank, crc32, bytes


def test_project(link: Link) -> None:
    """The project pipeline (SYS_PROJECT.MD). Not run on a board yet."""
    prj(link, P_ABORT)
    prj(link, P_ERASE)
    act(link, A_REMOVE, u32(T5))
    i = prj_info(link)
    check("project INFO with none stored", i is not None and i[0] == 0, str(i))
    r = prj(link, P_RUN)
    check("RUN with none -> ERR_PROJECT_NOT_STORED", err_cause(r) == "ERR_PROJECT_NOT_STORED", str(r))

    # frame before a section / program not last / bad CRC: nothing stored, nothing staged
    r = prj_save(link, image(CONN_INFO))
    check("frame before SECTION -> ERR_PROJECT_NO_SECTION", err_cause(r) == "ERR_PROJECT_NO_SECTION", str(r))
    r = prj_save(link, section(SEC_PROGRAM, CONN_INFO) + section(T5, CONN_INFO))
    check("section after the program -> ERR_PROJECT_ORDER", err_cause(r) == "ERR_PROJECT_ORDER", str(r))
    r = prj_save(link, section(SEC_START, CONN_INFO) + section(SEC_PROGRAM, CONN_INFO))
    check("program after the start -> ERR_PROJECT_ORDER", err_cause(r) == "ERR_PROJECT_ORDER", str(r))
    r = prj(link, P_BEGIN, struct.pack("<IB", 8, 3))
    check("BEGIN with boot 3 -> ERR_PROJECT_BAD_BOOT", err_cause(r) == "ERR_PROJECT_BAD_BOOT", str(r))
    stream = section(T5, CONN_INFO) + section(SEC_PROGRAM, *([CONN_INFO] * 1100))  # 2 chunks of 4 KB
    r = prj_save(link, stream, crc=1)
    check("bad CRC -> ERR_PROJECT_CRC", err_cause(r) == "ERR_PROJECT_CRC", str(r))
    check("nothing stored after the failures", prj_info(link) is not None and prj_info(link)[0] == 0)
    check("failed saves leave no staged record", info(link, T5) is None)

    # a good save, run at once (boot RUN)
    r = prj_save(link, stream, boot=BOOT_RUN)
    check("project save + run", r is not None and r.ok, str(r))
    i = prj_info(link)
    check("INFO: stored, boot RUN, crc and length of the stream",
          i is not None and i[0] == 1 and i[1] == BOOT_RUN and i[3] == zlib.crc32(stream) and i[4] == len(stream), str(i))
    want = image(CONN_INFO)  # 2 + 3 bytes: [u16 len][key][dir][op]
    check("the section's record is stored under its ID", info(link, T5) == (len(want), 1, zlib.crc32(want)), str(info(link, T5)))
    listing = actions_list(link, PROJECT_RECORD)
    ids = [x for x in (listing[1] if listing else []) if x >> 24 == CLASS_PROJECT]
    bank = prj_info(link)[2]
    base = ((bank + 1) << 16) | (CLASS_PROJECT << 24)
    # the record, the list of chunks, 2 chunks: IDs are given one after another in the order they are needed, and the staged
    # section (n = 1) is needed first, so the chunks are n = 2 and 3 (SYS_PROJECT.MD "IDs one after another")
    want = [PROJECT_RECORD, base, base + 2, base + 3]
    check("program records: list + 2 chunks in order", ids == want, f"{[hex(x) for x in ids]} want {[hex(x) for x in want]}")
    r = prj(link, P_RUN)
    check("project RUN", r is not None and r.ok, str(r))
    r = prj(link, P_LOAD)
    check("project LOAD", r is not None and r.ok, str(r))

    # a second save (with a start section, nothing at start) goes to the other bank and the old bank's records go
    r = prj_save(link, section(T5, CONN_INFO, CONN_INFO) + section(SEC_PROGRAM, CONN_INFO) + section(SEC_START, CONN_INFO))
    check("second save", r is not None and r.ok, str(r))
    i2 = prj_info(link)
    check("the other bank is the project's now", i2 is not None and i2[2] == 1 - bank, str(i2))
    listing = actions_list(link, PROJECT_RECORD)
    ids = [x for x in (listing[1] if listing else []) if x >> 24 == CLASS_PROJECT]
    base2 = ((2 - bank) << 16) | (CLASS_PROJECT << 24)
    check("old bank cleared: record + list + 1 chunk + the start", ids == [PROJECT_RECORD, base2, base2 + 2, base2 + 0xFFFF],
          str([hex(x) for x in ids]))
    check("INFO: nothing at start", prj_info(link)[1] == BOOT_NONE)
    r = prj(link, P_RUN)
    check("project RUN with its start section", r is not None and r.ok, str(r))

    # the boot option on its own (OPTIONS), and SECTION outside a stream
    r = prj(link, P_OPTIONS, bytes([BOOT_LOAD]), dir=WRITE)
    check("OPTIONS write boot LOAD", r is not None and r.ok and r.data == b"", str(r))
    r = prj(link, P_OPTIONS, dir=READ)
    check("OPTIONS read = LOAD, INFO shows it", r is not None and r.ok and r.data == bytes([BOOT_LOAD])
          and prj_info(link)[1] == BOOT_LOAD, f"{r} {prj_info(link)}")
    r = prj(link, P_OPTIONS, bytes([3]), dir=WRITE)
    check("OPTIONS write boot 3 -> ERR_PROJECT_BAD_BOOT", err_cause(r) == "ERR_PROJECT_BAD_BOOT", str(r))
    r = prj(link, P_OPTIONS, bytes([BOOT_NONE]), dir=WRITE)
    check("OPTIONS write boot NONE", r is not None and r.ok, str(r))
    r = prj(link, P_SECTION, u32(T5))
    check("SECTION as a command -> ERR_PROJECT_NOT_IN_STREAM", err_cause(r) == "ERR_PROJECT_NOT_IN_STREAM", str(r))

    r = prj(link, P_ERASE)
    check("ERASE", r is not None and r.ok and prj_info(link)[0] == 0, str(r))
    listing = actions_list(link, PROJECT_RECORD)
    check("ERASE leaves no project record", not [x for x in (listing[1] if listing else []) if x >> 24 == CLASS_PROJECT])
    act(link, A_REMOVE, u32(T5))


STREAM_EVENTS = 0x06


def event_records(link: Link, t: float) -> list[tuple]:
    """Records on the events stream since `t`: (source, event, channel, flags, value, t_ms)."""
    return [struct.unpack("<HHBBiQ", f[2:]) for f in link.stream_since(STREAM_EVENTS, t)
            if len(f) == 20 and f[0] == 0x07 and f[1] == 0x01]


def test_actions_after_restart(link: Link) -> None:
    i = info(link, KEEP)
    check("record survives the restart (id 255)", i is not None and i[1] == 1, str(i))
    r = act(link, A_REMOVE, u32(KEEP))
    check("REMOVE 255", r is not None and r.ok, str(r))


KEY_LOG = 0x08
LOG_CONFIG = 0x01  # read + write: the write sets the options, the read answers them
LOG_DEFAULT = (3, 1, 1, 1, 1)  # (level, mirror, trace, send_errors, send_logs)
# level INFO, mirror to the console, print error chains


def log_frame(*cfg: int) -> bytes:
    """The log CONFIG write as a record holds it."""
    return frame(bytes([KEY_LOG, LOG_CONFIG]), WRITE) + bytes(cfg)


LOG_DEFAULT_FRAME = log_frame(*LOG_DEFAULT)


def log_get(link: Link) -> tuple | None:
    r = link.request(bytes([KEY_LOG, LOG_CONFIG]), dir=READ)
    return tuple(r.data) if r is not None and r.ok and len(r.data) == 5 else None


def log_set(link: Link, level: int, mirror: int, trace: int, errors: int = 1, logs: int = 1) -> Response | None:
    return link.request(bytes([KEY_LOG, LOG_CONFIG]), payload=bytes([level, mirror, trace, errors, logs]), dir=WRITE)


def provoke_error(link: Link) -> tuple[float, list[str], list]:
    """A command with an unknown root key: the router answers it and err_report logs the chain (if it may) and sends the
    error record. Returns the mark, the console lines and the error records that came back."""
    t = link.mark()
    link.request(bytes([0x7E]))
    time.sleep(0.5)
    return t, link.logs_since(t), link.error_records_since(t)


def printed(lines: list[str]) -> bool:
    return any("ERR_ROUTER_UNKNOWN_KEY" in l for l in lines)


def recorded(recs: list) -> bool:
    return any("ERR_ROUTER_UNKNOWN_KEY" in x.names() for x in recs)


def test_log(link: Link) -> None:
    r = log_set(link, *LOG_DEFAULT)
    check("log CONFIG write defaults: ACK without data", r is not None and r.ok and r.data == b"", str(r))
    check("log CONFIG read = defaults", log_get(link) == LOG_DEFAULT, str(log_get(link)))

    r = log_set(link, 9, 1, 1)
    check("log level above the ceiling -> ERR_LOG_BAD_LEVEL", err_cause(r) == "ERR_LOG_BAD_LEVEL", str(r))
    r = log_set(link, 3, 2, 1)
    check("log flag 2 -> ERR_CMD_BAD_ARG", err_cause(r) == "ERR_CMD_BAD_ARG", str(r))
    r = link.request(bytes([KEY_LOG, LOG_CONFIG]), payload=bytes([3, 1]), dir=WRITE)
    check("log CONFIG write with 2 bytes -> ERR_CMD_BAD_LEN", err_cause(r) == "ERR_CMD_BAD_LEN", str(r))
    r = link.request(bytes([KEY_LOG, LOG_CONFIG]), payload=b"\x00", dir=READ)
    check("log CONFIG read with args -> ERR_CMD_BAD_LEN", err_cause(r) == "ERR_CMD_BAD_LEN", str(r))
    check("a refused write changes nothing", log_get(link) == LOG_DEFAULT, str(log_get(link)))

    _, lines, recs = provoke_error(link)
    check("defaults: the chain is printed on the console and the record is sent", printed(lines) and recorded(recs),
          f"printed {printed(lines)}, record {recorded(recs)}")

    log_set(link, 3, 1, 0)
    _, lines, recs = provoke_error(link)
    check("trace_errors off: no chain lines, the record is still sent", not printed(lines) and recorded(recs),
          f"printed {printed(lines)}, record {recorded(recs)}")

    log_set(link, 3, 0, 1)
    _, lines, recs = provoke_error(link)
    check("mirror_serial off: nothing on the console, the record is still sent", not lines and recorded(recs),
          f"{len(lines)} line(s), record {recorded(recs)}")

    log_set(link, 0, 1, 1)
    _, lines, recs = provoke_error(link)
    check("level 0 blocks every log line, the record is still sent", not lines and recorded(recs),
          f"{len(lines)} line(s), record {recorded(recs)}")

    log_set(link, 3, 1, 1, 0, 1)
    _, lines, recs = provoke_error(link)
    check("send_errors off: no record on the errors stream, the chain is still printed",
          not recorded(recs) and printed(lines), f"printed {printed(lines)}, record {recorded(recs)}")
    r = link.request(bytes([0x7E]))
    check("send_errors off: the command still gets its error response", r is not None and not r.ok, str(r))

    log_set(link, 3, 1, 1)
    _, lines, _ = provoke_error(link)
    check("back to defaults: the chain is printed again", printed(lines), str(lines))

    # Not stored by the module: a live write is gone after a restart (the app keeps it as a record, tested below).
    log_set(link, 2, 0, 0, 0, 0)
    link.request(bytes([KEY_SYS, SYS_RESTART]))
    up = False
    for _ in range(40):
        time.sleep(0.5)
        if ping(link) is not None:
            up = True
            break
    check("board answers after RESTART (silent: level 2, mirror off)", up)
    check("log options are the defaults after the restart (nothing is stored)", log_get(link) == LOG_DEFAULT,
          str(log_get(link)))
    r = log_set(link, *LOG_DEFAULT)
    check("log CONFIG write defaults again", r is not None and r.ok and log_get(link) == LOG_DEFAULT, str(r))


# System-class record IDs (sys_actions.h SYS_ACTIONS_ID_*)
REC_LOG, REC_PW_CFG, REC_PW_PSU, REC_PW_BATTERY, REC_PW_RESP = [(CLASS_SYSTEM << 24) | n for n in range(1, 6)]


def test_settings(link: Link) -> None:
    """Settings are records: the app saves the write frame under the module's ID; nothing replays by itself."""
    cfg = log_frame(2, 1, 0, 0, 1)
    act(link, A_REMOVE, u32(REC_LOG))
    r = upload(link, REC_LOG, image(cfg))
    check("settings: the log CONFIG write saved as record 0x01000001", r is not None and r.ok, str(r))
    got = readback(link, REC_LOG)
    check("settings: READ = the log CONFIG write exactly", got == [cfg], str(got))
    log_set(link, *LOG_DEFAULT)
    r = act(link, A_RUN, u32(REC_LOG))
    check("settings: RUN applies it (log level 2, errors off)", r is not None and r.ok and log_get(link) == (2, 1, 0, 0, 1), str(r))
    log_set(link, *LOG_DEFAULT)
    check("settings: the live write did not change the record", readback(link, REC_LOG) == [cfg])
    act(link, A_REMOVE, u32(REC_LOG))


OP_ERROR_POLICY = 0x0007  # read + write: the write takes the policy, the read answers it
IMP_NONE, IMP_LOW, IMP_MEDIUM, IMP_HIGH, IMP_CRITICAL = 0, 1, 2, 3, 4
ACT_MED, ACT_HIGH = 201, 200  # actions run by the policy: each sets another log level, which GET shows


def test_error_policy(link: Link) -> None:
    r = dev(link, GPIO_ID, OP_CREATE, struct.pack("<H", GPIO_TYPE))
    check("policy: ESP32-S3 GPIO present", r is not None and (r.ok or err_cause(r) == "ERR_DEV_ID_TAKEN"), str(r))
    for aid, level in ((ACT_HIGH, 2), (ACT_MED, 1)):
        act(link, A_REMOVE, u32(aid))
        r = upload(link, aid, image(log_frame(level, 1, 1, 1, 1)))
        check(f"policy: action {aid} sets log level {level}", r is not None and r.ok, str(r))
    log_set(link, *LOG_DEFAULT)

    def policy(importance: int, actions=(0, ACT_MED, ACT_HIGH, 0)) -> Response | None:
        return dev(link, GPIO_ID, OP_ERROR_POLICY, struct.pack("<B4I", importance, *actions), dir=WRITE)

    def policy_get() -> Response | None:
        return dev(link, GPIO_ID, OP_ERROR_POLICY, dir=READ)

    def level_after(provoke) -> int | None:
        log_set(link, *LOG_DEFAULT)
        provoke()
        time.sleep(0.5)  # the action is queued on the router
        got = log_get(link)
        return got[0] if got else None

    high_error = lambda: dev(link, GPIO_ID, IO_MODE, bytes([22, 3]))  # ERR_DEV_BAD_PIN, HIGH

    def medium_error():  # ERR_DEV_SUSPENDED, MEDIUM
        dev(link, GPIO_ID, OP_SUSPEND)
        dev(link, GPIO_ID, IO_MODE, bytes([OUT_PIN, 3]))
        dev(link, GPIO_ID, OP_RESUME)

    r = policy_get()
    check("policy read default: LOW, no actions", r is not None and r.ok and r.data == struct.pack("<B4I", IMP_LOW, 0, 0, 0, 0), str(r))
    ops = ops_of(dev(link, GPIO_ID, OP_OPS))
    check("OPS lists ERROR_POLICY with a read and a write side", ops.get(OP_ERROR_POLICY) == SIDE_READ | SIDE_WRITE,
          str({hex(k): v for k, v in ops.items()}))
    r = policy(5)
    check("policy importance 5 -> ERR_DEV_RANGE", err_cause(r) == "ERR_DEV_RANGE", str(r))
    r = dev(link, GPIO_ID, OP_ERROR_POLICY, bytes([IMP_HIGH, 0, 0, 0]), dir=WRITE)
    check("policy write with 4 bytes -> ERR_CMD_BAD_LEN", err_cause(r) == "ERR_CMD_BAD_LEN", str(r))

    policy(IMP_LOW)
    check("LOW: a HIGH error runs nothing", level_after(high_error) == 3)
    policy(IMP_MEDIUM)
    check("MEDIUM: a HIGH error runs the HIGH action", level_after(high_error) == 2)
    check("MEDIUM: a MEDIUM error runs nothing", level_after(medium_error) == 3)
    policy(IMP_HIGH)
    check("HIGH: a MEDIUM error runs the MEDIUM action", level_after(medium_error) == 1)
    policy(IMP_CRITICAL, (0, 0, 0, 0))
    check("CRITICAL without actions: nothing runs", level_after(high_error) == 3)
    policy(IMP_NONE)
    check("NONE: a HIGH error runs nothing", level_after(high_error) == 3)
    r = policy_get()
    check("policy read = what was written", r is not None and r.ok and r.data == struct.pack("<B4I", IMP_NONE, 0, ACT_MED, ACT_HIGH, 0), str(r))
    r = dev(link, GPIO_ID, IO_MODE, bytes([22, 3]))
    check("NONE: the command still gets its error response", err_cause(r) == "ERR_DEV_BAD_PIN", str(r))

    # A new instance starts at the defaults
    if ONBOARD:
        skip("REMOVE + CREATE: policy back to LOW, no actions", "the PCB's expander depends on the GPIO: it cannot be removed")
    else:
        dev(link, GPIO_ID, OP_REMOVE)
        dev(link, GPIO_ID, OP_CREATE, struct.pack("<H", GPIO_TYPE))
        r = policy_get()
        check("REMOVE + CREATE: policy back to LOW, no actions", r is not None and r.ok and r.data == struct.pack("<B4I", IMP_LOW, 0, 0, 0, 0), str(r))

    for aid in (ACT_HIGH, ACT_MED):
        act(link, A_REMOVE, u32(aid))
    log_set(link, *LOG_DEFAULT)


KEY_POWER = 0x0A
# contract_power_manager.json: CONFIG, PSU, BATTERY, PD and MATRIX have a read and a write side (the read answers what
# the write takes), STATUS is read only
PW_CONFIG, PW_PSU, PW_BATTERY, PW_STATUS, PW_PD, PW_MATRIX = 0x01, 0x03, 0x04, 0x07, 0x08, 0x09
PD_OFF, PD_MAX, PD_VOLTAGE = 0, 1, 2
SRC_UNKNOWN, SRC_USB, SRC_PSU, SRC_BATTERY = 0, 1, 2, 3
PW_FLAG_ENABLED, PW_FLAG_STRICT, PW_FLAG_INPUT_SEEN = 0x01, 0x02, 0x08
PW_RESPONSES_DEFAULT = bytes([0, 1, 0, 1, 0, 0, 3, 0, 3, 1, 0])  # by kind, index 0 unused
TPS_RAIL_A = 10  # a budgeted rail on the board; not created on the devkit
PW_TARGET_BOARD = 0xFFFF  # response target: the board row
VREG_VOLTAGE = 0x0501  # POWER_VREG VOLTAGE (write only)


def pw(link: Link, op: int, payload: bytes = b"", dir: int | None = None) -> Response | None:
    return link.request(bytes([KEY_POWER, op]), payload=payload, dir=dir)


def pw_get(link: Link, op: int) -> Response | None:
    return pw(link, op, dir=READ)


def pw_set(link: Link, op: int, payload: bytes) -> Response | None:
    return pw(link, op, payload, dir=WRITE)


def pw_config(enabled=1, strict=1, margin=20, source=0) -> bytes:
    return bytes([enabled, strict, margin, source])


def pw_status(link: Link) -> dict | None:
    r = pw(link, PW_STATUS)
    if r is None or not r.ok or len(r.data) != 40:
        return None
    f = struct.unpack("<BiiIIIIIBBIIB", r.data)
    keys = ("source", "input_mV", "input_mA", "source_mV", "source_mA", "budget_mW", "limit_mW", "allocated_mW",
            "battery_pct", "flags", "pd_mV", "pd_mA", "pd_type")
    return dict(zip(keys, f))


def wait_source(link: Link, want: int, timeout: float = 3.0) -> dict | None:
    end = time.time() + timeout
    st = None
    while time.time() < end:
        st = pw_status(link)
        if st and st["source"] == want:
            return st
        time.sleep(0.2)
    return st


def test_power(link: Link) -> None:
    pw_set(link, PW_PSU, struct.pack("<II", 0, 0))
    pw_set(link, PW_BATTERY, bytes([0, 0, 0, 0]))
    r = pw_set(link, PW_CONFIG, pw_config())
    check("power CONFIG write defaults: ACK without data", r is not None and r.ok and r.data == b"", str(r))
    r = pw_get(link, PW_CONFIG)
    check("power CONFIG read = defaults (on, strict, 20 %, source UNKNOWN)", r is not None and r.ok and r.data == pw_config(), str(r))
    for name, cfg in (("enabled 2", pw_config(enabled=2)), ("strict 2", pw_config(strict=2)),
                      ("margin 101", pw_config(margin=101)), ("source 4", pw_config(source=4))):
        r = pw_set(link, PW_CONFIG, cfg)
        check(f"power config {name} -> ERR_PWRM_BAD_ARG", err_cause(r) == "ERR_PWRM_BAD_ARG", str(r))
    r = pw_set(link, PW_CONFIG, pw_config()[:3])
    check("power CONFIG write with 3 bytes -> ERR_CMD_BAD_LEN", err_cause(r) == "ERR_CMD_BAD_LEN", str(r))
    r = pw(link, PW_STATUS, dir=WRITE)
    check("power STATUS (read only) sent as a write -> ERR_CMD_DIRECTION", err_cause(r) == "ERR_CMD_DIRECTION", str(r))

    st = wait_source(link, SRC_UNKNOWN)
    devkit_check("status: 40 bytes, source UNKNOWN, enabled + strict, no input monitor, no USB-PD on the devkit",
          st is not None and st["source"] == SRC_UNKNOWN and st["flags"] & 0x0B == PW_FLAG_ENABLED | PW_FLAG_STRICT
          and st["pd_type"] == 0xFF, str(st))

    # USB-PD request: stored and read back (the devkit has no sink: nothing is asked)
    pd = lambda mode, mV=0: bytes([mode]) + struct.pack("<I", mV)
    r = pw_get(link, PW_PD)
    check("PD read = default (highest offer)", r is not None and r.ok and r.data == pd(PD_MAX), str(r))
    r = pw_set(link, PW_PD, pd(PD_VOLTAGE, 9000))
    r2 = pw_get(link, PW_PD)
    check("PD write 9 V -> read 9 V", r is not None and r.ok and r2 is not None and r2.data == pd(PD_VOLTAGE, 9000), f"{r} {r2}")
    for name, args in (("mode 3", pd(3)), ("voltage 0", pd(PD_VOLTAGE, 0)), ("voltage 30 V", pd(PD_VOLTAGE, 30000))):
        r = pw_set(link, PW_PD, args)
        check(f"PD write {name} -> ERR_PWRM_BAD_ARG", err_cause(r) == "ERR_PWRM_BAD_ARG", str(r))
    r = pw_set(link, PW_PD, pd(PD_MAX, 5000))
    r2 = pw_get(link, PW_PD)
    check("PD write highest offer drops the voltage", r is not None and r.ok and r2 is not None and r2.data == pd(PD_MAX), f"{r} {r2}")

    # Settings: PSU and battery, checked and read back
    r = pw_set(link, PW_PSU, struct.pack("<II", 0, 500))
    check("PSU 0 mV -> ERR_PWRM_BAD_ARG", err_cause(r) == "ERR_PWRM_BAD_ARG", str(r))
    r = pw_set(link, PW_PSU, struct.pack("<II", 12000, 2000))
    r2 = pw_get(link, PW_PSU)
    check("PSU write 12 V / 2 A -> read the same", r is not None and r.ok and r2 is not None and r2.ok
          and r2.data == struct.pack("<II", 12000, 2000), f"{r} {r2}")
    pw_set(link, PW_PSU, struct.pack("<II", 0, 0))
    r = pw_set(link, PW_BATTERY, bytes([1, 0, 20, 10]))
    check("battery 0 cells -> ERR_PWRM_BAD_ARG", err_cause(r) == "ERR_PWRM_BAD_ARG", str(r))
    r = pw_set(link, PW_BATTERY, bytes([1, 2, 10, 10]))
    check("battery critical >= low -> ERR_PWRM_BAD_ARG", err_cause(r) == "ERR_PWRM_BAD_ARG", str(r))
    r = pw_set(link, PW_BATTERY, bytes([1, 2, 20, 10]))
    r2 = pw_get(link, PW_BATTERY)
    check("BATTERY write LiPo 2S 20 / 10 % -> read the same", r is not None and r.ok and r2 is not None and r2.ok
          and r2.data == bytes([1, 2, 20, 10]), f"{r} {r2}")
    pw_set(link, PW_BATTERY, bytes([0, 0, 0, 0]))

    # Source on a board without source pins (the devkit): what CONFIG says
    for name, src in (("PSU", SRC_PSU), ("BATTERY", SRC_BATTERY), ("USB", SRC_USB), ("UNKNOWN", SRC_UNKNOWN)):
        pw_set(link, PW_CONFIG, pw_config(source=src))
        st = wait_source(link, src)
        devkit_check(f"CONFIG source {name} -> {name}", st is not None and st["source"] == src, str(st))

    # Modes
    r = pw_set(link, PW_CONFIG, pw_config(strict=0, margin=30))
    st = pw_status(link)
    check("soft: flags enabled, not strict", st is not None and st["flags"] & 0x03 == PW_FLAG_ENABLED, str(st))
    pw_set(link, PW_CONFIG, pw_config(enabled=0))
    time.sleep(0.3)
    st = pw_status(link)
    check("off: flags not enabled", st is not None and st["flags"] & PW_FLAG_ENABLED == 0, str(st))
    pw_set(link, PW_CONFIG, pw_config())

    # Response matrix: the board row, then one row per rail, each its target first. The write takes the same rows,
    # matched by target: a row left out keeps its responses.
    row_len = 2 + len(PW_RESPONSES_DEFAULT)

    def matrix() -> list[tuple[int, bytes]] | None:
        r = pw_get(link, PW_MATRIX)
        if r is None or not r.ok or len(r.data) % row_len:
            return None
        return [(struct.unpack_from("<H", r.data, i)[0], bytes(r.data[i + 2:i + row_len])) for i in range(0, len(r.data), row_len)]

    def rows_bytes(rows: list[tuple[int, bytes]]) -> bytes:
        return b"".join(struct.pack("<H", target) + cells for target, cells in rows)

    def set_matrix(rows: list[tuple[int, bytes]]) -> Response | None:
        return pw_set(link, PW_MATRIX, rows_bytes(rows))

    rows = matrix() or []
    check("MATRIX read: the board row first, every row the board defaults",
          len(rows) > 0 and rows[0][0] == PW_TARGET_BOARD and all(c == PW_RESPONSES_DEFAULT for _, c in rows), str(rows))
    if rows:
        target, cells = rows[-1]  # the last rail (the board row on the devkit)
        one = (target, cells[:5] + bytes([1]) + cells[6:])
        r = set_matrix([one])
        check("MATRIX write of one row (OCP critical -> DISABLE): only that row changes",
              r is not None and r.ok and r.data == b"" and matrix() == rows[:-1] + [one], str(r))
        set_matrix(rows)
        check("MATRIX write of the whole matrix restores it", matrix() == rows, str(matrix()))
    whole = rows_bytes(rows)
    r = pw_set(link, PW_MATRIX, whole[:-1])
    check("MATRIX write with the wrong size -> ERR_CMD_BAD_LEN", err_cause(r) == "ERR_CMD_BAD_LEN", str(r))
    r = pw_set(link, PW_MATRIX, struct.pack("<H", PW_TARGET_BOARD) + bytes([4]) + PW_RESPONSES_DEFAULT[1:])
    check("MATRIX write with a response 4 -> ERR_PWRM_BAD_ARG", err_cause(r) == "ERR_PWRM_BAD_ARG", str(r))
    r = pw_set(link, PW_MATRIX, struct.pack("<H", MISSING_ID) + PW_RESPONSES_DEFAULT)
    check("MATRIX write for a target that is no rail -> ERR_PWRM_NOT_RAIL", err_cause(r) == "ERR_PWRM_NOT_RAIL", str(r))
    check("refused MATRIX writes change nothing", matrix() == rows, str(matrix()))

    # The VREG guard passes a budgeted rail that isn't created
    r = dev(link, TPS_RAIL_A, VREG_VOLTAGE, struct.pack("<I", 9000))
    devkit_check("VREG VOLTAGE on rail A (not created) -> ERR_DEV_NOT_FOUND", err_cause(r) == "ERR_DEV_NOT_FOUND", str(r))


def test_restart(link: Link) -> None:
    t = link.mark()
    r = link.request(bytes([KEY_SYS, SYS_RESTART]))
    check("RESTART answered", r is not None and r.ok, str(r))
    check("board boots again", link.wait_log("runIT up", 20.0, since=t))
    check("PING after restart", ping(link) is not None)


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", required=True, help="COM5 or socket://localhost:5555")
    ap.add_argument("--target", choices=["qemu", "board"], default="board", help="board: a devkit or the PCB (told apart by device 0)")
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

        global ONBOARD
        info = dev(link, GPIO_ID, OP_INFO)
        ONBOARD = info is not None and info.ok
        if ONBOARD:
            print("note: device 0 exists (the runIT PCB): the sections that create devices from an empty table are skipped")

        test_sys(link)
        test_router_errors(link)
        test_errors_stream(link)
        test_deadman(link)
        test_stop(link)
        test_i2c(link, a.target)
        test_conn(link)
        test_ble(link, a.target)
        if ONBOARD:
            for section in ("gpio", "power mux", "device create / remove"):
                skip(section, "needs a bare devkit (no onboard devices); board_tests.py covers the PCB")
        else:
            test_gpio(link, a.target)
            test_power_mux(link, a.target)
            test_device(link)
        test_actions(link)
        test_project(link)
        test_log(link)
        test_settings(link)
        test_error_policy(link)
        test_power(link)
        test_restart(link)
        test_actions_after_restart(link)

    failed = [r for r in RESULTS if not r[1]]
    print(f"\n{len(RESULTS) - len(failed)}/{len(RESULTS)} passed" + (f", {len(SKIPPED)} skipped" if SKIPPED else ""))
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
