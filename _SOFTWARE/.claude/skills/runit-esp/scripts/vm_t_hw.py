"""VM suite, part 4: the device blocks and the system events on the real PCB. Run through vm_suite.py.

Hardware touched: GPIO pin PIN (default 12, push-pull output, nothing connected is expected) of device 0, and reads of the
INA3221 (device 12). Nothing is switched on: the blocks of motors, servos and regulators are only loaded (to check their
shape) and the program is reset without ever starting. Change the pin with the VM_SUITE_PIN environment variable.
"""

from __future__ import annotations

import os
import struct
import time

from vm_lib import *  # noqa: F401,F403
from vm_t_blocks import loaded, NAN  # noqa: F401
from vm_t_core import F_IN, OPF, expr_state  # noqa: F401

KEY_DEVICE = 0x05
GPIO_ID, INA_ID = 0, 12
PIN = int(os.environ.get("VM_SUITE_PIN", "12"))
IO_SET_LEVEL, IO_TOGGLE, IO_GET_LEVEL, IO_PWM_DUTY, IO_PWM_FREQ, IO_SET_MODE, IO_RESET, IO_CONFIGURE_INTR, IO_GET_VOLTAGE, IO_SET_VOLTAGE = (
    0x0100, 0x0101, 0x0102, 0x0103, 0x0104, 0x0105, 0x0106, 0x0107, 0x0108, 0x0109)
EV_RISING, EV_FALLING, EV_ANY_IO = 0x0100, 0x0101, 0x01FF


def dev_call(vm: Vm, dev_id: int, op: int, payload: bytes = b"") -> Response | None:
    return vm.link.request(bytes([KEY_DEVICE]) + struct.pack("<HH", dev_id, op), payload)


def dev_state(device: int, op: int, idle_op: int = 0, k=(), idle_k=()) -> bytes:
    """vm_block_device_data_t (56 bytes): device, op, idle op, flags, pad, k[4], idle_k[4], last[4]."""
    kk = list(k) + [0] * (4 - len(k))
    ik = list(idle_k) + [0] * (4 - len(idle_k))
    return struct.pack("<HHHBB4i4i4i", device, op, idle_op, 0, 0, *kk, *ik, 0, 0, 0, 0)


def fbits(x: float) -> int:
    return struct.unpack("<i", f32(x))[0]


def level(vm: Vm, pin: int = PIN) -> int | None:
    r = dev_call(vm, GPIO_ID, IO_GET_LEVEL, bytes([pin]))
    return r.data[0] if ok(r) and r.data else None


def pin_free(vm: Vm) -> bool:
    """The pin is not used by anything else: release it, then make it an output."""
    dev_call(vm, GPIO_ID, IO_CONFIGURE_INTR, bytes([PIN]) + struct.pack("<BBHHHH", 0, 0, 0, 0, 0, 0))
    dev_call(vm, GPIO_ID, IO_RESET, bytes([PIN]))
    r = dev_call(vm, GPIO_ID, IO_SET_MODE, bytes([PIN, 3]))
    if not ok(r):
        check(f"GPIO {PIN} can be made an output (else the pin tests are skipped)", False, str(r))
        return False
    dev_call(vm, GPIO_ID, IO_RESET, bytes([PIN]))
    return True


def pin_release(vm: Vm) -> None:
    dev_call(vm, GPIO_ID, IO_CONFIGURE_INTR, bytes([PIN]) + struct.pack("<BBHHHH", 0, 0, 0, 0, 0, 0))
    dev_call(vm, GPIO_ID, IO_RESET, bytes([PIN]))


# =====================================================================================================================
# shape of every device block (nothing is started)
# =====================================================================================================================

