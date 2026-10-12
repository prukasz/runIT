"""VM core black-box tests over the router (ROUTER_KEY_VM = 0x0B), for the empty block palette.

  Board:  python vm_core_tests.py --port COM11

Covers the loader (open, objects, values, fail closed), starting an empty program, telemetry as the same command
the app writes (subscribe, values out, runtime write, a telemetry frame fed back as a command), runtime write
refusals, reset and the slow-motion op. No block is loaded: the palette is not ported yet (VM_CORE.MD). Run with the
IDF venv Python. The board is reset first, so it starts from an empty VM.
"""

from __future__ import annotations

import argparse
import struct
import sys
import time
import zlib

from router_link import STREAM_TELEMETRY, KEY_SYS, Link, Response

KEY_VM = 0x0B
OP_RESET, OP_OPEN, OP_ADD_OBJECTS, OP_SET_DATA, OP_ADD_ACCESSORS, OP_ADD_BLOCK = 1, 2, 3, 4, 5, 6
OP_SUBSCRIBE, OP_EXEC, OP_SPEED = 7, 8, 9
EXEC_NORMAL, EXEC_PAUSE, EXEC_RESUME, EXEC_ACK_FAULT = 5, 6, 7, 9

T_U8, T_U32, T_B = 2, 3, 6
F_MUTABLE = 1

RESULTS: list[tuple[str, bool, str]] = []


def check(name: str, ok: bool, detail: str = "") -> bool:
    RESULTS.append((name, ok, detail))
    print(f"{'PASS' if ok else 'FAIL'}  {name}" + (f"  -- {detail}" if detail and not ok else ""))
    return ok


def cause(r: Response | None) -> str:
    return r.record.cause.name if r and r.record and r.record.cause else ""


def vm(link: Link, op: int, payload: bytes = b"", timeout: float = 2.0) -> Response | None:
    return link.request(bytes([KEY_VM, op]), payload, timeout=timeout)


def obj_rec(obj_id: int, otype: int, nbytes: int, flags: int = F_MUTABLE, name: bytes = b"") -> bytes:
    """vm_wire_obj_t (id + the 4-byte head) and the name."""
    return struct.pack("<HHBB", obj_id, nbytes, otype | (len(name) << 4), flags) + name


def data_rec(obj_id: int, start: int, data: bytes) -> bytes:
    return struct.pack("<HHH", obj_id, start, len(data)) + data


def batch(*records: bytes) -> bytes:
    return bytes([len(records)]) + b"".join(records)


def open_prog(link: Link, objs: int, accs: int = 0, blks: int = 0, size: int = 1024) -> Response | None:
    return vm(link, OP_OPEN, struct.pack("<HHHI", objs, accs, blks, size))


def exec_cmd(link: Link, cmd: int) -> Response | None:
    return vm(link, OP_EXEC, bytes([cmd]))


def parse_values(frame: bytes) -> dict[int, bytes] | None:
    """A telemetry frame [0x0B][SET_DATA][n] n x (id, start, len, data) -> {id: data} (only start 0)."""
    if len(frame) < 3 or frame[0] != KEY_VM or frame[1] != OP_SET_DATA:
        return None
    n, off, out = frame[2], 3, {}
    for _ in range(n):
        if off + 6 > len(frame):
            return None
        oid, start, ln = struct.unpack_from("<HHH", frame, off)
        off += 6
        if off + ln > len(frame):
            return None
        if start == 0:
            out[oid] = frame[off:off + ln]
        off += ln
    return out if off == len(frame) else None


def values_since(link: Link, t: float) -> list[tuple[bytes, dict[int, bytes]]]:
    res = []
    for p in link.stream_since(STREAM_TELEMETRY, t):
        v = parse_values(p)
        if v is not None:
            res.append((p, v))
    return res


def wait_value(link: Link, t: float, oid: int, want: bytes, timeout: float = 3.0):
    end = time.time() + timeout
    while time.time() < end:
        for frame, v in values_since(link, t):
            if v.get(oid) == want:
                return frame
        time.sleep(0.1)
    return None


# ---------------------------------------------------------------------------


