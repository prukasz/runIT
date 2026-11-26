#!/usr/bin/env python3
"""Tests of the sys_device lifecycle (SYS_DEVICE.MD) on a board over the console session.

    python runit-esp/tools/test_device_lifecycle.py                      # auto-detect single port; RUNIT_PORT=COM10
    python runit-esp/tools/test_device_lifecycle.py -v DeviceSweeps

Uses device_testdev (type 0x004E) as a virtual bus/IO/event listener and device_tca6424a (type 0x0042) as a real
child driver attached to testdev's simulated I2C bus and reset pin to test:
  - CREATE, INFO, OPS (filtering LOCAL ops), REMOVE, system device protection (GPIO 0x00)
  - SUSPEND / RESUME / RESET semantics and error rules
  - Registry lifecycle events (CREATED, REMOVED with incrementing generation, SUSPENDED, RESUMED, RESET)
  - Dependencies (sys_device_use), resource ownership (I2C address + locked IO pin), and rollback on failed CREATE
  - Sweeps on target ID 0xFF (SUSPEND, RESUME, RESET, REMOVE in dependency order while keeping system devices)
"""
import contextlib
import io
import os
import struct
import sys
import time
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import runit_defs as defs  # noqa: E402
import session_client as sc  # noqa: E402

READ, WRITE = sc.READ, sc.WRITE
KEY_POLICY = defs.service_key("POLICY")
OP_POLICY = defs.service_op("POLICY", "POLICY")
IMP_NONE = defs.sym("SYS_DEVICE_IMPORTANCE_NONE")
IMP_LOW = defs.sym("SYS_DEVICE_IMPORTANCE_LOW")
IMP_MEDIUM = defs.sym("SYS_DEVICE_IMPORTANCE_MEDIUM")
IMP_HIGH = defs.sym("SYS_DEVICE_IMPORTANCE_HIGH")
IMP_CRITICAL = defs.sym("SYS_DEVICE_IMPORTANCE_CRITICAL")
RESP_NONE = 0
RESP_SUSPEND = defs.sym("SYS_DEVICE_RESPONSE_SUSPEND")
RESP_RESET = defs.sym("SYS_DEVICE_RESPONSE_RESET")

GPIO_ID = sc.GPIO  # 0x00, SYS_DEVICE_CREATE_SYSTEM
DEV_A, DEV_B, DEV_C = 33, 34, 35  # IDs no board device takes
SWEEP_ALL = defs.sym("SYS_DEVICE_ID_NONE")

GPIO_TYPE = defs.device_type("esp32s3_gpio")
TCA6424A_TYPE = defs.device_type("tca6424a")
TESTDEV_TYPE = defs.device_type("testdev")

# Lifecycle (CONTRACT_SYS_DEVICE)
DEV_CREATE = defs.contract_op("DEVICE", "CREATE")
DEV_RESET = defs.contract_op("DEVICE", "RESET")
DEV_SUSPEND = defs.contract_op("DEVICE", "SUSPEND")
DEV_RESUME = defs.contract_op("DEVICE", "RESUME")
DEV_REMOVE = defs.contract_op("DEVICE", "REMOVE")
DEV_INFO = defs.contract_op("DEVICE", "INFO")
DEV_OPS = defs.contract_op("DEVICE", "OPS")

# IO
IO_LEVEL = defs.contract_op("IO", "LEVEL")
IO_TOGGLE = defs.contract_op("IO", "TOGGLE")
IO_MODE = defs.contract_op("IO", "MODE")
IO_LOCKED = defs.contract_op("IO", "LOCKED")
IO_LOCK = defs.contract_op("IO", "LOCK")
IO_UNLOCK = defs.contract_op("IO", "UNLOCK")

# I2C
I2C_ATTACH = defs.contract_local_op("I2C", 0x00)
I2C_DETACH = defs.contract_local_op("I2C", 0x01)
I2C_XFER = defs.contract_local_op("I2C", 0x02)
I2C_RAW = defs.contract_op("I2C", "RAW")

# PMON / VREG / SUPPLY / UART / OWN
PMON_VOLTAGE = defs.contract_op("POWER_MONITOR", "VOLTAGE")
VREG_VOLTAGE = defs.contract_op("POWER_VREG", "VOLTAGE")
SUPPLY_SOURCE = defs.contract_op("POWER_SUPPLY", "SOURCE")
UART_BAUD = defs.contract_op("UART", "BAUD")
UART_SUBSCRIBE = defs.contract_local_op("UART", 0x05)
UART_SEND_FRAME = defs.contract_local_op("UART", 0x07)
UART_RX_HOOK = defs.contract_local_op("UART", 0x08)