# (block, device, [(op, inputs, outputs), ...] that must load)
SHAPES = [
    ("IO", GPIO_ID, [(IO_SET_LEVEL, 2, 0), (IO_TOGGLE, 1, 0), (IO_GET_LEVEL, 1, 1), (IO_PWM_DUTY, 2, 0), (IO_PWM_FREQ, 2, 0), (IO_SET_MODE, 2, 0), (IO_RESET, 1, 0),
                     (IO_GET_VOLTAGE, 1, 1), (IO_SET_VOLTAGE, 2, 0)]),
    ("HBRIDGE", 5, [(0x0900, 2, 0), (0x0901, 1, 0), (0x0902, 1, 0), (0x0903, 1, 1)]),
    ("POWER_MONITOR", INA_ID, [(0x0400, 1, 1), (0x0401, 1, 1), (0x0403, 1, 1), (0x0402, 3, 0)]),
    ("POWER_VREG", 10, [(0x0500, 1, 0), (0x0501, 1, 0), (0x0502, 1, 0)]),
    ("POWER_SUPPLY", 7, [(0x0800, 1, 0)]),
    ("POWER_USB_PD", 13, [(0x0700, 2, 0), (0x0702, 0, 2)]),
    ("SERVO", 20, [(0xFF02, 1, 0), (0xFF00, 0, 0), (0xFF04, 0, 0), (0xFF01, 1, 0)]),
    ("DRV8962", 5, [(0xFF00, 2, 0), (0xFF01, 1, 0), (0xFF02, 1, 2)]),
    ("TPS55289", 10, [(0xFF01, 4, 0), (0xFF02, 0, 4)]),
]


def one_block(vm: Vm, btype: str, state: bytes, nin: int, nout: int, ens: int = 0) -> Response | None:
    p = Prog()
    ins = [p.f(1.0) for _ in range(nin)]
    outs = [p.i32() for _ in range(nout)]
    gates = [p.b(True) for _ in range(ens)]
    p.block(btype, ins=ins, outs=outs, ens=gates, state=state)
    return vm.load(p)


@test("device blocks/shape of every contract block")
def t_dev_shape(vm: Vm) -> None:
    for btype, dev, ops in SHAPES:
        for n, (op, nin, nout) in enumerate(ops):
            r = one_block(vm, btype, dev_state(dev, op), nin, nout)
            check(f"{btype} op {op:#06x} with {nin} input(s) and {nout} output(s) loads", r is None, str(r))
            if n:  # the limits are tried on the first operation of each block only: the same code checks them all
                continue
            if nin:
                r = one_block(vm, btype, dev_state(dev, op), nin + 1, nout) if nin < 4 else one_block(vm, btype, dev_state(dev, op), 5, nout)
                check(f"{btype} op {op:#06x}: one input more than the operation has -> ERR_VM_BLK_BAD_SHAPE", cause(r) == "ERR_VM_BLK_BAD_SHAPE", str(r))
            r = one_block(vm, btype, dev_state(dev, op), nin, nout + 1)
            check(f"{btype} op {op:#06x}: one output more than the operation answers -> ERR_VM_BLK_BAD_SHAPE", cause(r) == "ERR_VM_BLK_BAD_SHAPE", str(r))
        r = one_block(vm, btype, dev_state(dev, 0x0DEF), 0, 0)
        check(f"{btype}: an operation that is not in its table -> ERR_VM_BLK_BAD_SHAPE", cause(r) == "ERR_VM_BLK_BAD_SHAPE", str(r))
        r = one_block(vm, btype, dev_state(dev, ops[0][0]), ops[0][1], ops[0][2], ens=2)
        check(f"{btype}: enables are allowed", r is None, str(r))
        r = one_block(vm, btype, dev_state(0xFFFF, ops[0][0]), ops[0][1], ops[0][2])
        check(f"{btype}: device 0xFFFF (no device) -> ERR_VM_BLK_BAD_SHAPE", cause(r) == "ERR_VM_BLK_BAD_SHAPE", str(r))
        r = one_block(vm, btype, dev_state(dev, ops[0][0])[:50], ops[0][1], ops[0][2])
        check(f"{btype}: a state shorter than 56 bytes -> ERR_VM_BLK_BAD_SHAPE", cause(r) == "ERR_VM_BLK_BAD_SHAPE", str(r))

    # the idle operation
    r = one_block(vm, "IO", dev_state(GPIO_ID, IO_SET_LEVEL, idle_op=IO_SET_LEVEL, idle_k=(1,)), 2, 0)
    check("IO SET_LEVEL with an idle SET_LEVEL loads", r is None, str(r))
    r = one_block(vm, "IO", dev_state(GPIO_ID, IO_SET_LEVEL, idle_op=IO_RESET), 2, 0)
    check("... or an idle RESET (an action)", r is None, str(r))
    r = one_block(vm, "IO", dev_state(GPIO_ID, IO_SET_LEVEL, idle_op=IO_GET_LEVEL), 2, 0)
    check("an idle operation that is a read -> ERR_VM_BLK_BAD_SHAPE", cause(r) == "ERR_VM_BLK_BAD_SHAPE", str(r))
    r = one_block(vm, "IO", dev_state(GPIO_ID, IO_GET_LEVEL, idle_op=IO_SET_LEVEL), 1, 1)
    check("a block that reads cannot have an idle operation -> ERR_VM_BLK_BAD_SHAPE", cause(r) == "ERR_VM_BLK_BAD_SHAPE", str(r))
    r = one_block(vm, "IO", dev_state(GPIO_ID, IO_SET_LEVEL, idle_op=0x0DEF), 2, 0)
    check("an idle operation the contract does not have -> ERR_VM_BLK_BAD_SHAPE", cause(r) == "ERR_VM_BLK_BAD_SHAPE", str(r))
    r = one_block(vm, "HBRIDGE", dev_state(5, 0x0900, idle_op=0x0902), 2, 0)
    check("HBRIDGE SET_DRIVE with an idle COAST loads", r is None, str(r))
    vm.reset()