def test_loader(link: Link) -> None:
    r = vm(link, OP_RESET)
    check("RESET on an empty VM", r is not None and r.ok, str(r))

    r = vm(link, OP_ADD_OBJECTS, batch(obj_rec(0, T_B, 1)))
    check("ADD_OBJECTS before OPEN -> ERR_VM_LOAD_BAD_STATE", cause(r) == "ERR_VM_LOAD_BAD_STATE", str(r))

    r = vm(link, OP_OPEN, b"\x01\x00")
    check("truncated OPEN -> ERR_VM_LOAD_SHORT_RECORD", cause(r) == "ERR_VM_LOAD_SHORT_RECORD", str(r))

    r = open_prog(link, 3, size=200 * 1024)  # CONFIG_VM_STORE_MAX_POOL defaults to 128 KB
    check("OPEN over the pool size -> error", r is not None and not r.ok, str(r))

    r = open_prog(link, 3)
    check("OPEN 3 objects", r is not None and r.ok, str(r))
    r = vm(link, OP_ADD_OBJECTS, batch(obj_rec(0, 0, 1)))
    check("object of type NONE -> ERR_VM_OBJ_BAD_TYPE", cause(r) == "ERR_VM_OBJ_BAD_TYPE", str(r))
    r = vm(link, OP_ADD_OBJECTS, batch(obj_rec(1, T_B, 1)))
    check("failed load discards the program (next op -> ERR_VM_LOAD_BAD_STATE)",
          cause(r) == "ERR_VM_LOAD_BAD_STATE", str(r))

    open_prog(link, 2)
    r = vm(link, OP_ADD_OBJECTS, batch(obj_rec(0, T_U32, 6)))
    check("U32 object with 6 bytes (not a multiple of 4) -> ERR_VM_OBJ_BAD_SIZE", cause(r) == "ERR_VM_OBJ_BAD_SIZE",
          str(r))

    open_prog(link, 2)
    r = vm(link, OP_ADD_OBJECTS, batch(obj_rec(0, T_B, 1), obj_rec(0, T_B, 1)))
    check("same object ID twice -> ERR_VM_REG_DUP", cause(r) == "ERR_VM_REG_DUP", str(r))

    open_prog(link, 1)
    r = vm(link, OP_ADD_OBJECTS, batch(obj_rec(5, T_B, 1)))
    check("object ID above OPEN's count -> ERR_VM_REG_OOB", cause(r) == "ERR_VM_REG_OOB", str(r))

    open_prog(link, 1, blks=1)
    vm(link, OP_ADD_OBJECTS, batch(obj_rec(0, T_B, 1)))
    r = exec_cmd(link, EXEC_NORMAL)
    check("start with a block slot missing -> ERR_VM_LOAD_INCOMPLETE", cause(r) == "ERR_VM_LOAD_INCOMPLETE", str(r))

    open_prog(link, 1, blks=1)
    vm(link, OP_ADD_OBJECTS, batch(obj_rec(0, T_B, 1)))
    blk = struct.pack("<HHBBBBBBHH", 0, 0, 63, 0, 0, 0, 0, 1, 0, 0xFFFF)  # type 63: not registered
    r = vm(link, OP_ADD_BLOCK, blk)
    check("block of an unknown type -> ERR_VM_BLK_UNKNOWN_TYPE", cause(r) == "ERR_VM_BLK_UNKNOWN_TYPE", str(r))

    r = vm(link, OP_RESET)
    check("RESET after the abort", r is not None and r.ok, str(r))