OWN_FAKE_PIN = defs.device_own_op("testdev", "fake_pin")
OWN_FAKE_I2C = defs.device_own_op("testdev", "fake_i2c")
OWN_LISTEN = defs.device_own_op("testdev", "listen")
OWN_HEARD = defs.device_own_op("testdev", "heard")
OWN_VREG = defs.device_own_op("testdev", "vreg")

# Registry events
EV_DEV_SUSPENDED = defs.contract_event("DEVICE", "SUSPENDED")
EV_DEV_RESUMED = defs.contract_event("DEVICE", "RESUMED")
EV_DEV_RESET = defs.contract_event("DEVICE", "RESET")
EV_DEV_CREATED = defs.contract_event("DEVICE", "CREATED")
EV_DEV_REMOVED = defs.contract_event("DEVICE", "REMOVED")

ANY_TAG = 0xFFFFFFFF
ANY_CH = 0xFFFF
PIN_NONE = (defs.sym("SYS_IO_PIN_NONE_DEVICE"), defs.sym("SYS_IO_PIN_NONE_INDEX"))


def quiet():
    return contextlib.redirect_stdout(io.StringIO())


def dev_cmd(dev_id: int, side: int, op: int, args: bytes = b"") -> bytes:
    return bytes([sc.KEY_DEVICE, side, dev_id]) + struct.pack("<H", op) + args


def policy_get_cmd(dev_id: int) -> bytes:
    return bytes([KEY_POLICY, READ, OP_POLICY, dev_id])


def policy_set_cmd(dev_id: int, importance: int, actions=(0, 0, 0, 0)) -> bytes:
    return bytes([KEY_POLICY, WRITE, OP_POLICY]) + struct.pack("<BBIIII", dev_id, importance, *actions)


def testdev_cfg(pins: int = 16, channels: int = 4, uart_mode: int = 1, uart_baud: int = 115200) -> bytes:
    return struct.pack("<HB", TESTDEV_TYPE, 0) + struct.pack("<BBBI", pins, channels, uart_mode, uart_baud)


def tca6424a_cfg(bus: int, addr: int, intr_pin=PIN_NONE, rst_pin=PIN_NONE) -> bytes:
    return struct.pack("<HB", TCA6424A_TYPE, 0) + struct.pack(
        "<BBBBBB", bus, addr, intr_pin[0], intr_pin[1], rst_pin[0], rst_pin[1]
    )


def unpack_ops(body: bytes) -> dict[int, int]:
    """List of [op u16][sides u8] rows -> {op: sides}."""
    assert len(body) % 3 == 0, len(body)
    return {struct.unpack_from("<H", body, i)[0]: body[i + 2] for i in range(0, len(body), 3)}


def unpack_heard(body: bytes):
    count, tag, source, channel, length, data = struct.unpack("<IIHHH16s", body)
    return count, tag, source, channel, length, data[:length]


class Client:
    def __init__(self, port: str):
        self.link = sc.Link(port, 115200)
        with quiet():
            assert self.link.hello(), "the board did not say hello on " + port

    def call(self, cmd: bytes, wait: float = 2.0):
        with quiet():
            return self.link.call(self.link.board, cmd, wait)

    def rd(self, dev_id: int, op: int, args: bytes = b""):
        return self.call(dev_cmd(dev_id, READ, op, args))

    def wr(self, dev_id: int, op: int, args: bytes = b""):
        return self.call(dev_cmd(dev_id, WRITE, op, args))

    def cleanup(self):
        # Ensure GPIO is resumed if a sweep suspended it, and remove test devices child-first
        self.wr(SWEEP_ALL, DEV_RESUME)
        for did in (DEV_C, DEV_B, DEV_A):
            self.wr(did, DEV_REMOVE)


CLI: Client | None = None


def setUpModule():
    global CLI
    port = os.environ.get("RUNIT_PORT")
    if not port:
        from serial.tools import list_ports
        ports = [p.device for p in list_ports.comports()]
        if len(ports) != 1:
            raise unittest.SkipTest(f"set RUNIT_PORT: serial ports found {ports}")
        port = ports[0]
    CLI = Client(port)
    CLI.cleanup()