# =====================================================================================================================
# IO block: a pin, driven and read by a program
# =====================================================================================================================


@test("device blocks/IO block drives and reads a pin", hw=True)
def t_io_block(vm: Vm) -> None:
    if not pin_free(vm):
        return
    p = Prog()
    lvl, en, tg = p.u8(0), p.b(True), p.b(False)
    rd, eno_l, eno_r, eno_t = p.u8(), p.b(), p.b(), p.b()
    p.block("IO", state=dev_state(GPIO_ID, IO_SET_MODE, k=(PIN, 3)))  # push-pull output, sent on the first pass
    p.block("IO", ins=[None, lvl], ens=[en], eno=eno_l, state=dev_state(GPIO_ID, IO_SET_LEVEL, idle_op=IO_SET_LEVEL, k=(PIN, 0), idle_k=(1,)))
    p.block("IO", ins=[None], outs=[rd], eno=eno_r, state=dev_state(GPIO_ID, IO_GET_LEVEL, k=(PIN,)))
    p.block("IO", ens=[tg], eno=eno_t, state=dev_state(GPIO_ID, IO_TOGGLE, k=(PIN,)))
    if not loaded(vm, p, X_NORMAL, "the IO program (SET_MODE, SET_LEVEL with an idle level, GET_LEVEL, TOGGLE)"):
        return
    time.sleep(0.5)
    check("SET_MODE and SET_LEVEL (0) from blocks reach the pin", level(vm) == 0, str(level(vm)))
    vm.write(lvl, 1)
    time.sleep(0.4)
    check("level 1 from the input pin of the block drives the pin high", level(vm) == 1, str(level(vm)))
    check("the GET_LEVEL block reads it back into its output", vm.read(rd) == 1)
    vm.write(lvl, 0)
    time.sleep(0.4)
    check("level 0 drives it low again", level(vm) == 0 and vm.read(rd) == 0, f"{level(vm)} {vm.read(rd)}")
    vm.write(en, False)
    time.sleep(0.4)
    check("the enable closes: the idle operation runs once (level 1, from idle_k)", level(vm) == 1, str(level(vm)))
    dev_call(vm, GPIO_ID, IO_SET_LEVEL, bytes([PIN, 0]))
    time.sleep(0.4)
    check("... only once: a disabled block leaves the pin alone afterwards", level(vm) == 0, str(level(vm)))
    vm.write(en, True)
    time.sleep(0.4)
    check("the enable opens: the block sends its level again (0)", level(vm) == 0 and vm.read(eno_l) == 1, f"{level(vm)} {vm.read(eno_l)}")
    # TOGGLE: once per rising enable
    vm.write(tg, True)
    time.sleep(0.4)
    a = level(vm)
    time.sleep(0.4)
    check("TOGGLE flips the pin once when its enable rises (and not again while it stays on)", a == 1 and level(vm) == 1, f"{a} {level(vm)}")
    vm.write(tg, False)
    time.sleep(0.2)
    vm.write(tg, True)
    time.sleep(0.4)
    check("... and again on the next rising edge", level(vm) == 0, str(level(vm)))
    vm.reset()
    pin_release(vm)

    # a setting is sent when its argument changes, not every pass: another writer is not overwritten
    if not pin_free(vm):
        return
    p = Prog()
    lvl = p.u8(1)
    p.block("IO", state=dev_state(GPIO_ID, IO_SET_MODE, k=(PIN, 3)))
    p.block("IO", ins=[None, lvl], state=dev_state(GPIO_ID, IO_SET_LEVEL, k=(PIN, 0)))
    loaded(vm, p, X_NORMAL, "a program with a setting")
    time.sleep(0.5)
    check("the block sets the pin high", level(vm) == 1)
    dev_call(vm, GPIO_ID, IO_SET_LEVEL, bytes([PIN, 0]))
    time.sleep(0.5)
    check("a setting is written on change only: someone else's write is not overwritten", level(vm) == 0, str(level(vm)))
    vm.write(lvl, 1)
    vm.write(lvl, 0)
    vm.write(lvl, 1)
    time.sleep(0.4)
    check("... until the value changes", level(vm) == 1, str(level(vm)))
    vm.reset()
    pin_release(vm)