def test_run(link: Link) -> None:
    # 0: B flag, 1: two U32 named "val", 2: a constant U8
    open_prog(link, 3)
    r = vm(link, OP_ADD_OBJECTS, batch(obj_rec(0, T_B, 1), obj_rec(1, T_U32, 8, name=b"val"), obj_rec(2, T_U8, 1, 0)))
    check("ADD_OBJECTS: flag, 2 x U32 named, constant", r is not None and r.ok, str(r))
    r = vm(link, OP_SET_DATA, batch(data_rec(1, 0, struct.pack("<II", 7, 9)), data_rec(2, 0, b"\x2a")))
    check("SET_DATA initial values", r is not None and r.ok, str(r))
    r = vm(link, OP_SET_DATA, batch(data_rec(1, 1, struct.pack("<II", 1, 2))))
    check("SET_DATA past the end -> ERR_VM_LOAD_DATA_RANGE", cause(r) == "ERR_VM_LOAD_DATA_RANGE", str(r))

    open_prog(link, 3)
    vm(link, OP_ADD_OBJECTS, batch(obj_rec(0, T_B, 1), obj_rec(1, T_U32, 8, name=b"val"), obj_rec(2, T_U8, 1, 0)))
    vm(link, OP_SET_DATA, batch(data_rec(1, 0, struct.pack("<II", 7, 9)), data_rec(2, 0, b"\x2a")))

    r = exec_cmd(link, EXEC_NORMAL)
    check("EXEC normal starts an empty program", r is not None and r.ok, str(r))
    r = open_prog(link, 1)
    check("OPEN while running replaces the program and stops it", r is not None and r.ok, str(r))
    r = vm(link, OP_ADD_OBJECTS, batch(obj_rec(0, T_B, 1), obj_rec(1, T_U32, 8, name=b"val")))
    check("... and the new program has the new, smaller object count -> ERR_VM_REG_OOB", cause(r) == "ERR_VM_REG_OOB",
          str(r))
    open_prog(link, 3)
    vm(link, OP_ADD_OBJECTS, batch(obj_rec(0, T_B, 1), obj_rec(1, T_U32, 8, name=b"val"), obj_rec(2, T_U8, 1, 0)))
    vm(link, OP_SET_DATA, batch(data_rec(1, 0, struct.pack("<II", 7, 9)), data_rec(2, 0, b"\x2a")))
    r = exec_cmd(link, EXEC_NORMAL)
    check("EXEC normal", r is not None and r.ok, str(r))

    # Telemetry: subscribe, everything once, then only changes.
    t0 = link.mark()
    r = vm(link, OP_SUBSCRIBE, batch(struct.pack("<H", 0), struct.pack("<H", 1), struct.pack("<H", 2)))
    check("SUBSCRIBE 0, 1, 2", r is not None and r.ok, str(r))
    frame = wait_value(link, t0, 1, struct.pack("<II", 7, 9))
    check("telemetry sends the loaded values: SET_DATA frame [0x0B][0x04][n]...", frame is not None)
    if frame:
        v = parse_values(frame)
        check("the frame carries every subscribed object once", v is not None and set(v) == {0, 1, 2}, str(v))

    # Runtime write: the same SET_DATA the app wrote the initial value with.
    t1 = link.mark()
    r = vm(link, OP_SET_DATA, batch(data_rec(1, 0, struct.pack("<II", 11, 13))))
    check("runtime SET_DATA of a mutable object", r is not None and r.ok, str(r))
    frame = wait_value(link, t1, 1, struct.pack("<II", 11, 13))
    check("... shows up in telemetry at the next pass", frame is not None)
    only = values_since(link, t1)
    check("only the changed object is sent again", all(set(v) == {1} for _, v in only if 1 in v), str(only))

    # A telemetry frame is a command: feed it back after changing the value.
    if frame:
        vm(link, OP_SET_DATA, batch(data_rec(1, 0, struct.pack("<II", 0, 0))))
        wait_value(link, t1, 1, struct.pack("<II", 0, 0))
        t2 = link.mark()
        r = link.request(bytes([frame[0], frame[1]]), frame[2:])
        check("a telemetry frame sent back is accepted as a command", r is not None and r.ok, str(r))
        check("... and it sets the same values again",
              wait_value(link, t2, 1, struct.pack("<II", 11, 13)) is not None)

    r = vm(link, OP_SET_DATA, batch(data_rec(2, 0, b"\x01")))
    check("runtime write to a constant -> ERR_VM_OBJ_NOT_MUTABLE", cause(r) == "ERR_VM_OBJ_NOT_MUTABLE", str(r))
    r = vm(link, OP_SET_DATA, batch(data_rec(9, 0, b"\x01")))
    check("runtime write to a missing object -> error", r is not None and not r.ok, str(r))
    r = vm(link, OP_ADD_OBJECTS, batch(obj_rec(0, T_B, 1)))
    check("ADD_OBJECTS while running is refused", r is not None and not r.ok, str(r))
    r = vm(link, OP_SET_DATA, batch(data_rec(1, 0, struct.pack("<II", 11, 13))))
    check("the refused load op did not discard the running program", r is not None and r.ok, str(r))

    r = exec_cmd(link, EXEC_PAUSE)
    check("EXEC pause", r is not None and r.ok, str(r))
    r = exec_cmd(link, EXEC_RESUME)
    check("EXEC resume", r is not None and r.ok, str(r))
    r = exec_cmd(link, 99)
    check("EXEC unknown command -> error", r is not None and not r.ok, str(r))
    r = vm(link, OP_EXEC, b"")
    check("EXEC without body -> ERR_VM_LOAD_SHORT_RECORD", cause(r) == "ERR_VM_LOAD_SHORT_RECORD", str(r))

    r = vm(link, OP_SPEED, struct.pack("<H", 0))
    check("SPEED 0 -> error", r is not None and not r.ok, str(r))
    r = vm(link, OP_SPEED, struct.pack("<H", 2))
    check("SPEED 2", r is not None and r.ok, str(r))
    r = vm(link, OP_SPEED, struct.pack("<H", 1))
    check("SPEED 1", r is not None and r.ok, str(r))

    r = vm(link, OP_SUBSCRIBE, bytes([0]))
    check("SUBSCRIBE 0 clears", r is not None and r.ok, str(r))
    t3 = link.mark()
    vm(link, OP_SET_DATA, batch(data_rec(1, 0, struct.pack("<II", 5, 6))))
    time.sleep(1.0)
    check("... and nothing is sent any more", not values_since(link, t3))

    r = vm(link, OP_RESET)
    check("RESET stops and unloads", r is not None and r.ok, str(r))
    r = vm(link, OP_SET_DATA, batch(data_rec(1, 0, struct.pack("<II", 1, 2))))
    check("SET_DATA after RESET -> error", r is not None and not r.ok, str(r))