class LifecycleBase(unittest.TestCase):
    def setUp(self):
        self.c = CLI
        self.c.cleanup()

    def tearDown(self):
        self.c.cleanup()


class DeviceCreateRemoveInfoOps(LifecycleBase):
    """CREATE, INFO, OPS (no LOCAL ops), REMOVE, error cases, and system device protection."""

    def test_create_info_ops_and_remove(self):
        # Empty slot -> INFO and REMOVE fail
        self.assertEqual(self.c.rd(DEV_A, DEV_INFO)[0], 1)
        self.assertEqual(self.c.wr(DEV_A, DEV_REMOVE)[0], 1)

        self.assertEqual(self.c.wr(DEV_A, DEV_CREATE, testdev_cfg())[0], 0)
        st, info = self.c.rd(DEV_A, DEV_INFO)
        self.assertEqual((st, struct.unpack("<HB", info)), (0, (TESTDEV_TYPE, 0)))

        # OPS contains registry ops + driver ops, and excludes all LOCAL ops
        st, raw_ops = self.c.rd(DEV_A, DEV_OPS)
        self.assertEqual(st, 0)
        ops = unpack_ops(raw_ops)
        self.assertEqual(ops.get(DEV_INFO), 1)       # R only
        self.assertEqual(ops.get(DEV_SUSPEND), 2)    # W only
        self.assertEqual(ops.get(IO_LEVEL), 3)       # R | W
        self.assertEqual(ops.get(PMON_VOLTAGE), 1)   # R only
        for local_op in (IO_LOCK, IO_UNLOCK, I2C_ATTACH, I2C_DETACH, I2C_XFER, UART_SUBSCRIBE, UART_SEND_FRAME, UART_RX_HOOK):
            self.assertNotIn(local_op, ops, hex(local_op))

        self.assertEqual(self.c.wr(DEV_A, DEV_REMOVE)[0], 0)
        self.assertEqual(self.c.rd(DEV_A, DEV_INFO)[0], 1)

    def test_create_error_cases(self):
        # Out-of-range IDs
        self.assertEqual(self.c.wr(64, DEV_CREATE, testdev_cfg())[0], 1)
        self.assertEqual(self.c.wr(0xFE, DEV_CREATE, testdev_cfg())[0], 1)
        # Unknown type
        self.assertEqual(self.c.wr(DEV_A, DEV_CREATE, struct.pack("<HB", 0x00FF, 0))[0], 1)
        # Wrong config length (short / long)
        self.assertEqual(self.c.wr(DEV_A, DEV_CREATE, struct.pack("<HB", TESTDEV_TYPE, 0) + b"\x10")[0], 1)
        self.assertEqual(self.c.wr(DEV_A, DEV_CREATE, testdev_cfg() + b"\x00")[0], 1)
        # Driver rejects bad cfg -> slot remains free for valid create
        self.assertEqual(self.c.wr(DEV_A, DEV_CREATE, testdev_cfg(pins=0))[0], 1)
        self.assertEqual(self.c.wr(DEV_A, DEV_CREATE, testdev_cfg())[0], 0)
        # Taken ID
        self.assertEqual(self.c.wr(DEV_A, DEV_CREATE, testdev_cfg())[0], 1)

    def test_system_device_cannot_be_removed(self):
        st, info = self.c.rd(GPIO_ID, DEV_INFO)
        self.assertEqual((st, struct.unpack("<HB", info)), (0, (GPIO_TYPE, 0)))
        self.assertEqual(self.c.wr(GPIO_ID, DEV_REMOVE)[0], 1)  # ERR_DEV_SYSTEM
        self.assertEqual(self.c.rd(GPIO_ID, DEV_INFO)[0], 0)

    def test_unknown_op_and_wrong_direction(self):
        self.assertEqual(self.c.wr(DEV_A, DEV_CREATE, testdev_cfg())[0], 0)
        self.assertEqual(self.c.rd(DEV_A, 0x0199)[0], 1)          # unknown op
        self.assertEqual(self.c.rd(DEV_A, IO_TOGGLE, bytes([0]))[0], 1)   # W-only read
        self.assertEqual(self.c.wr(DEV_A, PMON_VOLTAGE, bytes([0]))[0], 1)  # R-only write
        self.assertEqual(self.c.wr(DEV_A, DEV_INFO)[0], 1)        # R-only registry op


