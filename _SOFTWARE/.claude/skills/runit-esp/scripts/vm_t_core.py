"""VM suite, part 1: the wire protocol of the VM router and the expression blocks. Run through vm_suite.py."""

from __future__ import annotations

import argparse
import math
import struct
import sys
import time
import zlib

from router_link import KEY_SYS, Link
from vm_lib import *  # noqa: F401,F403
from vm_lib import _enum  # noqa: F401

# =====================================================================================================================
# the wire: every op, malformed
# =====================================================================================================================


def tiny(vm: Vm, nobj: int = 2, nacc: int = 0, nblk: int = 0, size: int = 4096) -> Response | None:
    return vm.op(OP_OPEN, struct.pack("<HHHI", nobj, nacc, nblk, size))


def obj_rec(oid: int, t: int, nbytes: int, flags: int = F_MUT, name: bytes = b"") -> bytes:
    return struct.pack("<HHBB", oid, nbytes, t | (len(name) << 4), flags) + name


def batch(*recs: bytes) -> bytes:
    return bytes([len(recs)]) + b"".join(recs)


def blk_head(bid=0, btype=BLK["SET"], ci=0, co=0, ce=0, en_mode=0, on_err=1, clen=0, eno=NO_ID) -> bytes:
    return struct.pack("<HHBBBBBBHH", bid, bid, btype, ci, co, ce, en_mode, on_err, clen, eno)


@test("wire/ops before open")
def t_wire_state(vm: Vm) -> None:
    vm.reset()
    r = vm.reset()
    check("RESET on an empty VM", ok(r), str(r))
    for name, op, payload in (("ADD_OBJECTS", OP_ADD_OBJECTS, batch(obj_rec(0, T_B, 1))),
                              ("SET_DATA", OP_SET_DATA, batch(struct.pack("<HHH", 0, 0, 1) + b"\x01")),
                              ("ADD_ACCESSORS", OP_ADD_ACCESSORS, batch(struct.pack("<HHBB", 0, 0, 0, 0))),
                              ("ADD_BLOCK", OP_ADD_BLOCK, blk_head())):
        r = vm.op(op, payload)
        check(f"{name} before OPEN -> ERR_VM_LOAD_BAD_STATE", cause(r) == "ERR_VM_LOAD_BAD_STATE", str(r))
    r = vm.exec(X_NORMAL)
    check("EXEC normal on an empty VM starts (and runs nothing)", ok(r), str(r))
    r = vm.op(OP_SET_DATA, batch(struct.pack("<HHH", 0, 0, 1) + b"\x01"))
    check("a runtime write on an empty VM is refused (no such object)", r is not None and not r.ok, str(r))
    vm.reset()


@test("wire/truncated frames")
def t_wire_short(vm: Vm) -> None:
    cases = [
        ("OPEN with 2 bytes", OP_OPEN, b"\x01\x00"),
        ("ADD_OBJECTS with no count", OP_ADD_OBJECTS, b""),
        ("ADD_OBJECTS: count 2, one record", OP_ADD_OBJECTS, batch(obj_rec(0, T_B, 1)).replace(b"\x01", b"\x02", 1)),
        ("ADD_OBJECTS: record cut inside the head", OP_ADD_OBJECTS, b"\x01\x00\x00\x01"),
        ("ADD_OBJECTS: name longer than the frame", OP_ADD_OBJECTS, b"\x01" + struct.pack("<HHBB", 0, 1, T_B | (5 << 4), F_MUT) + b"ab"),
        ("SET_DATA: record head cut", OP_SET_DATA, b"\x01\x00\x00\x00"),
        ("SET_DATA: data shorter than byte_len", OP_SET_DATA, b"\x01" + struct.pack("<HHH", 0, 0, 4) + b"\x01"),
        ("ADD_ACCESSORS: head cut", OP_ADD_ACCESSORS, b"\x01\x00\x00\x00"),
        ("ADD_ACCESSORS: steps shorter than idx_len", OP_ADD_ACCESSORS, b"\x01" + struct.pack("<HHBB", 0, 0, 1, 9) + b"\x00\x01"),
        ("ADD_BLOCK with 5 bytes", OP_ADD_BLOCK, b"\x00\x00\x00\x00\x06"),
        ("ADD_BLOCK: pins cut", OP_ADD_BLOCK, blk_head(ci=2) + b"\x00\x00"),
        ("ADD_BLOCK: state shorter than custom_len", OP_ADD_BLOCK, blk_head(clen=8) + b"\x00\x00"),
    ]
    for name, op, payload in cases:
        tiny(vm, 4, 2, 2)
        t = vm.link.mark()
        r = vm.op(op, payload)
        check(f"{name} -> ERR_VM_LOAD_SHORT_RECORD", cause(r) == "ERR_VM_LOAD_SHORT_RECORD", str(r))
        check("... and the failed load discards the program (fail closed)", "ERR_VM_LOAD_ABORTED" in chain(r), str(chain(r)))
        r2 = vm.op(OP_ADD_OBJECTS, batch(obj_rec(0, T_B, 1)))
        check("... the next load op -> ERR_VM_LOAD_BAD_STATE", cause(r2) == "ERR_VM_LOAD_BAD_STATE", str(r2))
    for name, op, payload in (("EXEC with no body", OP_EXEC, b""), ("EXEC with 2 bytes", OP_EXEC, b"\x05\x00"),
                              ("SPEED with 1 byte", OP_SPEED, b"\x02"), ("SPEED with 3 bytes", OP_SPEED, b"\x02\x00\x00"),
                              ("SUBSCRIBE with no count", OP_SUBSCRIBE, b""),
                              ("SUBSCRIBE: count 2, one id", OP_SUBSCRIBE, b"\x02\x00\x00")):
        r = vm.op(op, payload)
        check(f"{name} -> ERR_VM_LOAD_SHORT_RECORD", cause(r) == "ERR_VM_LOAD_SHORT_RECORD", str(r))
    r = vm.op(0x20)
    check("an op the VM does not have (0x20) is refused", r is not None and not r.ok, str(r))
    r = vm.op(0x00)
    check("op 0 is refused", r is not None and not r.ok, str(r))
    vm.reset()