# ---- blocks ----------------------------------------------------------------

B_EXPR, B_EXPR_BIT, B_IF, B_SWITCH, B_FOR, B_SET, B_CLONE, B_EDGE, B_TIMER = 1, 2, 3, 4, 5, 6, 7, 8, 9
B_LATCH, B_PERIODIC = 12, 13
NO_ID = 0xFFFF
F_OUT = F_MUTABLE
T_F = 5

# expression opcodes (vm_expr_core.h)
X_END, X_IN, X_K, X_ADD, X_MUL = 0, 1, 2, 6, 8
BX_IN, BX_K, BX_OR, BX_SHL = 1, 2, 7, 9


def blk(blk_id: int, btype: int, ins=(), outs=(), ens=(), custom: bytes = b"", eno: int = NO_ID) -> bytes:
    """vm_wire_block_t, the pin ids and the state (on_error CONTINUE, enables ANY)."""
    head = struct.pack("<HHBBBBBBHH", blk_id, blk_id, btype, len(ins), len(outs), len(ens), 0, 1, len(custom), eno)
    pins = b"".join(struct.pack("<H", p) for p in (*ins, *outs, *ens))
    return head + pins + custom


def expr_state(consts: list[float], code: list[int]) -> bytes:
    return struct.pack("<BBH", len(consts), 0, len(code)) + b"".join(struct.pack("<f", k) for k in consts) + bytes(code)


def bit_state(consts: list[int], code: list[int]) -> bytes:
    return struct.pack("<BBH", len(consts), 0, len(code)) + b"".join(struct.pack("<I", k) for k in consts) + bytes(code)


def acc_rec(acc_id: int, root: int) -> bytes:
    return struct.pack("<HHBB", acc_id, root, 0, 0)


def load_program(link: Link, objs: list[bytes], accs: dict[int, int], blocks: list[bytes], data: list[bytes] = (),
                 size: int = 8192) -> Response | None:
    """OPEN + objects + values + accessors + blocks (one per frame). The first failing response, else None."""
    steps = [open_prog(link, len(objs), len(accs), len(blocks), size), vm(link, OP_ADD_OBJECTS, batch(*objs))]
    if data:
        steps.append(vm(link, OP_SET_DATA, batch(*data)))
    if accs:
        steps.append(vm(link, OP_ADD_ACCESSORS, batch(*(acc_rec(a, o) for a, o in accs.items()))))
    for b in blocks:
        steps.append(vm(link, OP_ADD_BLOCK, b))
    for r in steps:
        if r is None or not r.ok:
            return r
    return None


def f32(x: float) -> bytes:
    return struct.pack("<f", x)


def u32(x: int) -> bytes:
    return struct.pack("<I", x)


def start(link: Link, ids: list[int]) -> float:
    """Subscribe to the objects, start normal mode; the mark taken before the subscription."""
    t = link.mark()
    vm(link, OP_SUBSCRIBE, batch(*(struct.pack("<H", i) for i in ids)))
    exec_cmd(link, EXEC_NORMAL)
    return t


