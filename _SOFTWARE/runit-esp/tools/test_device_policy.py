#!/usr/bin/env python3
"""Focused hardware tests of sys_device_policy (key 0x21) using device_testdev and device_tca6424a.

    python runit-esp/tools/test_device_policy.py  # auto-detect single port; RUNIT_PORT=COM10

Tests:
  - Policy CRUD, defaults, validation, cleanup on REMOVE, and reset on re-CREATE
  - Auto-SUSPEND and auto-RESET built-in responses on intentional HIGH/MEDIUM errors
  - Importance admission filtering (NONE, LOW, MEDIUM, HIGH) and action=0 no-op
  - Multi-device error chain propagation (TCA6424A child -> testdev I2C bus parent):
      * child IMPORTANCE_NONE stops the chain walk (parent does NOT react)
      * child IMPORTANCE_LOW passes walk down to parent (parent reacts)
      * both child and parent configured -> both react from one error chain
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
DEV_PARENT, DEV_CHILD = 20, 21
TESTDEV_TYPE = defs.device_type("testdev")
TCA6424A_TYPE = defs.device_type("tca6424a")

IMP_NONE = defs.sym("SYS_DEVICE_IMPORTANCE_NONE")
IMP_LOW = defs.sym("SYS_DEVICE_IMPORTANCE_LOW")
IMP_MEDIUM = defs.sym("SYS_DEVICE_IMPORTANCE_MEDIUM")
IMP_HIGH = defs.sym("SYS_DEVICE_IMPORTANCE_HIGH")
IMP_CRITICAL = defs.sym("SYS_DEVICE_IMPORTANCE_CRITICAL")
RESP_NONE = 0
RESP_SUSPEND = defs.sym("SYS_DEVICE_RESPONSE_SUSPEND")
RESP_RESET = defs.sym("SYS_DEVICE_RESPONSE_RESET")

DEV_CREATE = defs.contract_op("DEVICE", "CREATE")
DEV_RESUME = defs.contract_op("DEVICE", "RESUME")
DEV_REMOVE = defs.contract_op("DEVICE", "REMOVE")
DEV_INFO = defs.contract_op("DEVICE", "INFO")
IO_MODE = defs.contract_op("IO", "MODE")
I2C_RAW = defs.contract_op("I2C", "RAW")
VREG_VOLTAGE = defs.contract_op("POWER_VREG", "VOLTAGE")
SUPPLY_SOURCE = defs.contract_op("POWER_SUPPLY", "SOURCE")
OWN_FAKE_I2C = defs.device_own_op("testdev", "fake_i2c")
OWN_VREG = defs.device_own_op("testdev", "vreg")
PIN_NONE = (defs.sym("SYS_IO_PIN_NONE_DEVICE"), defs.sym("SYS_IO_PIN_NONE_INDEX"))


def quiet():
    return contextlib.redirect_stdout(io.StringIO())


def dev_cmd(dev_id: int, side: int, op: int, args: bytes = b"") -> bytes:
    return bytes([sc.KEY_DEVICE, side, dev_id]) + struct.pack("<H", op) + args


def policy_get(dev_id: int) -> bytes:
    return bytes([KEY_POLICY, READ, OP_POLICY, dev_id])


def policy_set(dev_id: int, importance: int, actions=(0, 0, 0, 0)) -> bytes:
    return bytes([KEY_POLICY, WRITE, OP_POLICY]) + struct.pack("<BBIIII", dev_id, importance, *actions)


def testdev_cfg(pins: int = 16, channels: int = 4, uart_mode: int = 1, uart_baud: int = 115200) -> bytes:
    return struct.pack("<HB", TESTDEV_TYPE, 0) + struct.pack("<BBBI", pins, channels, uart_mode, uart_baud)


def tca6424a_cfg(bus: int, addr: int) -> bytes:
    return struct.pack("<HB", TCA6424A_TYPE, 0) + struct.pack("<BBBBBB", bus, addr, *PIN_NONE, *PIN_NONE)


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

    def suspended(self, dev_id: int) -> int:
        return struct.unpack("<HB", self.rd(dev_id, DEV_INFO)[1])[1]

    def cleanup(self):
        for did in (DEV_CHILD, DEV_PARENT):
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


class DevicePolicyTests(unittest.TestCase):
    def setUp(self):
        self.c = CLI
        self.c.cleanup()
        self.assertEqual(self.c.wr(DEV_PARENT, DEV_CREATE, testdev_cfg())[0], 0)

    def tearDown(self):
        self.c.cleanup()

    def test_crud_validation_and_recreate(self):
        self.assertEqual(self.c.call(policy_get(DEV_CHILD))[0], 1)
        st, body = self.c.call(policy_get(DEV_PARENT))
        self.assertEqual((st, struct.unpack("<BBIIII", body)), (0, (DEV_PARENT, IMP_LOW, 0, 0, 0, 0)))

        self.assertEqual(self.c.call(policy_set(DEV_PARENT, IMP_HIGH, (0, RESP_RESET, RESP_SUSPEND, 0)))[0], 0)
        self.assertEqual(
            struct.unpack("<BBIIII", self.c.call(policy_get(DEV_PARENT))[1]),
            (DEV_PARENT, IMP_HIGH, 0, RESP_RESET, RESP_SUSPEND, 0),
        )
        # Invalid importance / unknown built-in response
        self.assertEqual(self.c.call(policy_set(DEV_PARENT, 5, (0, 0, 0, 0)))[0], 1)
        self.assertEqual(self.c.call(policy_set(DEV_PARENT, IMP_MEDIUM, (0, 0, 0xFF000042, 0)))[0], 1)

        # Remove + recreate resets policy to defaults
        self.assertEqual(self.c.wr(DEV_PARENT, DEV_REMOVE)[0], 0)
        self.assertEqual(self.c.call(policy_get(DEV_PARENT))[0], 1)
        self.assertEqual(self.c.wr(DEV_PARENT, DEV_CREATE, testdev_cfg())[0], 0)
        self.assertEqual(struct.unpack("<BBIIII", self.c.call(policy_get(DEV_PARENT))[1]), (DEV_PARENT, IMP_LOW, 0, 0, 0, 0))

    def test_auto_suspend_auto_reset_and_noop_action_zero(self):
        # action=0 admitted -> no-op (not even queued)
        self.assertEqual(self.c.call(policy_set(DEV_PARENT, IMP_HIGH, (0, 0, 0, 0)))[0], 0)
        self.assertEqual(self.c.wr(DEV_PARENT, VREG_VOLTAGE, struct.pack("<I", 999))[0], 1)
        time.sleep(0.04)
        self.assertEqual(self.c.suspended(DEV_PARENT), 0)

        # HIGH -> RESET restores defaults without suspending
        self.assertEqual(self.c.wr(DEV_PARENT, VREG_VOLTAGE, struct.pack("<I", 12000))[0], 0)
        self.assertEqual(self.c.wr(DEV_PARENT, SUPPLY_SOURCE, bytes([2]))[0], 0)
        self.assertEqual(self.c.call(policy_set(DEV_PARENT, IMP_MEDIUM, (0, 0, RESP_RESET, 0)))[0], 0)
        self.assertEqual(self.c.rd(DEV_PARENT, I2C_RAW, bytes([0x21, 0x00, 1]))[0], 1)  # ERR_I2C_NO_ACK (HIGH)
        time.sleep(0.05)
        self.assertEqual(self.c.suspended(DEV_PARENT), 0)
        self.assertEqual(struct.unpack("<BII", self.c.rd(DEV_PARENT, OWN_VREG)[1]), (0, 5000, 1000))
        self.assertEqual(self.c.rd(DEV_PARENT, SUPPLY_SOURCE)[1], bytes([0]))

        # HIGH -> SUSPEND enters safe state
        self.assertEqual(self.c.wr(DEV_PARENT, SUPPLY_SOURCE, bytes([1]))[0], 0)
        self.assertEqual(self.c.call(policy_set(DEV_PARENT, IMP_MEDIUM, (0, 0, RESP_SUSPEND, 0)))[0], 0)
        self.assertEqual(self.c.wr(DEV_PARENT, VREG_VOLTAGE, struct.pack("<I", 999))[0], 1)  # ERR_RANGE (HIGH)
        time.sleep(0.05)
        self.assertEqual(self.c.suspended(DEV_PARENT), 1)
        self.assertEqual(self.c.rd(DEV_PARENT, SUPPLY_SOURCE)[1], bytes([0]))

    def test_multi_device_chain_propagation_and_importance_none_barrier(self):
        # Put simulated chip 0x22 on DEV_PARENT, attach TCA6424A (DEV_CHILD), then remove chip 0x22 so child ops fail
        # with chain: ERR_DEV_ID(DEV_CHILD) -> ERR_DEV_ID(DEV_PARENT) -> ERR_I2C_NO_ACK (HIGH)
        self.assertEqual(self.c.wr(DEV_PARENT, OWN_FAKE_I2C, bytes([0x48, 0]))[0], 0)
        self.assertEqual(self.c.wr(DEV_PARENT, OWN_FAKE_I2C, bytes([0x22, 1]))[0], 0)
        self.assertEqual(self.c.wr(DEV_CHILD, DEV_CREATE, tca6424a_cfg(DEV_PARENT, 0x22))[0], 0)
        self.assertEqual(self.c.wr(DEV_PARENT, OWN_FAKE_I2C, bytes([0x22, 0]))[0], 0)

        # Parent wants SUSPEND on HIGH; child has IMPORTANCE_NONE -> stops walk, parent stays active!
        self.assertEqual(self.c.call(policy_set(DEV_PARENT, IMP_MEDIUM, (0, 0, RESP_SUSPEND, 0)))[0], 0)
        self.assertEqual(self.c.call(policy_set(DEV_CHILD, IMP_NONE, (0, 0, RESP_SUSPEND, 0)))[0], 0)
        self.assertEqual(self.c.wr(DEV_CHILD, IO_MODE, bytes([0, 3]))[0], 1)
        time.sleep(0.05)
        self.assertEqual((self.c.suspended(DEV_CHILD), self.c.suspended(DEV_PARENT)), (0, 0))

        # Child at IMPORTANCE_LOW passes walk down to parent -> parent suspended, child stays active!
        self.assertEqual(self.c.call(policy_set(DEV_CHILD, IMP_LOW, (0, 0, RESP_SUSPEND, 0)))[0], 0)
        self.assertEqual(self.c.wr(DEV_CHILD, IO_MODE, bytes([0, 3]))[0], 1)
        time.sleep(0.05)
        self.assertEqual((self.c.suspended(DEV_CHILD), self.c.suspended(DEV_PARENT)), (0, 1))

        # Resume parent, set BOTH to MEDIUM + SUSPEND -> single error chain suspends BOTH!
        self.assertEqual(self.c.wr(DEV_PARENT, DEV_RESUME)[0], 0)
        self.assertEqual(self.c.call(policy_set(DEV_CHILD, IMP_MEDIUM, (0, 0, RESP_SUSPEND, 0)))[0], 0)
        self.assertEqual(self.c.wr(DEV_CHILD, IO_MODE, bytes([0, 3]))[0], 1)
        time.sleep(0.05)
        self.assertEqual((self.c.suspended(DEV_CHILD), self.c.suspended(DEV_PARENT)), (1, 1))


if __name__ == "__main__":
    unittest.main(verbosity=2)