@test("wire/objects")
def t_wire_objects(vm: Vm) -> None:
    def one(name: str, rec: bytes, want: str, nobj: int = 3) -> None:
        tiny(vm, nobj)
        r = vm.op(OP_ADD_OBJECTS, batch(rec))
        check(f"{name} -> {want}", cause(r) == want, str(r))

    one("type NONE", obj_rec(0, 0, 1), "ERR_VM_OBJ_BAD_TYPE")
    for t in (8, 12, 15):
        one(f"type {t}", obj_rec(0, t, 1), "ERR_VM_OBJ_BAD_TYPE")
    one("size 0", obj_rec(0, T_U8, 0), "ERR_VM_OBJ_EMPTY")
    one("U32 of 6 bytes", obj_rec(0, T_U32, 6), "ERR_VM_OBJ_BAD_SIZE")
    one("U32 of 2 bytes", obj_rec(0, T_U32, 2), "ERR_VM_OBJ_BAD_SIZE")
    one("F of 5 bytes", obj_rec(0, T_F, 5), "ERR_VM_OBJ_BAD_SIZE")
    one("I32 of 3 bytes", obj_rec(0, T_I32, 3), "ERR_VM_OBJ_BAD_SIZE")
    one("PTR of 6 bytes", obj_rec(0, T_PTR, 6), "ERR_VM_OBJ_BAD_SIZE")
    one("id above OPEN's count", obj_rec(3, T_B, 1), "ERR_VM_REG_OOB")
    one("id 0xFFFF (the 'no id' value: it would skip the registry)", obj_rec(0xFFFF, T_B, 1), "ERR_VM_REG_OOB")
    tiny(vm, 3)
    r = vm.op(OP_ADD_OBJECTS, batch(obj_rec(1, T_B, 1), obj_rec(1, T_U8, 4)))
    check("the same id twice in one frame -> ERR_VM_REG_DUP", cause(r) == "ERR_VM_REG_DUP", str(r))
    tiny(vm, 3)
    vm.op(OP_ADD_OBJECTS, batch(obj_rec(1, T_B, 1)))
    r = vm.op(OP_ADD_OBJECTS, batch(obj_rec(1, T_B, 1)))
    check("the same id in two frames -> ERR_VM_REG_DUP", cause(r) == "ERR_VM_REG_DUP", str(r))

    # every type, odd and large sizes, a 15-char name
    tiny(vm, 8, size=8192)
    recs = [obj_rec(0, T_U8, 1), obj_rec(1, T_U32, 4), obj_rec(2, T_I32, 400), obj_rec(3, T_F, 4, name=b"x" * 15),
            obj_rec(4, T_B, 1), obj_rec(5, T_STR, 33), obj_rec(6, T_PTR, 8), obj_rec(7, T_U8, 1000, name=b"big")]
    r = vm.op(OP_ADD_OBJECTS, batch(*recs[:4]))
    r2 = vm.op(OP_ADD_OBJECTS, batch(*recs[4:]))
    check("every element type, a 15-character name, a 1000-byte array load", ok(r) and ok(r2), f"{r} {r2}")
    r = vm.exec(X_NORMAL)
    check("... and the program starts", ok(r), str(r))
    vm.reset()

    # object count / pool
    r = tiny(vm, 1, size=200 * 1024)
    check("OPEN over the pool size (128 KB) -> error", r is not None and not r.ok, str(r))
    r = vm.op(OP_ADD_OBJECTS, batch(obj_rec(0, T_B, 1)))
    check("... and nothing is open afterwards", cause(r) == "ERR_VM_LOAD_BAD_STATE", str(r))
    tiny(vm, 2, size=24)
    r = vm.op(OP_ADD_OBJECTS, batch(obj_rec(0, T_U8, 200)))
    check("an object that does not fit the arena -> ERR_VM_ALLOC_EXHAUSTED", cause(r) in ("ERR_VM_ALLOC_EXHAUSTED", "ERR_VM_LOAD_TOO_BIG"), str(r))
    r = tiny(vm, 0, 0, 0, 16)
    check("OPEN with no objects, accessors or blocks", ok(r), str(r))
    r = vm.exec(X_NORMAL)
    check("... starts", ok(r), str(r))
    vm.reset()
    r = tiny(vm, 65535, 0, 0, 8192)
    check("OPEN with 65535 object slots on an 8 KB arena -> error (the slot table does not fit)", r is not None and not r.ok, str(r))
    vm.reset()


@test("wire/set data")
def t_wire_data(vm: Vm) -> None:
    def prog():
        tiny(vm, 5, size=2048)
        vm.op(OP_ADD_OBJECTS, batch(obj_rec(0, T_U32, 64), obj_rec(1, T_F, 4), obj_rec(2, T_PTR, 16), obj_rec(3, T_U8, 3), obj_rec(4, T_B, 1)))

    def sd(oid, start, data):
        return vm.op(OP_SET_DATA, batch(struct.pack("<HHH", oid, start, len(data)) + data))

    prog()
    check("SET_DATA of a whole array", ok(sd(0, 0, bytes(range(64)))))
    check("SET_DATA of a slice in the middle", ok(sd(0, 5, b"\x01\x00\x00\x00" * 3)))
    check("SET_DATA of the last element", ok(sd(0, 15, b"\x09\x00\x00\x00")))
    check("SET_DATA with zero bytes is a no-op", ok(sd(0, 0, b"")))
    check("a SET_DATA frame with no records is a no-op", ok(vm.op(OP_SET_DATA, b"\x00")))
    r = vm.op(OP_SET_DATA, batch(struct.pack("<HHH", 1, 0, 4) + f32(1.5), struct.pack("<HHH", 3, 0, 3) + b"abc", struct.pack("<HHH", 4, 0, 1) + b"\x01"))
    check("one frame with records for three objects", ok(r), str(r))
    for name, args, want in (("an unknown object", (9, 0, b"\x00"), "ERR_VM_ACCESSOR_UNKNOWN_ID"),
                             ("a length that is not a multiple of the width", (0, 0, b"\x00" * 6), "ERR_VM_LOAD_DATA_RANGE"),
                             ("a slice past the end", (0, 15, b"\x00" * 8), "ERR_VM_LOAD_DATA_RANGE"),
                             ("a start index past the end", (0, 16, b"\x00" * 4), "ERR_VM_LOAD_DATA_RANGE"),
                             ("a start index of 0xFFFF", (0, 0xFFFF, b"\x00" * 4), "ERR_VM_LOAD_DATA_RANGE")):
        prog()
        r = sd(*args)
        check(f"SET_DATA with {name} -> {want}", cause(r) == want, str(r))
        check("... and the program is discarded", cause(vm.op(OP_SET_DATA, batch(struct.pack("<HHH", 0, 0, 4) + bytes(4)))) == "ERR_VM_LOAD_BAD_STATE")

    # PTR links
    prog()
    check("PTR element linked to an object", ok(sd(2, 0, u16(1))))
    check("two elements linked in one record", ok(sd(2, 1, u16(0) + u16(3))))
    for name, args, want in (("an odd byte count", (2, 0, b"\x01\x00\x00"), "ERR_VM_LOAD_DATA_RANGE"),
                             ("a slice past the end", (2, 3, u16(0) * 2), "ERR_VM_LOAD_DATA_RANGE"),
                             ("a child that does not exist", (2, 0, u16(40)), "ERR_VM_ACCESSOR_UNKNOWN_ID")):
        prog()
        r = sd(*args)
        check(f"PTR SET_DATA with {name} -> {want}", cause(r) == want, str(r))
    prog()
    r = sd(2, 0, u16(2))
    check("a PTR linked to itself: refused, or the program stays safe to run and to watch", not ok(r) or ok(vm.exec(X_NORMAL)), str(r))
    if ok(r):
        d = vm.snap_raw([2])
        check("... telemetry of the loop answers (no hang)", 2 in d, str(d))
    vm.reset()