def test_blocks_logic(link: Link) -> None:
    # 0 a, 1 b, 2 result (F); 3 x, 4 y, 5 bit result (U32); 6 cond, 7 if-true, 8 if-false (B); 9 src, 10 dst (F);
    # 11 selector (U32), 12..14 switch outputs (B)
    objs = [obj_rec(i, T_F, 4) for i in (0, 1, 2)] + [obj_rec(i, T_U32, 4) for i in (3, 4, 5)] \
        + [obj_rec(i, T_B, 1) for i in (6, 7, 8)] + [obj_rec(i, T_F, 4) for i in (9, 10)] \
        + [obj_rec(11, T_U32, 4)] + [obj_rec(i, T_B, 1) for i in (12, 13, 14)]
    accs = {i: i for i in range(15)}
    blocks = [
        blk(0, B_EXPR, ins=[0, 1], outs=[2], custom=expr_state([2.0], [X_IN, 0, X_K, 0, X_ADD, X_IN, 1, X_MUL, X_END])),
        blk(1, B_EXPR_BIT, ins=[3, 4], outs=[5], custom=bit_state([4], [BX_IN, 0, BX_K, 0, BX_SHL, BX_IN, 1, BX_OR, 0])),
        blk(2, B_IF, ins=[6], outs=[7, 8]),
        blk(3, B_SWITCH, ins=[11], outs=[12, 13, 14]),
        blk(4, B_SET, ins=[9, 10], custom=f32(0.0) + bytes(4)),
    ]
    r = load_program(link, objs, accs, blocks)
    check("logic program loads: EXPR, EXPR_BIT, IF, SWITCH, SET", r is None, str(r))
    if r is not None:
        return
    t = start(link, [2, 5, 7, 8, 10, 12, 13, 14])
    r = vm(link, OP_SET_DATA, batch(data_rec(0, 0, f32(3)), data_rec(1, 0, f32(4)), data_rec(3, 0, u32(10)),
                                    data_rec(4, 0, u32(5)), data_rec(6, 0, b"\x01"), data_rec(9, 0, f32(9)),
                                    data_rec(11, 0, u32(1))))
    check("runtime write of the inputs", r is not None and r.ok, str(r))
    check("EXPR (a + 2) * b = 20", wait_value(link, t, 2, f32(20)) is not None)
    check("EXPR_BIT (x << 4) | y = 165", wait_value(link, t, 5, u32(165)) is not None)
    check("IF true: first output on", wait_value(link, t, 7, b"\x01") is not None)
    check("SWITCH 1: second output on", wait_value(link, t, 13, b"\x01") is not None)
    check("SET copies the source to the destination", wait_value(link, t, 10, f32(9)) is not None)

    t2 = link.mark()
    vm(link, OP_SET_DATA, batch(data_rec(0, 0, f32(5)), data_rec(6, 0, b"\x00"), data_rec(11, 0, u32(2))))
    check("EXPR follows a changed input: (5 + 2) * 4 = 28", wait_value(link, t2, 2, f32(28)) is not None)
    check("IF false: second output on", wait_value(link, t2, 8, b"\x01") is not None)
    check("... first output cleared", wait_value(link, t2, 7, b"\x00") is not None)
    check("SWITCH 2: third output on", wait_value(link, t2, 14, b"\x01") is not None)
    vm(link, OP_RESET)


def test_blocks_state(link: Link) -> None:
    # 0 gate, 1 periodic ENO, 2 latch ENO, 3 edge ENO, 4 timer ENO, 6 latch reset (B); 5 timer ET (U32)
    objs = [obj_rec(i, T_B, 1) for i in (0, 1, 2, 3, 4)] + [obj_rec(5, T_U32, 4)] + [obj_rec(6, T_B, 1)]
    accs = {0: 0, 1: 1, 2: 6}  # gate, periodic ENO, latch reset
    timer = bytes(4) + u32(0) + u32(300) + u32(0) + struct.pack("<Q", 0) + u32(0) + bytes(4)  # TON, 300 ms
    assert len(timer) == 32
    blocks = [
        blk(0, B_PERIODIC, ens=[0], custom=struct.pack("<IBBHQ", 200, 0, 0, 0, 0), eno=1),
        blk(1, B_LATCH, ins=[2], ens=[1], custom=struct.pack("<BBH", 0, 0, 0), eno=2),
        blk(2, B_EDGE, ens=[0], custom=bytes(4), eno=3),
        blk(3, B_TIMER, outs=[5], ens=[0], custom=timer, eno=4),
    ]
    r = load_program(link, objs, accs, blocks)
    check("state program loads: PERIODIC, LATCH, EDGE, TIMER", r is None, str(r))
    if r is not None:
        return
    t = start(link, [1, 2, 3, 4, 5])
    time.sleep(1.0)
    check("gate off: PERIODIC does not tick", not any(v.get(1) == b"\x01" for _, v in values_since(link, t)))

    t1 = link.mark()
    vm(link, OP_SET_DATA, batch(data_rec(0, 0, b"\x01")))
    check("EDGE pulses on the rising gate", wait_value(link, t1, 3, b"\x01") is not None)
    check("TIMER ENO true after the preset", wait_value(link, t1, 4, b"\x01", 2.0) is not None)
    check("TIMER ET is reported", any(v.get(5) not in (None, u32(0)) for _, v in values_since(link, t1)))
    t_win = link.mark()
    time.sleep(1.4)
    ticks = sum(1 for _, v in values_since(link, t_win) if v.get(1) == b"\x01")
    check("PERIODIC 200 ms ticks while the gate is open (about 7 in 1.4 s)", 5 <= ticks <= 9, str(ticks))
    check("LATCH holds after the first tick", wait_value(link, t1, 2, b"\x01") is not None)

    t2 = link.mark()
    vm(link, OP_SET_DATA, batch(data_rec(0, 0, b"\x00")))
    check("TIMER ENO drops with the gate", wait_value(link, t2, 4, b"\x00") is not None)
    time.sleep(0.6)
    check("PERIODIC stops with the gate", not any(v.get(1) == b"\x01" for _, v in values_since(link, link.mark() - 0.5)))
    check("LATCH stays set without ticks", not any(v.get(2) == b"\x00" for _, v in values_since(link, t2)))
    t3 = link.mark()
    vm(link, OP_SET_DATA, batch(data_rec(6, 0, b"\x01")))
    check("LATCH RESET clears it", wait_value(link, t3, 2, b"\x00") is not None)
    vm(link, OP_RESET)