class DeviceSuspendResumeEvents(LifecycleBase):
    """SUSPEND / RESUME / RESET semantics and lifecycle events with slot generation."""

    def test_suspend_resume_rules_and_always_ops(self):
        self.assertEqual(self.c.wr(DEV_A, DEV_CREATE, testdev_cfg())[0], 0)
        # RESUME while not suspended is a no-op (idempotent)
        self.assertEqual(self.c.wr(DEV_A, DEV_RESUME)[0], 0)
        self.assertEqual(struct.unpack("<HB", self.c.rd(DEV_A, DEV_INFO)[1])[1], 0)

        self.assertEqual(self.c.wr(DEV_A, DEV_SUSPEND)[0], 0)
        self.assertEqual(struct.unpack("<HB", self.c.rd(DEV_A, DEV_INFO)[1])[1], 1)
        # Normal write refused, read allowed, ALWAYS writes (FAKE_PIN, UART_BAUD) allowed
        self.assertEqual(self.c.wr(DEV_A, SUPPLY_SOURCE, bytes([1]))[0], 1)
        self.assertEqual(self.c.rd(DEV_A, SUPPLY_SOURCE), (0, bytes([0])))
        self.assertEqual(self.c.wr(DEV_A, OWN_FAKE_PIN, struct.pack("<BBi", 0, 1, 500))[0], 0)
        self.assertEqual(self.c.wr(DEV_A, UART_BAUD, struct.pack("<I", 230400))[0], 0)

        self.assertEqual(self.c.wr(DEV_A, DEV_RESUME)[0], 0)
        self.assertEqual(struct.unpack("<HB", self.c.rd(DEV_A, DEV_INFO)[1])[1], 0)
        self.assertEqual(self.c.wr(DEV_A, SUPPLY_SOURCE, bytes([1]))[0], 0)

    def test_lifecycle_events_and_slot_generation(self):
        # DEV_B listens to every registry event whose source is DEV_A
        self.assertEqual(self.c.wr(DEV_B, DEV_CREATE, testdev_cfg())[0], 0)
        self.assertEqual(self.c.wr(DEV_B, OWN_LISTEN, struct.pack("<IHH", ANY_TAG, DEV_A, ANY_CH))[0], 0)

        # CREATE -> CREATED(gen1)
        self.assertEqual(self.c.wr(DEV_A, DEV_CREATE, testdev_cfg())[0], 0)
        cnt, tag, src, ch, ln, data = unpack_heard(self.c.rd(DEV_B, OWN_HEARD)[1])
        self.assertEqual((cnt, tag, src, ch, ln), (1, EV_DEV_CREATED, DEV_A, 0, 1))
        gen1 = data[0]
        self.assertNotEqual(gen1, 0)

        # SUSPEND -> SUSPENDED
        self.assertEqual(self.c.wr(DEV_A, DEV_SUSPEND)[0], 0)
        cnt, tag, src, _, ln, _ = unpack_heard(self.c.rd(DEV_B, OWN_HEARD)[1])
        self.assertEqual((cnt, tag, src, ln), (2, EV_DEV_SUSPENDED, DEV_A, 0))

        # RESUME -> RESUMED
        self.assertEqual(self.c.wr(DEV_A, DEV_RESUME)[0], 0)
        cnt, tag, src, _, ln, _ = unpack_heard(self.c.rd(DEV_B, OWN_HEARD)[1])
        self.assertEqual((cnt, tag, src, ln), (3, EV_DEV_RESUMED, DEV_A, 0))

        # RESET -> RESET
        self.assertEqual(self.c.wr(DEV_A, DEV_RESET)[0], 0)
        cnt, tag, src, _, ln, _ = unpack_heard(self.c.rd(DEV_B, OWN_HEARD)[1])
        self.assertEqual((cnt, tag, src, ln), (4, EV_DEV_RESET, DEV_A, 0))

        # REMOVE -> REMOVED(gen1)
        self.assertEqual(self.c.wr(DEV_A, DEV_REMOVE)[0], 0)
        cnt, tag, src, _, ln, data = unpack_heard(self.c.rd(DEV_B, OWN_HEARD)[1])
        self.assertEqual((cnt, tag, src, ln, data[0]), (5, EV_DEV_REMOVED, DEV_A, 1, gen1))

        # Recreate -> CREATED(gen2) where gen2 != gen1
        self.assertEqual(self.c.wr(DEV_A, DEV_CREATE, testdev_cfg())[0], 0)
        cnt, tag, _, _, ln, data = unpack_heard(self.c.rd(DEV_B, OWN_HEARD)[1])
        self.assertEqual((cnt, tag, ln), (6, EV_DEV_CREATED, 1))
        self.assertEqual(data[0], 1 if gen1 == 255 else gen1 + 1)