@test("device blocks/a failing device call", hw=True)
def t_dev_fail(vm: Vm) -> None:
    p = Prog()
    out, e = p.i32(77), p.b()
    p.block("POWER_MONITOR", outs=[out], eno=e, state=dev_state(99, 0x0400, k=(1,)))
    if not loaded(vm, p, X_SCAN, "a POWER_MONITOR block on a device that does not exist"):
        return
    t = vm.link.mark()
    g = vm.step([out, e])
    check("the block loads (the device is looked up when it runs): ENO false, the output is left alone", vm.vals(g, e) == 0 and vm.vals(g, out) == 77, f"{vm.vals(g, e)} {vm.vals(g, out)}")
    names = wait_err(vm.link, t, "ERR_VM_BLOCK_FAILED", 2.0)
    check("... and the failure is reported", names is not None, str(err_names(vm.link, t)))
    vm.exec(X_NORMAL)
    t = vm.link.mark()
    time.sleep(1.0)
    n = sum(1 for x in err_names(vm.link, t) if x == "ERR_VM_BLOCK_FAILED")
    check("a block that fails on every pass does not flood the error stream (the stream is rate limited: at most ~60 reports a second)", n <= 60, f"{n} reports in 1 s")
    vm.reset()

    # a bad channel of a real device
    p = Prog()
    out, e = p.i32(77), p.b()
    p.block("POWER_MONITOR", outs=[out], eno=e, state=dev_state(INA_ID, 0x0400, k=(9,)))
    loaded(vm, p, X_SCAN, "a POWER_MONITOR block on channel 9")
    t = vm.link.mark()
    g = vm.step([out, e])
    check("a channel the chip does not have: ENO false", vm.vals(g, e) == 0 and vm.vals(g, out) == 77, f"{vm.vals(g, e)} {vm.vals(g, out)}")
    check("... ERR_POWER_BAD_CHANNEL says why", wait_err(vm.link, t, "ERR_POWER_BAD_CHANNEL", 2.0) is not None, str(err_names(vm.link, t)))
    vm.reset()