def test_blocks_for(link: Link) -> None:
    # 0 loop index (F), 1 result (F). FOR 0..5 step 1 runs the EXPR body (index * 2) every turn.
    objs = [obj_rec(0, T_F, 4), obj_rec(1, T_F, 4)]
    accs = {0: 0}
    loop = struct.pack("<HHfffHBBB3x", 1, 2, 0.0, 5.0, 1.0, 100, 0, 0, 0)  # span [1, 2), + step, < end
    assert len(loop) == 24
    blocks = [
        blk(0, B_FOR, outs=[0], custom=loop),
        blk(1, B_EXPR, ins=[0], outs=[1], custom=expr_state([2.0], [X_IN, 0, X_K, 0, X_MUL, X_END])),
    ]
    r = load_program(link, objs, accs, blocks)
    check("FOR program loads (loop + body)", r is None, str(r))
    if r is not None:
        return
    t = start(link, [0, 1])
    check("FOR runs its body each turn: the last index 4 gives 8", wait_value(link, t, 1, f32(8)) is not None)
    check("... and leaves the index at 4", wait_value(link, t, 0, f32(4)) is not None)
    vm(link, OP_RESET)


KEY_DEVICE, KEY_ACTIONS = 0x05, 0x06
A_RUN, A_BEGIN, A_WRITE, A_COMMIT, A_REMOVE = 0x01, 0x04, 0x05, 0x06, 0x0A
B_ACTION, B_ON_EVENT = 14, 15


def upload_record(link: Link, rid: int, *frames: bytes) -> Response | None:
    """A record of the store (SYS_ACTIONS.MD): the frames as `[u16 len][keys + payload]`, BEGIN / WRITE / COMMIT."""
    img = b"".join(struct.pack("<H", len(f)) + f for f in frames)
    req = lambda op, p: link.request(bytes([KEY_ACTIONS, op]), p)  # noqa: E731
    r = req(A_BEGIN, struct.pack("<IH", rid, len(img)))
    if r is None or not r.ok:
        return r
    for off in range(0, len(img), 64):
        r = req(A_WRITE, struct.pack("<H", off) + img[off:off + 64])
        if r is None or not r.ok:
            return r
    return req(A_COMMIT, struct.pack("<I", zlib.crc32(img)))