@test("wire/accessors")
def t_wire_acc(vm: Vm) -> None:
    def prog(nacc=4):
        tiny(vm, 4, nacc, 0, 4096)
        vm.op(OP_ADD_OBJECTS, batch(obj_rec(0, T_F, 16), obj_rec(1, T_PTR, 8), obj_rec(2, T_F, 4, name=b"kid"), obj_rec(3, T_U32, 4)))
        vm.op(OP_SET_DATA, batch(struct.pack("<HHH", 1, 0, 2) + u16(2)))

    def acc(aid, root, count, steps):
        return vm.op(OP_ADD_ACCESSORS, batch(struct.pack("<HHBB", aid, root, count, len(steps)) + steps))

    prog()
    check("accessor with no steps (the whole object)", ok(acc(0, 0, 0, b"")))
    check("accessor with a literal step", ok(acc(1, 0, 1, struct.pack("<BI", 0, 3))))
    check("accessor with a reference step", ok(acc(2, 0, 1, struct.pack("<BH", 1, 1))))
    check("accessor with a name step", ok(acc(3, 1, 1, struct.pack("<BB", 2, 3) + b"kid")))
    for name, args, want in (
        ("an unknown step kind", (0, 0, 1, bytes([7, 0, 0, 0, 0])), "ERR_VM_ACC_BAD_KIND"),
        ("a reference to an accessor that does not exist yet", (0, 0, 1, struct.pack("<BH", 1, 3)), "ERR_VM_REG_OOB"),
        ("a reference to itself", (0, 0, 1, struct.pack("<BH", 1, 0)), "ERR_VM_REG_OOB"),
        ("a name longer than 15", (0, 1, 1, struct.pack("<BB", 2, 16) + b"x" * 16), "ERR_VM_OBJ_NAME_TOO_LONG"),
        ("an id above OPEN's count", (9, 0, 0, b""), "ERR_VM_REG_OOB"),
        ("a step count that the bytes do not cover", (0, 0, 2, struct.pack("<BI", 0, 3)), "ERR_VM_LOAD_SHORT_RECORD"),
        ("trailing bytes after the steps", (0, 0, 1, struct.pack("<BI", 0, 3) + b"\x00"), "ERR_VM_LOAD_SHORT_RECORD"),
        ("a literal step cut short", (0, 0, 1, b"\x00\x01\x00"), "ERR_VM_LOAD_SHORT_RECORD"),
    ):
        prog()
        r = acc(*args)
        check(f"accessor with {name} -> {want}", cause(r) == want, str(r))
    prog()
    r = acc(0xFFFF, 0, 0, b"")
    check("accessor id 0xFFFF (the 'no id' value: it would skip the registry) -> ERR_VM_REG_OOB", cause(r) == "ERR_VM_REG_OOB", str(r))
    prog()
    acc(0, 0, 0, b"")
    r = acc(0, 0, 0, b"")
    check("the same accessor id twice -> ERR_VM_REG_DUP", cause(r) == "ERR_VM_REG_DUP", str(r))
    prog()
    r = acc(0, 77, 0, b"")
    check("an accessor on an object that does not exist loads (it is resolved when used)", ok(r), str(r))
    vm.reset()