class DeviceDependenciesAndResources(LifecycleBase):
    """Real child driver (TCA6424A) on testdev's simulated I2C bus + RST pin: uses, locks, and rollback."""

    def test_child_locks_pin_and_i2c_addr_and_blocks_parent_remove(self):
        self.assertEqual(self.c.wr(DEV_A, DEV_CREATE, testdev_cfg())[0], 0)
        # Put a simulated chip at 0x22 on testdev's I2C bus (replace default 0x48 slot)
        self.assertEqual(self.c.wr(DEV_A, OWN_FAKE_I2C, bytes([0x48, 0]))[0], 0)
        self.assertEqual(self.c.wr(DEV_A, OWN_FAKE_I2C, bytes([0x22, 1]))[0], 0)

        # Create TCA6424A on DEV_A bus @ 0x22 with rst_pin = (DEV_A, 10)
        self.assertEqual(self.c.wr(DEV_C, DEV_CREATE, tca6424a_cfg(DEV_A, 0x22, rst_pin=(DEV_A, 10)))[0], 0)

        # Pin 10 is locked by DEV_C; external write to pin 10 is refused
        self.assertEqual(struct.unpack("<Q", self.c.rd(DEV_A, IO_LOCKED)[1])[0], 1 << 10)
        self.assertEqual(self.c.wr(DEV_A, IO_LEVEL, bytes([10, 0]))[0], 1)

        # I2C address 0x22 is owned by DEV_C; RAW access is refused
        self.assertEqual(self.c.rd(DEV_A, I2C_RAW, bytes([0x22, 0x00, 1]))[0], 1)

        # Parent DEV_A cannot be removed while child DEV_C uses it (ERR_DEV_IN_USE)
        self.assertEqual(self.c.wr(DEV_A, DEV_REMOVE)[0], 1)

        # Remove child -> pin 10 unlocked, I2C addr detached, parent now removable
        self.assertEqual(self.c.wr(DEV_C, DEV_REMOVE)[0], 0)
        self.assertEqual(struct.unpack("<Q", self.c.rd(DEV_A, IO_LOCKED)[1])[0], 0)
        self.assertEqual(self.c.rd(DEV_A, I2C_RAW, bytes([0x22, 0x00, 1]))[0], 0)
        self.assertEqual(self.c.wr(DEV_A, DEV_REMOVE)[0], 0)

    def test_failed_child_create_rolls_back_resources_and_uses(self):
        self.assertEqual(self.c.wr(DEV_A, DEV_CREATE, testdev_cfg())[0], 0)
        # No chip at 0x23 -> TCA6424A create claims rst_pin (DEV_A, 11), then fails I2C attach
        self.assertEqual(self.c.wr(DEV_C, DEV_CREATE, tca6424a_cfg(DEV_A, 0x23, rst_pin=(DEV_A, 11)))[0], 1)
        # Pin 11 must have been unlocked on rollback, and DEV_A must be removable
        self.assertEqual(struct.unpack("<Q", self.c.rd(DEV_A, IO_LOCKED)[1])[0], 0)
        self.assertEqual(self.c.wr(DEV_A, DEV_REMOVE)[0], 0)