def test_block_action(link: Link) -> None:
    # 0 target (F), 1 gate (B), 2 id pin (U32), 3 ENO (B). The records write the target through the VM's own
    # SET_DATA command, so the same frame that telemetry sends and the app writes is what a record replays.
    def set_target(v: float) -> bytes:
        return bytes([KEY_VM, OP_SET_DATA]) + batch(data_rec(0, 0, f32(v)))

    for rid in (7, 8):
        link.request(bytes([KEY_ACTIONS, A_REMOVE]), u32(rid))
    check("record 7 uploaded (sets the target to 42)", (r := upload_record(link, 7, set_target(42))) is not None and r.ok, str(r))
    check("record 8 uploaded (sets the target to 43)", (r := upload_record(link, 8, set_target(43))) is not None and r.ok, str(r))

    objs = [obj_rec(0, T_F, 4), obj_rec(1, T_B, 1), obj_rec(2, T_U32, 4), obj_rec(3, T_B, 1)]
    r = load_program(link, objs, {0: 1, 1: 2}, [blk(0, B_ACTION, ens=[0], custom=u32(7) + bytes(4), eno=3)])
    check("ACTION program loads (id from the state)", r is None, str(r))
    if r is None:
        t = start(link, [0, 3])
        vm(link, OP_SET_DATA, batch(data_rec(1, 0, b"\x01")))
        check("rising enable runs the record: the target becomes 42", wait_value(link, t, 0, f32(42)) is not None)
        check("... and ENO pulses on the request", wait_value(link, t, 3, b"\x01") is not None)
        vm(link, OP_SET_DATA, batch(data_rec(0, 0, f32(0))))
        t2 = link.mark()
        time.sleep(0.7)
        check("a held enable does not fire again", not any(v.get(0) == f32(42) for _, v in values_since(link, t2)))
        vm(link, OP_SET_DATA, batch(data_rec(1, 0, b"\x00")))
        time.sleep(0.2)
        t3 = link.mark()
        vm(link, OP_SET_DATA, batch(data_rec(1, 0, b"\x01")))
        check("a new rising edge fires it again", wait_value(link, t3, 0, f32(42)) is not None)
        vm(link, OP_RESET)

    # The id from the pin replaces the state's.
    r = load_program(link, objs, {0: 1, 1: 2}, [blk(0, B_ACTION, ins=[1], ens=[0], custom=u32(7) + bytes(4), eno=3)],
                     data=[data_rec(2, 0, u32(8))])
    check("ACTION program loads (id from the pin)", r is None, str(r))
    if r is None:
        t = start(link, [0])
        vm(link, OP_SET_DATA, batch(data_rec(1, 0, b"\x01")))
        check("the pin's id (8) runs: the target becomes 43", wait_value(link, t, 0, f32(43)) is not None)
        vm(link, OP_RESET)

    r = load_program(link, objs, {0: 1}, [blk(0, B_ACTION, ens=[0], custom=u32(0) + bytes(4))])
    check("ACTION with no id (pin unwired, state 0) is rejected", cause(r) == "ERR_VM_BLK_BAD_SHAPE", str(r))

    # A missing record is reported when the request runs; the block itself loads.
    r = load_program(link, objs, {0: 1}, [blk(0, B_ACTION, ens=[0], custom=u32(0x00F0F0F0) + bytes(4), eno=3)])
    if r is None:
        t = start(link, [3])
        mark = link.mark()
        vm(link, OP_SET_DATA, batch(data_rec(1, 0, b"\x01")))
        time.sleep(0.5)
        errs = [e.cause.name for e in link.error_records_since(mark) if e.cause]
        check("a request for a missing record is reported (ERR_ACTION_NOT_FOUND)", "ERR_ACTION_NOT_FOUND" in errs, str(errs))
        vm(link, OP_RESET)
    for rid in (7, 8):
        link.request(bytes([KEY_ACTIONS, A_REMOVE]), u32(rid))


GPIO_TYPE, GPIO_ID, OUT_PIN = 0x40, 0, 4


def gpio(link: Link, op: int, payload: bytes = b"") -> Response | None:
    return link.request(bytes([KEY_DEVICE]) + struct.pack("<HH", GPIO_ID, op), payload)


def test_block_on_event(link: Link) -> None:
    # The falling edge of a push-pull pin raises an IO event (GPIO ISR -> sys_event -> the VM's queue -> this pass).
    r = gpio(link, 0x0000, struct.pack("<H", GPIO_TYPE))  # CREATE GPIO ESP
    check("GPIO ESP device created for the events", r is not None and r.ok, str(r))
    if r is None or not r.ok:
        return
    gpio(link, 0x0105, bytes([OUT_PIN, 3]))  # SET_MODE push-pull
    gpio(link, 0x0100, bytes([OUT_PIN, 0]))  # SET_LEVEL 0
    r = gpio(link, 0x0107, bytes([OUT_PIN]) + struct.pack("<BBHHHH", 2, 0, 0, 0, 0, 0))  # CONFIGURE_INTR falling
    check("CONFIGURE_INTR falling edge", r is not None and r.ok, str(r))

    # 0 VALUE (I32), 1 ENO any event of the pin, 2 ENO wrong channel, 3 VALUE of the second block
    objs = [obj_rec(0, 4, 4), obj_rec(1, T_B, 1), obj_rec(2, T_B, 1), obj_rec(3, 4, 4)]
    any_event = struct.pack("<HHB3x", GPIO_ID, 0xFFFF, OUT_PIN)
    wrong_pin = struct.pack("<HHB3x", GPIO_ID, 0xFFFF, OUT_PIN + 1)
    r = load_program(link, objs, {}, [blk(0, B_ON_EVENT, outs=[0], custom=any_event, eno=1),
                                      blk(1, B_ON_EVENT, outs=[3], custom=wrong_pin, eno=2)])
    check("ON_EVENT program loads", r is None, str(r))
    if r is None:
        t = start(link, [0, 1, 2])
        gpio(link, 0x0100, bytes([OUT_PIN, 1]))
        time.sleep(0.2)
        t1 = link.mark()
        gpio(link, 0x0100, bytes([OUT_PIN, 0]))
        check("a falling edge of the pin pulses ENO", wait_value(link, t1, 1, b"\x01") is not None)
        time.sleep(0.3)
        check("the block filtering another channel stays quiet",
              not any(v.get(2) == b"\x01" for _, v in values_since(link, t)))
        vm(link, OP_RESET)

    r = load_program(link, objs, {}, [blk(0, B_ON_EVENT, outs=[0, 3], custom=any_event)])
    check("ON_EVENT with two outputs is rejected", cause(r) == "ERR_VM_BLK_BAD_SHAPE", str(r))
    vm(link, OP_RESET)
    gpio(link, 0x0107, bytes([OUT_PIN]) + struct.pack("<BBHHHH", 0, 0, 0, 0, 0, 0))  # intr off
    gpio(link, 0x0004)  # REMOVE