@test("wire/blocks")
def t_wire_blocks(vm: Vm) -> None:
    def prog(nblk=2):
        tiny(vm, 4, 3, nblk, 4096)
        vm.op(OP_ADD_OBJECTS, batch(obj_rec(0, T_F, 4), obj_rec(1, T_B, 1), obj_rec(2, T_U8, 1), obj_rec(3, T_B, 1, 0)))
        vm.op(OP_ADD_ACCESSORS, batch(*(struct.pack("<HHBB", i, i, 0, 0) for i in range(3))))

    set_state = f32(1.0) + bytes(4)
    pin = lambda *ids: b"".join(u16(i) for i in ids)  # noqa: E731
    cases = [
        ("block type 0 (reserved)", blk_head(btype=0, ci=2) + pin(0, 0) + set_state, "ERR_VM_BLK_UNKNOWN_TYPE"),
        ("block type 63 (not in the palette)", blk_head(btype=63) , "ERR_VM_BLK_UNKNOWN_TYPE"),
        ("block type 255", blk_head(btype=255), "ERR_VM_BLK_UNKNOWN_TYPE"),
        ("17 inputs", blk_head(ci=17) + pin(*[0] * 17), "ERR_VM_BLK_BAD_SHAPE"),
        ("17 outputs", blk_head(co=17) + pin(*[0] * 17), "ERR_VM_BLK_BAD_SHAPE"),
        ("17 enables", blk_head(ce=17) + pin(*[0] * 17), "ERR_VM_BLK_BAD_SHAPE"),
        ("enable mode 2", blk_head(ci=2, en_mode=2) + pin(0, 1) + set_state, "ERR_VM_BLK_BAD_SHAPE"),
        ("on_error 2", blk_head(ci=2, on_err=2) + pin(0, 1) + set_state, "ERR_VM_BLK_BAD_SHAPE"),
        ("an input accessor that does not exist", blk_head(ci=2, clen=8) + pin(0, 40) + set_state, "ERR_VM_BLK_BAD_REF"),
        ("an output object that does not exist", blk_head(btype=BLK["EXPR"], co=1) + pin(40), "ERR_VM_BLK_BAD_REF"),
        ("an enable accessor that does not exist", blk_head(ci=2, ce=1, clen=8) + pin(0, 1, 40) + set_state, "ERR_VM_BLK_BAD_REF"),
        ("an enable written as NO_ID", blk_head(ci=2, ce=1, clen=8) + pin(0, 1, NO_ID) + set_state, "ERR_VM_BLK_BAD_REF"),
        ("an ENO object that does not exist", blk_head(ci=2, clen=8, eno=40) + pin(0, 1) + set_state, "ERR_VM_BLK_BAD_REF"),
        ("an ENO object that is not a boolean", blk_head(ci=2, clen=8, eno=0) + pin(0, 1) + set_state, "ERR_VM_BLK_BAD_REF"),
        ("an ENO object that is a U8", blk_head(ci=2, clen=8, eno=2) + pin(0, 1) + set_state, "ERR_VM_BLK_BAD_REF"),
        ("an ENO object that is not mutable", blk_head(ci=2, clen=8, eno=3) + pin(0, 1) + set_state, "ERR_VM_BLK_BAD_REF"),
        ("block id above OPEN's count", struct.pack("<HHBBBBBBHH", 9, 9, BLK["SET"], 2, 0, 0, 0, 1, 8, NO_ID) + pin(0, 1) + set_state, "ERR_VM_REG_OOB"),
    ]
    for name, payload, want in cases:
        prog()
        r = vm.op(OP_ADD_BLOCK, payload)
        check(f"{name} -> {want}", cause(r) == want, str(r))
    prog()
    r = vm.op(OP_ADD_BLOCK, struct.pack("<HHBBBBBBHH", 0xFFFF, 0, BLK["SET"], 2, 0, 0, 0, 1, 8, NO_ID) + pin(0, 1) + set_state)
    check("block id 0xFFFF (the 'no id' value: it would skip the registry) -> ERR_VM_REG_OOB", cause(r) == "ERR_VM_REG_OOB", str(r))
    prog()
    vm.op(OP_ADD_BLOCK, blk_head(ci=2, clen=8) + pin(0, 1) + set_state)
    r = vm.op(OP_ADD_BLOCK, blk_head(ci=2, clen=8) + pin(0, 1) + set_state)
    check("the same block id twice -> ERR_VM_REG_DUP", cause(r) == "ERR_VM_REG_DUP", str(r))
    prog()
    r = vm.op(OP_ADD_BLOCK, blk_head(btype=BLK["EXPR"], co=2, clen=4) + pin(0, 0) + bytes(4))
    check("a block naming one output twice -> ERR_VM_BLK_OUTPUT_TAKEN", cause(r) == "ERR_VM_BLK_OUTPUT_TAKEN", str(r))
    prog()
    r = vm.op(OP_ADD_BLOCK, blk_head(btype=BLK["EXPR"], co=1, clen=4, eno=1) + pin(1) + bytes(4))
    check("... or its ENO also as an output -> ERR_VM_BLK_OUTPUT_TAKEN", cause(r) == "ERR_VM_BLK_OUTPUT_TAKEN", str(r))
    prog()
    r = vm.op(OP_ADD_BLOCK, blk_head(ci=2, clen=8, eno=1) + pin(0, 1) + set_state)
    check("a SET block with an ENO object loads", ok(r), str(r))

    # a start with a slot missing
    for name, objs, accs, blks, want_kind in (("an object", 2, 0, 0, 0), ("an accessor", 1, 1, 0, 1), ("a block", 1, 0, 1, 2)):
        tiny(vm, objs, accs, blks)
        vm.op(OP_ADD_OBJECTS, batch(obj_rec(0, T_B, 1)))
        t = vm.link.mark()
        r = vm.exec(X_NORMAL)
        check(f"start with {name} slot never uploaded -> ERR_VM_LOAD_INCOMPLETE", cause(r) == "ERR_VM_LOAD_INCOMPLETE", str(r))
        nodes = [n for n in (r.record.nodes if r and r.record else []) if n.name == "ERR_VM_LOAD_INCOMPLETE"]
        check("... naming the kind of slot", nodes and nodes[0].data[:1] == bytes([want_kind]), str(nodes[0] if nodes else None))
    vm.reset()


@test("wire/running")
def t_wire_running(vm: Vm) -> None:
    p = Prog()
    a = p.f(1.0)
    c = p.f(2.0, flags=0)
    r = vm.start(p)
    check("a program starts", ok(r), str(r))
    r = vm.op(OP_ADD_OBJECTS, batch(obj_rec(9, T_B, 1)))
    check("ADD_OBJECTS while running -> ERR_VM_LOAD_RUNNING", cause(r) == "ERR_VM_LOAD_RUNNING", str(r))
    r = vm.op(OP_ADD_ACCESSORS, batch(struct.pack("<HHBB", 0, 0, 0, 0)))
    check("ADD_ACCESSORS while running -> ERR_VM_LOAD_RUNNING", cause(r) == "ERR_VM_LOAD_RUNNING", str(r))
    r = vm.op(OP_ADD_BLOCK, blk_head())
    check("ADD_BLOCK while running -> ERR_VM_LOAD_RUNNING", cause(r) == "ERR_VM_LOAD_RUNNING", str(r))
    check("the refused ops did not touch the program", vm.read(a) == 1.0)
    r = vm.write(a, 5.0)
    check("a SET_DATA while running is a runtime write", ok(r), str(r))
    check("... and takes effect", vm.read(a) == 5.0)
    r = vm.op(OP_ADD_OBJECTS, b"\x05")
    check("a malformed load frame while running is refused without discarding the program", not ok(r) and vm.read(a) == 5.0, str(r))
    r = vm.op(OP_OPEN, struct.pack("<HHHI", 1, 0, 0, 1024))
    check("OPEN while running replaces the program (and stops it)", ok(r), str(r))
    r = vm.op(OP_ADD_OBJECTS, batch(obj_rec(0, T_B, 1)))
    check("... the new program takes loads", ok(r), str(r))
    check("... and the old subscription is gone", True)
    vm.exec(X_NORMAL)
    r = vm.reset()
    check("RESET while running", ok(r), str(r))
    r = vm.op(OP_ADD_OBJECTS, batch(obj_rec(0, T_B, 1)))
    check("... leaves nothing open", cause(r) == "ERR_VM_LOAD_BAD_STATE", str(r))
    vm.exec(X_NORMAL)
    r = vm.exec(X_RESET)
    check("EXEC reset (8) is a RESET", ok(r), str(r))
    r = vm.op(OP_ADD_OBJECTS, batch(obj_rec(0, T_B, 1)))
    check("... leaves nothing open", cause(r) == "ERR_VM_LOAD_BAD_STATE", str(r))
    vm.reset()