@test("device blocks/POWER_MONITOR reads the board", hw=True)
def t_pmon(vm: Vm) -> None:
    p = Prog()
    ch = p.u8(1)
    mv, ma, mw = p.i32(), p.i32(), p.i32()
    mv0, mvx = p.i32(), p.i32()
    e1, e2, e3 = p.b(), p.b(), p.b()
    p.block("POWER_MONITOR", ins=[None], outs=[mv], eno=e1, state=dev_state(INA_ID, 0x0400, k=(1,)))      # the channel from the state: the input
    p.block("POWER_MONITOR", ins=[None], outs=[ma], eno=e2, state=dev_state(INA_ID, 0x0401, k=(1,)))
    p.block("POWER_MONITOR", ins=[None], outs=[mw], eno=e3, state=dev_state(INA_ID, 0x0403, k=(1,)))
    p.block("POWER_MONITOR", ins=[ch], outs=[mv0], state=dev_state(INA_ID, 0x0400, k=(0,)))             # the channel from a pin (1) overrides k
    p.block("POWER_MONITOR", ins=[None], outs=[mvx], state=dev_state(INA_ID, 0x0400, k=(0,)))           # rail B
    if not loaded(vm, p, X_SCAN, "five POWER_MONITOR blocks"):
        return
    g = vm.step([mv, ma, mw, mv0, mvx, e1, e2, e3])
    v = vm.vals(g, mv)
    check("the board input voltage (channel 1) is about 5 V", v is not None and 4000 <= v <= 6000, f"{v} mV")
    check("... ENO true on the three reads", (vm.vals(g, e1), vm.vals(g, e2), vm.vals(g, e3)) == (1, 1, 1))
    check("the current and the power are numbers of the right sign and size (mA / mW)", abs(vm.vals(g, ma)) < 6000 and abs(vm.vals(g, mw)) < 30000, f"{vm.vals(g, ma)} mA {vm.vals(g, mw)} mW")
    check("a channel from a pin overrides the state's (pin 1 -> the input again)", vm.vals(g, mv0) is not None and abs(vm.vals(g, mv0) - v) < 400, f"{vm.vals(g, mv0)} vs {v}")
    check("channel 0 (rail B, off) reads less than the input", vm.vals(g, mvx) is not None and vm.vals(g, mvx) < v - 1000, f"{vm.vals(g, mvx)}")
    vm.write(ch, 2)
    g = vm.step([mv0])
    check("the pin changes to channel 2 (rail A): the block reads that channel", vm.vals(g, mv0) is not None and vm.vals(g, mv0) < v - 1000, f"{vm.vals(g, mv0)}")
    vm.reset()


# =====================================================================================================================
# ON_EVENT with the edges of a pin
# =====================================================================================================================


def ev_state(source: int, event: int, channel: int) -> bytes:
    return struct.pack("<HHB3x", source, event, channel)