def test_blocks_reject(link: Link) -> None:
    objs = [obj_rec(i, T_F, 4) for i in range(3)] + [obj_rec(3, T_B, 1), obj_rec(4, T_B, 1)]
    accs = {0: 0, 1: 1, 2: 3}  # accessor ids are dense: 2 reads the B object 3

    def rejects(name: str, block: bytes) -> None:
        r = load_program(link, objs, accs, [block])
        check(name, cause(r) == "ERR_VM_BLK_BAD_SHAPE", str(r))

    rejects("EXPR with an unknown opcode", blk(0, B_EXPR, ins=[0], outs=[2], custom=expr_state([], [X_IN, 0, 0xFE])))
    rejects("EXPR whose stack ends with two values",
            blk(0, B_EXPR, ins=[0, 1], outs=[2], custom=expr_state([], [X_IN, 0, X_IN, 1, X_END])))
    rejects("EXPR reading an unwired input", blk(0, B_EXPR, ins=[NO_ID], outs=[2], custom=expr_state([], [X_IN, 0])))
    rejects("EXPR without an output", blk(0, B_EXPR, ins=[0], custom=expr_state([], [X_IN, 0])))
    rejects("IF with one output", blk(0, B_IF, ins=[2], outs=[3]))
    rejects("IF with an unwired condition", blk(0, B_IF, ins=[NO_ID], outs=[3, 4]))
    rejects("SET with the destination unwired", blk(0, B_SET, ins=[0, NO_ID], custom=f32(0) + bytes(4)))
    rejects("SET with a NaN constant", blk(0, B_SET, ins=[0, 1], custom=struct.pack("<I", 0x7FC00000) + bytes(4)))
    rejects("EDGE with no enable", blk(0, B_EDGE, custom=bytes(4), eno=3))
    rejects("LATCH with a bad mode", blk(0, B_LATCH, ens=[2], custom=bytes([9, 0, 0, 0])))
    rejects("PERIODIC with period 0", blk(0, B_PERIODIC, custom=struct.pack("<IBBHQ", 0, 0, 0, 0, 0)))
    rejects("state shorter than the block needs", blk(0, B_SET, ins=[0, 1], custom=bytes(4)))
    vm(link, OP_RESET)


def test_restart(link: Link) -> None:
    """The board is still alive after the VM was used and the VM is empty after a restart."""
    r = link.request(bytes([KEY_SYS, 0x03]))
    for _ in range(40):
        time.sleep(0.5)
        r = link.request(bytes([KEY_SYS, 0x01]))
        if r is not None and r.ok:
            break
    check("board answers after RESTART", r is not None and r.ok)
    r = vm(link, OP_SET_DATA, batch(data_rec(1, 0, struct.pack("<II", 1, 2))))
    check("the VM is empty after a restart", r is not None and not r.ok, str(r))


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", required=True)
    ap.add_argument("--echo", action="store_true")
    a = ap.parse_args()
    with Link(a.port, echo=a.echo) as link:
        time.sleep(1.0)
        for fn in (test_loader, test_run, test_blocks_reject, test_blocks_logic, test_blocks_state, test_blocks_for,
                   test_block_action, test_block_on_event, test_restart):
            print(f"-- {fn.__name__}")
            fn(link)
    bad = [n for n, ok, _ in RESULTS if not ok]
    print(f"\n{len(RESULTS) - len(bad)}/{len(RESULTS)} passed")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