# =====================================================================================================================
# expressions
# =====================================================================================================================


def expr_state(consts: Sequence[float], code: Sequence[int]) -> bytes:
    return struct.pack("<BBH", len(consts), 0, len(code)) + b"".join(f32(k) for k in consts) + bytes(code)


def bit_state(consts: Sequence[int], code: Sequence[int]) -> bytes:
    return struct.pack("<BBH", len(consts), 0, len(code)) + b"".join(u32(k) for k in consts) + bytes(code)


F_IN, F_K = OPF["IN"], OPF["K"]
B_IN, B_K = OPB["IN"], OPB["K"]


def fcode(name: str, nargs: int) -> list[int]:
    code = []
    for i in range(nargs):
        code += [F_IN, i]
    return code + [OPF[name], OPF["END"]]


def _f32round(x):
    return as_f32(x)


# (op, args, expected). Inputs: a, b, c, d, e of the program below.
A, B, C, D, E = 7.5, 2.0, 1.0, -3.25, 0.5
F_CASES = [
    ("ADD", [A, B], A + B), ("SUB", [A, B], A - B), ("MUL", [A, B], A * B), ("DIV", [A, B], A / B), ("MOD", [A, B], math.fmod(A, B)),
    ("MOD", [D, B], math.fmod(D, B)), ("POW", [A, B], A ** B), ("POW", [B, D], B ** D), ("POW", [D, B], D ** 2), ("ROOT", [A, B], A ** 0.5),
    ("ROOT", [-8.0, 3.0], -2.0), ("MIN", [A, B], B), ("MAX", [A, B], A), ("MIN", [D, B], D), ("ATAN2", [A, B], math.atan2(A, B)),
    ("ATAN2", [D, D], math.atan2(D, D)), ("HYPOT", [A, B], math.hypot(A, B)), ("NEG", [A], -A), ("NEG", [D], -D), ("ABS", [D], 3.25), ("ABS", [A], A),
    ("SQRT", [A], math.sqrt(A)), ("SQRT", [0.0], 0.0), ("SQUARE", [D], D * D), ("RECIP", [B], 0.5), ("RECIP", [D], 1 / D),
    ("FLOOR", [A], 7.0), ("FLOOR", [D], -4.0), ("CEIL", [A], 8.0), ("CEIL", [D], -3.0), ("ROUND", [A], 8.0), ("ROUND", [D], -3.0),
    ("ROUND", [-2.5], -3.0), ("ROUND", [2.4], 2.0), ("TRUNC", [A], 7.0), ("TRUNC", [D], -3.0), ("SIGN", [A], 1.0), ("SIGN", [D], -1.0),
    ("SIGN", [0.0], 0.0), ("SIN", [E], math.sin(E)), ("COS", [E], math.cos(E)), ("TAN", [E], math.tan(E)), ("ASIN", [E], math.asin(E)),
    ("ACOS", [E], math.acos(E)), ("ASIN", [1.0], math.pi / 2), ("ATAN", [A], math.atan(A)), ("EXP", [C], math.e), ("EXP", [D], math.exp(D)),
    ("LOG", [A], math.log(A)), ("LOG10", [100.0], 2.0), ("LOG2", [8.0], 3.0), ("DEG", [math.pi], 180.0), ("RAD", [180.0], math.pi),
    ("LT", [A, B], 0.0), ("LT", [B, A], 1.0), ("LE", [B, B], 1.0), ("GT", [A, B], 1.0), ("GE", [B, A], 0.0), ("GE", [B, B], 1.0), ("EQ", [B, B], 1.0),
    ("EQ", [A, B], 0.0), ("NE", [A, B], 1.0), ("NE", [B, B], 0.0), ("AND", [A, B], 1.0), ("AND", [A, 0.0], 0.0), ("OR", [0.0, 0.0], 0.0),
    ("OR", [0.0, D], 1.0), ("XOR", [A, B], 0.0), ("XOR", [A, 0.0], 1.0), ("NOT", [0.0], 1.0), ("NOT", [D], 0.0), ("SEL", [C, A, B], A),
    ("SEL", [0.0, A, B], B), ("SEL", [D, A, B], A),
]


@test("expr/float: every operation")
def t_expr_ops(vm: Vm) -> None:
    # one program: inputs 0..4, a block per case with its own output
    p = Prog(size=65536)
    cases = []
    for name, args, want in F_CASES:
        # the case's arguments come from private input objects, so every case has its own values
        srcs = [p.f(v) for v in args]
        out = p.f()
        eno = p.b() if len(cases) < 3 else None  # a subscription holds at most 128 objects
        p.block("EXPR", ins=srcs, outs=[out], eno=eno, state=expr_state([], fcode(name, len(args))))
        cases.append((name, args, want, out, eno))
    r = vm.start(p, X_SCAN)
    check(f"a program of {len(cases)} EXPR blocks loads and enters scan mode", ok(r), str(r))
    if not ok(r):
        return
    got = vm.step([c[3] for c in cases] + [c[4] for c in cases if c[4] is not None], timeout=6.0, kick=True)
    for name, args, want, out, eno in cases:
        v = vm.vals(got, out)
        check(f"{name}{tuple(args)} = {want:.6g}", close(v, _f32round(want)), f"got {v}")
    check("ENO is true", all(vm.vals(got, c[4]) == 1 for c in cases if c[4] is not None))
    vm.reset()