@test("events/ON_EVENT on the edges of a pin", hw=True)
def t_on_event(vm: Vm) -> None:
    if not pin_free(vm):
        return
    dev_call(vm, GPIO_ID, IO_SET_MODE, bytes([PIN, 3]))
    dev_call(vm, GPIO_ID, IO_SET_LEVEL, bytes([PIN, 1]))
    r = dev_call(vm, GPIO_ID, IO_CONFIGURE_INTR, bytes([PIN]) + struct.pack("<BBHHHH", 3, 0, 0, 0, 0, 0))  # both edges, no debounce
    check("both edges of the pin raise events", ok(r), str(r))
    p = Prog()
    gate = p.b(False)
    rows = {}
    for nm, st in (("any event of the pin", ev_state(GPIO_ID, 0xFFFF, PIN)), ("another pin", ev_state(GPIO_ID, 0xFFFF, PIN + 1)),
                   ("falling, any device and channel", ev_state(0xFFFF, EV_FALLING, 0xFF)), ("rising, any device and channel", ev_state(0xFFFF, EV_RISING, 0xFF)),
                   ("any IO event, any pin", ev_state(0xFFFF, EV_ANY_IO, 0xFF)), ("another device", ev_state(GPIO_ID + 1, 0xFFFF, 0xFF)), ("the pin, gated", ev_state(GPIO_ID, 0xFFFF, PIN))):
        e, v = p.b(), p.i32(-5)
        gated = nm.endswith("gated")
        p.block("ON_EVENT", outs=[v], ens=[gate] if gated else [], eno=e, state=st)
        rows[nm] = (e, v)
    if not loaded(vm, p, X_SCAN, "seven ON_EVENT blocks"):
        pin_release(vm)
        return
    ids = [x for ev in rows.values() for x in ev]

    def fired(g):
        return {nm: vm.vals(g, e) for nm, (e, v) in rows.items()}

    def only(g, names):
        f = fired(g)
        return all(f[n] == (1 if n in names else 0) for n in rows)

    g = vm.step(ids)
    check("no event: nobody fires", only(g, []), str(fired(g)))
    dev_call(vm, GPIO_ID, IO_SET_LEVEL, bytes([PIN, 0]))
    time.sleep(0.15)
    g = vm.step(ids)
    names = ["any event of the pin", "falling, any device and channel", "any IO event, any pin"]
    check("a falling edge: the matching blocks fire (pin, falling, any IO event) and the others do not", only(g, names), str(fired(g)))
    check("... the value of the event is the level (0)", vm.vals(g, rows["any event of the pin"][1]) == 0 and vm.vals(g, rows["falling, any device and channel"][1]) == 0, str(vm.vals(g, rows["any event of the pin"][1])))
    check("a block that did not fire leaves its value alone", vm.vals(g, rows["another pin"][1]) == -5)
    g = vm.step(ids)
    check("an event lives one pass: the next pass nobody fires", only(g, []), str(fired(g)))
    dev_call(vm, GPIO_ID, IO_SET_LEVEL, bytes([PIN, 1]))
    time.sleep(0.15)
    g = vm.step(ids)
    check("a rising edge: pin, rising and any-IO fire; the value is 1", only(g, ["any event of the pin", "rising, any device and channel", "any IO event, any pin"]) and vm.vals(g, rows["rising, any device and channel"][1]) == 1, str(fired(g)))
    dev_call(vm, GPIO_ID, IO_SET_LEVEL, bytes([PIN, 0]))
    dev_call(vm, GPIO_ID, IO_SET_LEVEL, bytes([PIN, 1]))
    time.sleep(0.2)
    g = vm.step(ids)
    check("two edges before one pass (fall, rise): one pulse per block, falling and rising blocks both fire, the value is the last (1)",
          only(g, ["any event of the pin", "falling, any device and channel", "rising, any device and channel", "any IO event, any pin"]) and vm.vals(g, rows["any event of the pin"][1]) == 1, str(fired(g)))

    # a closed gate misses events
    dev_call(vm, GPIO_ID, IO_SET_LEVEL, bytes([PIN, 0]))
    time.sleep(0.15)
    vm.step(ids, {gate: True})
    g = vm.step(ids)
    check("an event that came while the block was disabled is missed (the gate opens later)", vm.vals(g, rows["the pin, gated"][0]) == 0, str(fired(g)))
    dev_call(vm, GPIO_ID, IO_SET_LEVEL, bytes([PIN, 1]))
    time.sleep(0.15)
    g = vm.step(ids)
    check("with the gate open the same block fires", vm.vals(g, rows["the pin, gated"][0]) == 1, str(fired(g)))
    vm.reset()
    pin_release(vm)

    # shape
    p = Prog()
    p.block("ON_EVENT", outs=[p.i32(), p.i32()], state=ev_state(0, 0xFFFF, 0xFF))
    check("ON_EVENT with two outputs -> ERR_VM_BLK_BAD_SHAPE", cause(vm.load(p)) == "ERR_VM_BLK_BAD_SHAPE")
    p = Prog()
    p.block("ON_EVENT", ins=[p.i32()], state=ev_state(0, 0xFFFF, 0xFF))
    check("ON_EVENT with an input -> ERR_VM_BLK_BAD_SHAPE", cause(vm.load(p)) == "ERR_VM_BLK_BAD_SHAPE")
    p = Prog()
    p.block("ON_EVENT", state=ev_state(0, 0xFFFF, 0xFF)[:6])
    check("ON_EVENT with a short state -> ERR_VM_BLK_BAD_SHAPE", cause(vm.load(p)) == "ERR_VM_BLK_BAD_SHAPE")
    p = Prog()
    p.block("ON_EVENT", state=ev_state(0, 0xFFFF, 0xFF))
    check("ON_EVENT with no output and no ENO loads", vm.load(p) is None)
    vm.reset()