class DeviceSweeps(LifecycleBase):
    """Sweeps on target ID 0xFF: SUSPEND, RESUME, RESET, and REMOVE (keeping system devices)."""

    def test_sweep_suspend_resume_and_remove_dependency_order(self):
        self.assertEqual(self.c.wr(DEV_A, DEV_CREATE, testdev_cfg())[0], 0)
        self.assertEqual(self.c.wr(DEV_A, OWN_FAKE_I2C, bytes([0x48, 0]))[0], 0)
        self.assertEqual(self.c.wr(DEV_A, OWN_FAKE_I2C, bytes([0x22, 1]))[0], 0)
        self.assertEqual(self.c.wr(DEV_C, DEV_CREATE, tca6424a_cfg(DEV_A, 0x22, rst_pin=(DEV_A, 10)))[0], 0)

        # SUSPEND sweep: child suspended before parent (can still touch parent's rst_pin / I2C)
        self.assertEqual(self.c.wr(SWEEP_ALL, DEV_SUSPEND)[0], 0)
        for did in (GPIO_ID, DEV_A, DEV_C):
            self.assertEqual(struct.unpack("<HB", self.c.rd(did, DEV_INFO)[1])[1], 1, f"dev {did} not suspended")

        # RESUME sweep: parent resumed before child
        self.assertEqual(self.c.wr(SWEEP_ALL, DEV_RESUME)[0], 0)
        for did in (GPIO_ID, DEV_A, DEV_C):
            self.assertEqual(struct.unpack("<HB", self.c.rd(did, DEV_INFO)[1])[1], 0, f"dev {did} still suspended")

        # REMOVE sweep: removes child DEV_C before parent DEV_A, keeps system GPIO_ID
        self.assertEqual(self.c.wr(SWEEP_ALL, DEV_REMOVE)[0], 0)
        self.assertEqual(self.c.rd(DEV_C, DEV_INFO)[0], 1)
        self.assertEqual(self.c.rd(DEV_A, DEV_INFO)[0], 1)
        self.assertEqual(self.c.rd(GPIO_ID, DEV_INFO)[0], 0)

    def test_sweep_reset_restores_defaults_and_skips_unsupported(self):
        self.assertEqual(self.c.wr(DEV_A, DEV_CREATE, testdev_cfg())[0], 0)
        self.assertEqual(self.c.wr(DEV_A, VREG_VOLTAGE, struct.pack("<I", 12000))[0], 0)
        # RESET sweep: testdev returns to defaults; devices without RESET (e.g. GPIO) are skipped cleanly
        self.assertEqual(self.c.wr(SWEEP_ALL, DEV_RESET)[0], 0)
        self.assertEqual(struct.unpack("<BII", self.c.rd(DEV_A, OWN_VREG)[1]), (0, 5000, 1000))