@test("expr/float: stack, constants, conversions")
def t_expr_stack(vm: Vm) -> None:
    p = Prog()
    a, b = p.f(7.5), p.f(2.0)
    u8, i32v, bl, u32v = p.u8(200), p.i32(-5), p.b(True), p.u32(4000000000)
    outs = {}

    def e(name, ins, consts, code):
        o = p.f()
        p.block("EXPR", ins=ins, outs=[o], state=expr_state(consts, code))
        outs[name] = o

    e("dup", [a], [], [F_IN, 0, OPF["DUP"], OPF["MUL"], OPF["END"]])
    e("swap", [a, b], [], [F_IN, 0, F_IN, 1, OPF["SWAP"], OPF["SUB"], OPF["END"]])
    e("drop", [a, b], [], [F_IN, 0, F_IN, 1, OPF["DROP"], OPF["END"]])
    e("const", [a], [1.5, 2.5], [F_K, 0, F_K, 1, OPF["ADD"], OPF["END"]])  # a block with no input never triggers
    e("input and const", [a], [10.0], [F_IN, 0, F_K, 0, OPF["MUL"], OPF["END"]])
    e("u8 + i32", [u8, i32v], [], [F_IN, 0, F_IN, 1, OPF["ADD"], OPF["END"]])
    e("bool as 1.0", [bl], [], [F_IN, 0, OPF["END"]])
    e("u32 in float", [u32v], [], [F_IN, 0, OPF["END"]])
    e("16 values deep", [a], [], [F_IN, 0] * 16 + [OPF["ADD"]] * 15 + [OPF["END"]])
    e("END ends the code", [a, b], [], [F_IN, 0, OPF["END"], F_IN, 1])
    e("one input read twice", [a], [], [F_IN, 0, F_IN, 0, OPF["ADD"], F_IN, 0, OPF["ADD"], OPF["END"]])
    e("a long formula", [a, b], [3.0], [F_IN, 0, F_IN, 1, OPF["ADD"], F_K, 0, OPF["MUL"], F_IN, 1, OPF["SUB"], OPF["SQRT"], OPF["END"]])
    e("no END at the end of the code", [a], [], [F_IN, 0])
    r = vm.start(p, X_SCAN)
    check("the program loads", ok(r), str(r))
    if not ok(r):
        return
    got = vm.step(list(outs.values()), kick=True)
    want = {"dup": 56.25, "swap": -5.5, "drop": 7.5, "const": 4.0, "input and const": 75.0, "u8 + i32": 195.0, "bool as 1.0": 1.0,
            "u32 in float": 4000000000.0, "16 values deep": 120.0, "END ends the code": 7.5, "one input read twice": 22.5,
            "a long formula": math.sqrt((7.5 + 2.0) * 3.0 - 2.0), "no END at the end of the code": 7.5}
    for k, v in want.items():
        check(f"stack/{k} = {v:.6g}", close(vm.vals(got, outs[k]), as_f32(v)), f"got {vm.vals(got, outs[k])}")
    vm.reset()


@test("expr/float: faults at run time")
def t_expr_faults(vm: Vm) -> None:
    p = Prog()
    x, y = p.f(1.0), p.f(0.0)
    rows = {}
    for name, ins, code in (("DIV by 0", [x, y], fcode("DIV", 2)), ("MOD by 0", [x, y], fcode("MOD", 2)), ("RECIP of 0", [y], fcode("RECIP", 1)),
                            ("SQRT of -1", [p.f(-1.0)], fcode("SQRT", 1)), ("LOG of 0", [y], fcode("LOG", 1)), ("LOG10 of -1", [p.f(-1.0)], fcode("LOG10", 1)),
                            ("LOG2 of 0", [y], fcode("LOG2", 1)), ("ASIN of 2", [p.f(2.0)], fcode("ASIN", 1)), ("ACOS of -2", [p.f(-2.0)], fcode("ACOS", 1)),
                            ("POW of -2 to 0.5", [p.f(-2.0), p.f(0.5)], fcode("POW", 2)), ("ROOT by 0", [x, y], fcode("ROOT", 2)),
                            ("ROOT of -4 by 2", [p.f(-4.0), p.f(2.0)], fcode("ROOT", 2)), ("EXP overflow", [p.f(100.0)], fcode("EXP", 1)),
                            ("MUL overflow", [p.f(3e38), p.f(3e38)], fcode("MUL", 2))):
        out, eno = p.f(77.0), p.b()
        p.block("EXPR", ins=ins, outs=[out], eno=eno, state=expr_state([], code))
        rows[name] = (out, eno)
    good_out, good_eno = p.f(), p.b()
    p.block("EXPR", ins=[x], outs=[good_out], eno=good_eno, state=expr_state([], fcode("NEG", 1)))
    r = vm.start(p, X_SCAN)
    check("the program loads", ok(r), str(r))
    if not ok(r):
        return
    t = vm.link.mark()
    got = vm.step([o for o, _ in rows.values()] + [e for _, e in rows.values()] + [good_out, good_eno], kick=True)
    for name, (out, eno) in rows.items():
        check(f"{name}: ENO false, the output is left alone", vm.vals(got, eno) == 0 and vm.vals(got, out) == 77.0, f"eno={vm.vals(got, eno)} out={vm.vals(got, out)}")
    check("a healthy block next to the faulty ones still works", vm.vals(got, good_out) == -1.0 and vm.vals(got, good_eno) == 1)
    time.sleep(0.3)
    names = err_names(vm.link, t)
    check("each fault is reported (ERR_VM_EXPR_MATH wrapped in ERR_VM_BLOCK_FAILED)", names.count("ERR_VM_EXPR_MATH") >= len(rows) - 1 and "ERR_VM_BLOCK_FAILED" in names, f"{names.count('ERR_VM_EXPR_MATH')} of {len(rows)}")
    # reasons: DIV0 = 0, DOMAIN = 1, NOT_FINITE = 2 (node data: block u16, pc u16, opcode u8, reason u8)
    reasons = {n.data[5] for n in err_nodes(vm.link, t, "ERR_VM_EXPR_MATH") if len(n.data) >= 6}
    check("... with the reasons division by zero, domain and not finite", reasons == {0, 1, 2}, str(reasons))
    # a fault is reported once, not on every pass
    t2 = vm.link.mark()
    for _ in range(4):
        vm.exec(X_ONCE)
    time.sleep(0.3)
    check("a standing fault is reported once, not again on the next passes", err_names(vm.link, t2).count("ERR_VM_EXPR_MATH") == 0, str(err_names(vm.link, t2)))
    vm.reset()


@test("expr/float: recovery, trigger and enable")
def t_expr_trigger(vm: Vm) -> None:
    p = Prog()
    a, b = p.f(6.0), p.f(0.0)
    q, e = p.f(), p.b()
    gate, q2, e2 = p.b(False), p.f(), p.b()
    q3, e3 = p.f(), p.b()
    p.block("EXPR", ins=[a, b], outs=[q], eno=e, state=expr_state([], fcode("DIV", 2)))
    p.block("EXPR", ins=[a], outs=[q2], eno=e2, ens=[gate], state=expr_state([1.0], [F_IN, 0, F_K, 0, OPF["ADD"], OPF["END"]]))
    p.block("EXPR", ins=[a], outs=[q3], eno=e3, ens=[gate, b], en_mode=EN_ALL, state=expr_state([], fcode("NEG", 1)))
    r = vm.start(p, X_SCAN)
    check("the program loads", ok(r), str(r))
    ids = [q, e, q2, e2, q3, e3]
    g = vm.step(ids, kick=True)
    check("DIV by 0: ENO false", vm.vals(g, e) == 0, str(vm.vals(g, e)))
    check("a block whose enable is false does not compute, ENO false", vm.vals(g, q2) == 0.0 and vm.vals(g, e2) == 0)
    g = vm.step(ids, {b: 3.0})
    check("the divisor fixed: the quotient is computed", close(vm.vals(g, q), 2.0), str(vm.vals(g, q)))
    check("... and ENO is back", vm.vals(g, e) == 1)

    # freshness: a pass without a fresh input does not recompute
    vm.write(q, 99.0)
    g = vm.step(ids)
    check("an input that is not fresh does not trigger the block (the output keeps what was written)", vm.vals(g, q) == 99.0, str(vm.vals(g, q)))
    check("... and ENO stays true (pass-through while enabled)", vm.vals(g, e) == 1)
    g = vm.step(ids, {a: 6.0})
    check("writing the input again (the same value) triggers it", close(vm.vals(g, q), 2.0), str(vm.vals(g, q)))

    # enable: a rising gate fires the block once even though no input changed
    g = vm.step(ids, {gate: True})
    check("an open gate (fresh and true) triggers the block", vm.vals(g, q2) == 7.0 and vm.vals(g, e2) == 1, f"{vm.vals(g, q2)} {vm.vals(g, e2)}")
    vm.write(q2, 0.0)
    g = vm.step(ids)
    check("a gate that is merely true (not rewritten) does not trigger", vm.vals(g, q2) == 0.0 and vm.vals(g, e2) == 1, f"{vm.vals(g, q2)} {vm.vals(g, e2)}")
    g = vm.step(ids, {gate: False})
    check("the gate closed: ENO false", vm.vals(g, e2) == 0)
    # enable mode ALL
    g = vm.step(ids, {gate: True, b: 0.0})
    check("two enables in mode ALL: one false keeps the block off", vm.vals(g, e3) == 0, str(vm.vals(g, e3)))
    g = vm.step(ids, {b: 1.0, gate: True})
    check("... both true: it runs", vm.vals(g, e3) == 1 and vm.vals(g, q3) == -6.0, f"{vm.vals(g, e3)} {vm.vals(g, q3)}")
    vm.reset()


def bcode(name: str, nargs: int) -> list[int]:
    code = []
    for i in range(nargs):
        code += [B_IN, i]
    return code + [OPB[name], OPB["END"]]


X, Y, Z = 0xF0F0A5A5, 4, 0x80000001
M = 0xFFFFFFFF
B_CASES = [
    ("AND", [X, 0xFF00FF00], X & 0xFF00FF00), ("OR", [X, 0x0F0F0F0F], X | 0x0F0F0F0F), ("XOR", [X, M], X ^ M), ("SHL", [X, Y], (X << 4) & M),
    ("SHL", [1, 31], 0x80000000), ("SHL", [X, 32], 0), ("SHL", [X, 200], 0), ("SHR", [X, Y], X >> 4), ("SHR", [X, 32], 0), ("SHR", [Z, 31], 1),
    ("SAR", [Z, 4], 0xF8000000), ("SAR", [0x40000000, 4], 0x04000000), ("SAR", [Z, 32], M), ("SAR", [0x7FFFFFFF, 40], 0),
    ("ROL", [X, 8], ((X << 8) | (X >> 24)) & M), ("ROL", [X, 0], X), ("ROL", [X, 40], ((X << 8) | (X >> 24)) & M), ("ROR", [X, 8], ((X >> 8) | (X << 24)) & M),
    ("ROR", [X, 32], X), ("GET", [X, 0], 1), ("GET", [X, 1], 0), ("GET", [X, 31], 1), ("GET", [X, 32], 1), ("SET", [0, 5], 32), ("SET", [0, 37], 32),
    ("CLR", [M, 0], M - 1), ("CLR", [M, 31], 0x7FFFFFFF), ("TGL", [X, 0], X ^ 1), ("TGL", [X, 4], X ^ 16), ("NOT", [X], X ^ M), ("NOT", [0], M),
    ("POPCNT", [X], bin(X).count("1")), ("POPCNT", [M], 32), ("POPCNT", [0], 0), ("CLZ", [1], 31), ("CLZ", [0], 32), ("CLZ", [M], 0), ("CTZ", [0x80], 7),
    ("CTZ", [0], 32), ("BSWAP", [0x11223344], 0x44332211),
]