class DeviceErrorPolicy(LifecycleBase):
    """sys_device_policy (key 0x21): automated SUSPEND / RESET responses and importance filtering on intentional errors."""

    def test_policy_defaults_set_get_validation_and_recreate_reset(self):
        self.assertEqual(self.c.call(policy_get_cmd(DEV_A))[0], 1)  # ERR_DEV_NOT_FOUND
        self.assertEqual(self.c.wr(DEV_A, DEV_CREATE, testdev_cfg())[0], 0)

        st, body = self.c.call(policy_get_cmd(DEV_A))
        self.assertEqual((st, struct.unpack("<BBIIII", body)), (0, (DEV_A, IMP_LOW, 0, 0, 0, 0)))

        # Set and read back
        self.assertEqual(self.c.call(policy_set_cmd(DEV_A, IMP_MEDIUM, (0, 0, RESP_SUSPEND, RESP_RESET)))[0], 0)
        st, body = self.c.call(policy_get_cmd(DEV_A))
        self.assertEqual((st, struct.unpack("<BBIIII", body)), (0, (DEV_A, IMP_MEDIUM, 0, 0, RESP_SUSPEND, RESP_RESET)))

        # Validation rejects invalid importance and unknown built-in response
        self.assertEqual(self.c.call(policy_set_cmd(DEV_A, 5, (0, 0, 0, 0)))[0], 1)
        self.assertEqual(self.c.call(policy_set_cmd(DEV_A, IMP_MEDIUM, (0, 0, 0xFF000099, 0)))[0], 1)

        # Remove clears policy; recreate restores defaults
        self.assertEqual(self.c.wr(DEV_A, DEV_REMOVE)[0], 0)
        self.assertEqual(self.c.call(policy_get_cmd(DEV_A))[0], 1)
        self.assertEqual(self.c.wr(DEV_A, DEV_CREATE, testdev_cfg())[0], 0)
        self.assertEqual(struct.unpack("<BBIIII", self.c.call(policy_get_cmd(DEV_A))[1]), (DEV_A, IMP_LOW, 0, 0, 0, 0))

    def test_intentional_high_error_triggers_auto_suspend(self):
        self.assertEqual(self.c.wr(DEV_A, DEV_CREATE, testdev_cfg())[0], 0)
        self.assertEqual(self.c.wr(DEV_A, SUPPLY_SOURCE, bytes([1]))[0], 0)
        # MEDIUM importance admits HIGH (ERR_RANGE) -> auto-suspend
        self.assertEqual(self.c.call(policy_set_cmd(DEV_A, IMP_MEDIUM, (0, 0, RESP_SUSPEND, 0)))[0], 0)

        self.assertEqual(self.c.wr(DEV_A, VREG_VOLTAGE, struct.pack("<I", 999))[0], 1)  # ERR_RANGE (HIGH)
        time.sleep(0.06)
        self.assertEqual(struct.unpack("<HB", self.c.rd(DEV_A, DEV_INFO)[1])[1], 1)
        self.assertEqual(self.c.rd(DEV_A, SUPPLY_SOURCE), (0, bytes([0])))  # safe state applied

    def test_intentional_high_error_triggers_auto_reset(self):
        self.assertEqual(self.c.wr(DEV_A, DEV_CREATE, testdev_cfg())[0], 0)
        self.assertEqual(self.c.wr(DEV_A, VREG_VOLTAGE, struct.pack("<I", 12000))[0], 0)
        self.assertEqual(self.c.wr(DEV_A, SUPPLY_SOURCE, bytes([2]))[0], 0)
        # MEDIUM importance admits HIGH (ERR_I2C_NO_ACK) -> auto-reset
        self.assertEqual(self.c.call(policy_set_cmd(DEV_A, IMP_MEDIUM, (0, 0, RESP_RESET, 0)))[0], 0)

        self.assertEqual(self.c.rd(DEV_A, I2C_RAW, bytes([0x21, 0x00, 1]))[0], 1)  # ERR_I2C_NO_ACK (HIGH)
        time.sleep(0.06)
        self.assertEqual(struct.unpack("<HB", self.c.rd(DEV_A, DEV_INFO)[1])[1], 0)  # stays active
        self.assertEqual(struct.unpack("<BII", self.c.rd(DEV_A, OWN_VREG)[1]), (0, 5000, 1000))
        self.assertEqual(self.c.rd(DEV_A, SUPPLY_SOURCE), (0, bytes([0])))

    def test_importance_filtering_blocks_and_admits_by_severity(self):
        self.assertEqual(self.c.wr(DEV_A, DEV_CREATE, testdev_cfg())[0], 0)

        # LOW importance (default) and NONE do NOT admit HIGH errors
        for imp in (IMP_LOW, IMP_NONE):
            self.assertEqual(self.c.call(policy_set_cmd(DEV_A, imp, (0, 0, RESP_SUSPEND, 0)))[0], 0)
            self.assertEqual(self.c.wr(DEV_A, VREG_VOLTAGE, struct.pack("<I", 999))[0], 1)
            time.sleep(0.05)
            self.assertEqual(struct.unpack("<HB", self.c.rd(DEV_A, DEV_INFO)[1])[1], 0, f"imp {imp} should ignore HIGH")

        # MEDIUM error (ERR_DEV_IN_USE): ignored at IMP_MEDIUM, admitted at IMP_HIGH
        self.assertEqual(self.c.wr(DEV_A, OWN_FAKE_I2C, bytes([0x48, 0]))[0], 0)
        self.assertEqual(self.c.wr(DEV_A, OWN_FAKE_I2C, bytes([0x22, 1]))[0], 0)
        self.assertEqual(self.c.wr(DEV_C, DEV_CREATE, tca6424a_cfg(DEV_A, 0x22))[0], 0)

        self.assertEqual(self.c.call(policy_set_cmd(DEV_A, IMP_MEDIUM, (0, RESP_SUSPEND, 0, 0)))[0], 0)
        self.assertEqual(self.c.wr(DEV_A, DEV_REMOVE)[0], 1)  # ERR_DEV_IN_USE (MEDIUM)
        time.sleep(0.05)
        self.assertEqual(struct.unpack("<HB", self.c.rd(DEV_A, DEV_INFO)[1])[1], 0)

        self.assertEqual(self.c.call(policy_set_cmd(DEV_A, IMP_HIGH, (0, RESP_SUSPEND, 0, 0)))[0], 0)
        self.assertEqual(self.c.wr(DEV_A, DEV_REMOVE)[0], 1)  # ERR_DEV_IN_USE (MEDIUM) -> admitted!
        time.sleep(0.06)
        self.assertEqual(struct.unpack("<HB", self.c.rd(DEV_A, DEV_INFO)[1])[1], 1)


if __name__ == "__main__":
    unittest.main(verbosity=2)