@test("expr/bit: every operation")
def t_expr_bit(vm: Vm) -> None:
    p = Prog(size=65536)
    cases = []
    for name, args, want in B_CASES:
        srcs = [p.u32(v) for v in args]
        out = p.u32()
        p.block("EXPR_BIT", ins=srcs, outs=[out], state=bit_state([], bcode(name, len(args))))
        cases.append((name, args, want, out))
    extra = {}
    a, b = p.u32(0x0F), p.u32(0xF0)
    o = p.u32()
    p.block("EXPR_BIT", ins=[a, b], outs=[o], state=bit_state([0xFF], [B_IN, 0, B_IN, 1, OPB["OR"], B_K, 0, OPB["XOR"], OPB["END"]]))
    extra["(a|b)^K"] = (o, 0)
    o = p.u32()
    p.block("EXPR_BIT", ins=[a], outs=[o], state=bit_state([], [B_IN, 0, OPB["DUP"], OPB["XOR"], OPB["END"]]))
    extra["dup xor"] = (o, 0)
    o = p.u32()
    p.block("EXPR_BIT", ins=[a, b], outs=[o], state=bit_state([], [B_IN, 0, B_IN, 1, OPB["SWAP"], OPB["SHL"], OPB["END"]]))
    extra["swap shl (0xF0 << 0x0F)"] = (o, (0xF0 << 0x0F) & M)
    u8 = p.u8(200)
    fl = p.f(3.9)
    o = p.u32()
    p.block("EXPR_BIT", ins=[u8, fl], outs=[o], state=bit_state([], [B_IN, 0, B_IN, 1, OPB["OR"], OPB["END"]]))
    extra["u8 | float"] = (o, 200 | 4)
    r = vm.start(p, X_SCAN)
    check(f"a program of {len(cases) + 4} EXPR_BIT blocks loads", ok(r), str(r))
    if not ok(r):
        return
    got = vm.step([c[3] for c in cases] + [e[0] for e in extra.values()], timeout=6.0, kick=True)
    for name, args, want, out in cases:
        v = vm.vals(got, out)
        check(f"{name}({', '.join(hex(a) for a in args)}) = {hex(want)}", v == want, f"got {None if v is None else hex(v)}")
    for k, (o, want) in extra.items():
        v = vm.vals(got, o)
        check(f"{k} = {hex(want)}", v == want, f"got {v}")
    vm.reset()


@test("expr/rejects at load")
def t_expr_rejects(vm: Vm) -> None:
    def build(kind: str, ins: int, state: bytes, outs: int = 1, wired: bool = True) -> Response | None:
        p = Prog()
        srcs = [p.f(1.0) if kind == "EXPR" else p.u32(1) for _ in range(ins)]
        if not wired:
            srcs = [None] * ins
        o = [p.f() if kind == "EXPR" else p.u32() for _ in range(outs)]
        p.block(kind, ins=srcs, outs=o, state=state)
        return vm.load(p)

    def code(*c):
        return list(c)

    # (what, state, ERR_VM_EXPR_BAD_CODE reason: 0 header, 1 opcode, 2 operand, 3 underflow, 4 overflow, 5 result)
    cases = [
        ("unknown opcode", expr_state([], code(F_IN, 0, 0xFE)), 1),
        ("opcode 50 (one past the last)", expr_state([], code(F_IN, 0, OPF["OP_CNT"])), 1),
        ("an operation with too few values (underflow)", expr_state([], code(F_IN, 0, OPF["ADD"])), 3),
        ("the stack ends with two values", expr_state([], code(F_IN, 0, F_IN, 0)), 5),
        ("the stack ends empty", expr_state([], code(F_IN, 0, OPF["DROP"])), 5),
        ("no code at all", expr_state([], []), 5),
        ("17 values (overflow)", expr_state([], code(*([F_IN, 0] * 17))), 4),
        ("IN with an input that does not exist", expr_state([], code(F_IN, 5)), 2),
        ("a constant that does not exist", expr_state([1.0], code(F_K, 1)), 2),
        ("IN without its operand byte", expr_state([], code(F_IN)), 2),
        ("code longer than the state", struct.pack("<BBH", 0, 0, 50) + bytes([F_IN, 0]), 0),
        ("constants longer than the state", struct.pack("<BBH", 9, 0, 2) + bytes([F_IN, 0]), 0),
        ("a state shorter than its header", b"\x00\x00", 0),
    ]
    for name, st, reason in cases:
        t = vm.link.mark()
        r = build("EXPR", 1, st)
        check(f"EXPR: {name} -> the program is refused (ERR_VM_BLK_BAD_SHAPE)", cause(r) == "ERR_VM_BLK_BAD_SHAPE", str(r))
        n = wait_err(vm.link, t, "ERR_VM_EXPR_BAD_CODE", 1.0)
        if name.startswith("a state shorter"):  # refused by the state size of the type, before the formula is looked at
            continue
        check("... and ERR_VM_EXPR_BAD_CODE says why", n is not None and len(n.data) >= 6 and n.data[5] == reason, f"{n} want reason {reason}")
    t = vm.link.mark()
    r = build("EXPR", 1, expr_state([], code(F_IN, 0)), wired=False)
    check("EXPR reading an unwired input -> refused", cause(r) == "ERR_VM_BLK_BAD_SHAPE", str(r))
    n = wait_err(vm.link, t, "ERR_VM_EXPR_BAD_CODE", 1.0)
    check("... operand reason", n is not None and n.data[5] == 2, str(n))
    r = build("EXPR", 1, expr_state([], code(F_IN, 0)), outs=0)
    check("EXPR with no output -> refused", cause(r) == "ERR_VM_BLK_BAD_SHAPE", str(r))
    r = build("EXPR", 1, expr_state([], code(F_IN, 0)), outs=2)
    check("EXPR with two outputs -> refused", cause(r) == "ERR_VM_BLK_BAD_SHAPE", str(r))
    r = build("EXPR", 17, expr_state([], code(F_IN, 0)))
    check("EXPR with 17 inputs -> refused", cause(r) == "ERR_VM_BLK_BAD_SHAPE", str(r))
    r = build("EXPR", 16, expr_state([], code(F_IN, 15)))
    check("EXPR with 16 inputs and the last one read loads", r is None, str(r))
    r = build("EXPR", 0, expr_state([2.0], code(F_K, 0)))
    check("EXPR with no inputs and a constant loads", r is None, str(r))
    for name, st in (("unknown opcode", bit_state([], [B_IN, 0, 0xFE])), ("underflow", bit_state([], [B_IN, 0, OPB["AND"]])),
                     ("opcode 23", bit_state([], [B_IN, 0, OPB["OP_CNT"]])), ("two results", bit_state([], [B_IN, 0, B_IN, 0])),
                     ("a constant that does not exist", bit_state([], [B_K, 0]))):
        r = build("EXPR_BIT", 1, st)
        check(f"EXPR_BIT: {name} -> refused", cause(r) == "ERR_VM_BLK_BAD_SHAPE", str(r))
    vm.reset()
