"""VM suite, part 3: the supervisor (run modes), runtime writes, telemetry, retained values, limits and load. Run through vm_suite.py."""

from __future__ import annotations

import struct
import time

from router_link import KEY_SYS, STREAM_TELEMETRY
from vm_lib import *  # noqa: F401,F403
from vm_t_blocks import loaded, set_state  # noqa: F401
from vm_t_core import F_IN, F_K, OPF, batch, blk_head, expr_state, obj_rec, tiny  # noqa: F401


def counter_prog(n: int = 1) -> tuple[Prog, list[int]]:
    """n blocks, each adds 1 to its own counter on every pass. The counters are not resetable, so once written they stay
    fresh and the block runs on every pass; kick() starts them."""
    p = Prog()
    cs = [p.f(0.0, flags=F_MUT) for _ in range(n)]
    for c in cs:
        p.block("EXPR", ins=[c], outs=[c], state=expr_state([1.0], [F_IN, 0, F_K, 0, OPF["ADD"], OPF["END"]]))
    return p, cs


def read_at(vm: Vm, oid: int) -> tuple[float, float]:
    """(value, host time of the request): a reading and when it was taken. The subscription is dropped again, so a
    counter that changes on every pass does not flood the link."""
    t = time.time()
    v = vm.read(oid, timeout=3.0)
    vm.sub([])
    return v, t


def pass_rate(vm: Vm, oid: int, window: float = 0.6) -> float:
    a, ta = read_at(vm, oid)
    time.sleep(window)
    b, tb = read_at(vm, oid)
    if a is None or b is None:
        return -1.0
    return (b - a) / (tb - ta)


def quiet_for(vm: Vm, seconds: float, settle: float = 1.0) -> int:
    """Telemetry frames that arrive in a window of `seconds`, starting `settle` seconds from now (the backlog of an earlier
    flood is gone by then)."""
    time.sleep(settle)
    t = vm.link.mark()
    time.sleep(seconds)
    return len(tele_frames(vm.link, t))


def next_block(vm: Vm) -> Response | None:
    """NEXT, retried while the previous block is still being dispatched (the command is refused then)."""
    r = None
    for _ in range(40):
        r = vm.exec(X_NEXT)
        if ok(r):
            return r
        time.sleep(0.03)
    return r


@test("exec/run modes")
def t_modes(vm: Vm) -> None:
    p, (c,) = counter_prog()
    if not loaded(vm, p, X_NORMAL):
        return
    vm.kick()
    r1 = pass_rate(vm, c)
    check("normal mode: the program runs on its own (counter passes per second)", r1 >= 40, f"{r1:.0f}/s")

    # pause / resume: no pass, so no telemetry
    vm.sub([c])
    r = vm.exec(X_PAUSE)
    check("PAUSE", ok(r), str(r))
    n = quiet_for(vm, 0.8)
    check("paused: no pass runs, no telemetry is sent", n == 0, f"{n} frames")
    r = vm.exec(X_PAUSE)
    check("PAUSE again is accepted", ok(r), str(r))
    r = vm.exec(X_RESUME)
    check("RESUME", ok(r), str(r))
    time.sleep(0.5)
    t = vm.link.mark()
    time.sleep(0.4)
    check("resumed: the telemetry flows again", len(tele_frames(vm.link, t)) > 3)
    r = vm.exec(X_RESUME)
    check("RESUME when not paused is accepted (nothing to do)", ok(r), str(r))
    vm.sub([])

    # restart: to the start, frozen
    r = vm.exec(X_RESTART)
    check("RESET_TO_START", ok(r), str(r))
    vm.sub([c])
    n = quiet_for(vm, 0.8)
    check("... leaves the program frozen before its first block", n == 0, f"{n} frames")
    vm.exec(X_RESUME)
    time.sleep(0.5)
    t = vm.link.mark()
    time.sleep(0.4)
    check("... RESUME runs it again", len(tele_frames(vm.link, t)) > 3)
    vm.sub([])

    # speed
    r = vm.op(OP_SPEED, struct.pack("<H", 8))
    check("SPEED 8 (slow motion)", ok(r), str(r))
    r8 = pass_rate(vm, c)
    r = vm.op(OP_SPEED, struct.pack("<H", 1))
    check("SPEED 1 (normal)", ok(r), str(r))
    r1b = pass_rate(vm, c)
    check("slow motion stretches a pass: at factor 8 the program runs at less than half the speed", 0 <= r8 < 0.5 * r1b, f"{r8:.0f}/s vs {r1b:.0f}/s")
    for bad in (0,):
        r = vm.op(OP_SPEED, struct.pack("<H", bad))
        check(f"SPEED {bad} is refused", r is not None and not r.ok, str(r))
    r = vm.op(OP_SPEED, struct.pack("<H", 65535))
    check("SPEED 65535 is accepted or refused cleanly (no hang)", r is not None, str(r))
    vm.op(OP_SPEED, struct.pack("<H", 1))

    # command validity
    for cmd in (11, 12, 99, 255):
        r = vm.exec(cmd)
        check(f"EXEC {cmd} (not a command) -> ERR_VM_EXEC_CONTROL", cause(r) == "ERR_VM_EXEC_CONTROL", str(r))
    r = vm.exec(X_ONCE)
    check("ONCE in normal mode -> ERR_VM_EXEC_CONTROL", cause(r) == "ERR_VM_EXEC_CONTROL", str(r))
    r = vm.exec(X_NEXT)
    check("NEXT in normal mode -> ERR_VM_EXEC_CONTROL", cause(r) == "ERR_VM_EXEC_CONTROL", str(r))
    r = vm.exec(X_ACK)
    check("ACK_FAULT while the program runs -> ERR_VM_EXEC_CONTROL", cause(r) == "ERR_VM_EXEC_CONTROL", str(r))
    vm.reset()
    r = vm.exec(X_ACK)
    check("ACK_FAULT when stopped (nothing latched) is accepted", ok(r), str(r))
    vm.reset()


@test("exec/STATUS reads the run")
def t_status(vm: Vm) -> None:
    """VM STATUS (0x0A, a read) answers vm_wire_status_t: mode, load state, ready, the fault latch, slow-down, passes, arena, next block."""
    def status(what: str):
        r = vm.op(OP_STATUS)
        if not (ok(r) and len(r.data) == 26):
            check(f"STATUS answers 26 bytes ({what})", False, str(r))
            return None
        return struct.unpack("<BBBBHHIIIIH", r.data)

    vm.reset()
    s = status("empty")
    if s is None:
        return
    check("empty: stopped, nothing loaded, not ready", s[:3] == (0, 0, 0), str(s[:3]))
    p, (c,) = counter_prog()
    r = vm.load(p)  # None: every step answered OK
    check("load", r is None, str(r))
    s = status("loaded")
    check("loaded and complete, not started: stopped, open, ready", s is not None and s[:3] == (0, 1, 1), str(s and s[:3]))
    vm.exec(X_NORMAL)
    time.sleep(0.4)
    a = status("running")
    time.sleep(0.4)
    b = status("running later")
    check("running: mode RUNNING and the pass count grows", a is not None and b is not None and a[0] == 1 and b[7] > a[7], f"{a and a[7]} -> {b and b[7]}")
    check("the arena in use is reported", a is not None and a[9] > 0, str(a and a[9]))
    check("running: no next block (0xFFFF)", a is not None and a[10] == 0xFFFF, str(a and a[10]))
    vm.exec(X_PAUSE)
    time.sleep(0.2)
    s = status("paused")
    check("PAUSE: mode FROZEN", s is not None and s[0] == 2, str(s and s[0]))
    r = vm.link.request(bytes([KEY_SYS, 0x02]))  # sys STOP: the safe state
    time.sleep(0.3)
    s = status("safe state")
    check("sys STOP: stopped, the program kept (open, ready)", ok(r) and s is not None and s[:3] == (0, 1, 1), str(s and s[:3]))
    r = vm.link.request(bytes([KEY_SYS, 0x0A]))  # sys SAFE_LEAVE: every device resumed, the note cleared
    check("SAFE_LEAVE: out of the safe state", ok(r), str(r))
    vm.reset()


@test("exec/scan and block mode")
def t_stepping(vm: Vm) -> None:
    p, (c,) = counter_prog()
    if not loaded(vm, p, X_SCAN):
        return
    seq = []
    for i in range(4):
        g = vm.step([c], kick=(i == 0))
        seq.append(vm.vals(g, c))
    check("scan mode: every ONCE is exactly one pass (the counter counts 1, 2, 3, 4)", seq == [1.0, 2.0, 3.0, 4.0], str(seq))
    vm.sub([c])
    n = quiet_for(vm, 0.6, settle=0.3)
    check("... and between two ONCE nothing runs", n == 0, f"{n} frames")
    r = vm.exec(X_NEXT)
    check("NEXT in scan mode -> ERR_VM_EXEC_CONTROL", cause(r) == "ERR_VM_EXEC_CONTROL", str(r))
    r = vm.exec(X_NORMAL)
    check("scan -> normal", ok(r), str(r))
    time.sleep(0.4)
    check("... the program runs by itself again", pass_rate(vm, c) >= 40)
    r = vm.exec(X_SCAN)
    check("normal -> scan", ok(r), str(r))
    vm.reset()

    # block mode: NEXT runs one block
    p, cs = counter_prog(3)
    if not loaded(vm, p, X_BLOCK, "a program of three blocks in block mode"):
        return
    vm.kick()
    vm.sub(cs)
    n = quiet_for(vm, 0.5, settle=1.5)  # the backlog of the flood of the earlier test is gone by then
    check("block mode: nothing runs until NEXT", n == 0, f"{n} frames")
    r = vm.exec(X_ONCE)
    check("ONCE in block mode -> ERR_VM_EXEC_CONTROL", cause(r) == "ERR_VM_EXEC_CONTROL", str(r))
    def next_of() -> int | None:
        """The block STATUS says the VM is parked before (None: the answer is not there)."""
        r = vm.op(OP_STATUS)
        return struct.unpack("<BBBBHHIIIIH", r.data)[10] if ok(r) and len(r.data) == 26 else None

    time.sleep(0.2)
    check("block mode, no pass started: STATUS next_block is 0 (the first block)", next_of() == 0, str(next_of()))
    t = vm.link.mark()
    check("NEXT (1/3)", ok(next_block(vm)))
    got = vm.collect(t, cs, 2.0)
    check("... one block ran and its result is sent at once, the pass is not over: the first counter is 1, the others still 0",
          [vm.vals(got, x) for x in cs] == [1.0, 0.0, 0.0], str(got))
    check("... STATUS next_block is 1", next_of() == 1, str(next_of()))
    t = vm.link.mark()
    check("NEXT (2/3)", ok(next_block(vm)))
    got = vm.collect(t, [cs[1]], 2.0)
    check("... the second counter is 1 (only what changed is sent)", vm.vals(got, cs[1]) == 1.0 and cs[0] not in got, str(got))
    check("... STATUS next_block is 2", next_of() == 2, str(next_of()))
    t = vm.link.mark()
    check("NEXT (3/3)", ok(next_block(vm)))
    got = vm.collect(t, [cs[2]], 2.0)
    check("the last block ends the pass: the third counter is 1", vm.vals(got, cs[2]) == 1.0, str(got))
    time.sleep(0.2)
    check("... and between two passes the next block is the first one again (0)", next_of() == 0, str(next_of()))
    t = vm.link.mark()
    for _ in range(3):
        next_block(vm)
    got = vm.collect(t, cs, 2.0)
    check("a second round of three NEXT: 2, 2, 2", [vm.vals(got, x) for x in cs] == [2.0, 2.0, 2.0], str(got))
    r = vm.exec(X_NORMAL)
    check("block -> normal", ok(r), str(r))
    vm.reset()


@test("exec/stepping moves program time")
def t_step_time(vm: Vm) -> None:
    """Between two requests no time passes for the program, so a stepped timing block would never fire: each pass asked for adds
    SPEED's step_ms of program time instead."""
    p = Prog()
    q = p.b()
    p.block("PERIODIC", eno=q, state=struct.pack("<IBBHQ", 100, 0, 0, 0, 0))  # a tick every 100 ms of program time
    if not loaded(vm, p, X_SCAN):
        return
    r = vm.op(OP_SPEED, struct.pack("<HH", 1, 0))
    check("SPEED with the factor and a step of 0 ms", ok(r), str(r))
    got = [bool(vm.vals(vm.step([q], kick=(i == 0)), q)) for i in range(4)]
    check("step 0: time stands still, the periodic block ticks on the first pass only", got == [True, False, False, False], str(got))
    r = vm.op(OP_SPEED, struct.pack("<HH", 1, 100))
    check("SPEED with a step of 100 ms", ok(r), str(r))
    got = [bool(vm.vals(vm.step([q]), q)) for _ in range(4)]
    check("step 100 ms: every pass asked for is one period, the block ticks on every one", got == [True] * 4, str(got))
    r = vm.op(OP_SPEED, struct.pack("<HH", 1, 60001))
    check("a step past 60000 ms is refused", cause(r) == "ERR_VM_BAD_ARG", str(r))
    r = vm.op(OP_SPEED, struct.pack("<H", 1))
    check("the factor alone (2 bytes) is still taken", ok(r), str(r))
    vm.op(OP_SPEED, struct.pack("<HH", 1, 10))
    vm.reset()


# =====================================================================================================================
# runtime writes
# =====================================================================================================================


@test("write/runtime DATA")
def t_writes(vm: Vm) -> None:
    p = Prog()
    f, i32v, u32v, bl = p.f(0.0), p.i32(0), p.u32(0), p.b(False)
    arr = p.obj(T_U8, 300)
    farr = p.obj(T_F, 4, init=[1.0, 2.0, 3.0, 4.0])
    const = p.f(5.0, flags=0)
    prot = p.f(6.0, flags=F_MUT | F_PROT)
    ptr = p.ptr(1)
    eno = p.b()
    q = p.f()
    p.block("EXPR", ins=[f], outs=[q], eno=eno, state=expr_state([], [F_IN, 0, OPF["END"]]))
    if not loaded(vm, p, X_SCAN):
        return
    ids = [f, i32v, u32v, bl, arr, farr, const, prot, eno]
    g = vm.step(ids, {f: 1.5, i32v: -9, u32v: 4000000000, bl: True})
    check("one frame, four objects of four types", (vm.vals(g, f), vm.vals(g, i32v), vm.vals(g, u32v), vm.vals(g, bl)) == (1.5, -9, 4000000000, 1))
    vm.write(arr, bytes(range(1, 21)), 100)
    g = vm.step(ids)
    a = g[arr]
    check("a slice from the middle of an array (start 100, 20 bytes)", a[100:120] == bytes(range(1, 21)) and a[:100] == bytes(100) and a[120:] == bytes(180), "slice wrong")
    vm.write(farr, [9.0], 3)
    g = vm.step(ids)
    check("one element at the end (start 3)", vm.vals(g, farr) == [1.0, 2.0, 3.0, 9.0], str(vm.vals(g, farr)))
    vm.write_raw(vm.data_rec(f, f32(10.0)), vm.data_rec(f, f32(20.0)))
    g = vm.step(ids)
    check("two records for one object in one frame: the later wins", vm.vals(g, f) == 20.0, str(vm.vals(g, f)))
    vm.write(f, 1.0)
    vm.write(f, 2.0)
    vm.write(f, 3.0)
    g = vm.step(ids)
    check("three writes in a row: the pass sees the last", vm.vals(g, f) == 3.0, str(vm.vals(g, f)))

    # what is refused (and says so at once)
    t = vm.link.mark()
    cases = [("an object that is not mutable", const, f32(1.0), 0, "ERR_VM_OBJ_NOT_MUTABLE"),
             ("an object protected from the user (the ENO of a block)", eno, b"\x01", 0, "ERR_VM_OBJ_USR_PROTECTED"),
             ("an object protected from the user (the upload flag)", prot, f32(1.0), 0, "ERR_VM_OBJ_USR_PROTECTED"),
             ("a pointer", ptr, u16(0), 0, "ERR_VM_OVERRIDE_PTR_UNSUPPORTED"),
             ("an object that does not exist", 99, b"\x00", 0, "ERR_VM_ACCESSOR_UNKNOWN_ID"),
             ("a length that is not a multiple of the width", f, b"\x00\x00\x00", 0, "ERR_VM_LOAD_DATA_RANGE"),
             ("a slice past the end", farr, f32(1.0) * 2, 3, "ERR_VM_LOAD_DATA_RANGE"),
             ("a start index past the end", farr, f32(1.0), 4, "ERR_VM_LOAD_DATA_RANGE")]
    for name, oid, raw, start, want in cases:
        r = vm.write_raw(vm.data_rec(oid, raw, start))
        check(f"a write to {name} -> {want}", cause(r) == want, str(r))
    g = vm.step(ids)
    check("none of the refused writes changed anything or stopped the program", vm.vals(g, f) == 3.0 and vm.vals(g, const) == 5.0 and vm.vals(g, prot) == 6.0)
    r = vm.write_raw(vm.data_rec(f, f32(77.0)), vm.data_rec(const, f32(1.0)))
    check("a frame whose second record is refused -> the error", cause(r) == "ERR_VM_OBJ_NOT_MUTABLE", str(r))
    g = vm.step(ids)
    check("... the record before it was already queued and applies", vm.vals(g, f) == 77.0, str(vm.vals(g, f)))
    vm.reset()

    # the ring behind runtime writes holds what arrives while no pass runs
    p, (c,) = counter_prog()
    arr = p.obj(T_U8, 300)
    if not loaded(vm, p, X_NORMAL, "a program for the override ring"):
        return
    vm.exec(X_PAUSE)
    time.sleep(0.3)
    accepted, refused = [], []
    for i in range(1, 31):
        r = vm.write_raw(vm.data_rec(arr, bytes([i]) * 100))
        (accepted if ok(r) else refused).append((i, r))
    check("writes pile up while the program is paused, until the ring (CONFIG_VM_OVERRIDE_BUF_SIZE) is full", 3 <= len(accepted) < 30, f"{len(accepted)} accepted")
    check("... then a write is refused with ERR_VM_ALLOC_EXHAUSTED", refused and cause(refused[0][1]) == "ERR_VM_ALLOC_EXHAUSTED", str(refused[0][1] if refused else None))
    vm.exec(X_RESUME)
    time.sleep(0.5)
    a = vm.snap([arr])[arr]
    last = accepted[-1][0]
    check("after the resume the ring is drained in order: the array holds the last write that was accepted", a[:100] == bytes([last]) * 100, f"first byte {a[0]}, last accepted {last}")
    r = vm.write(arr, bytes([200]) * 10)
    time.sleep(0.3)
    check("... and writes are accepted again", ok(r) and vm.snap([arr])[arr][:10] == bytes([200]) * 10)
    vm.reset()


# =====================================================================================================================
# telemetry
# =====================================================================================================================


@test("telemetry/subscriptions")
def t_telemetry(vm: Vm) -> None:
    p = Prog(size=65536)
    objs = [p.u8(i) for i in range(130)]
    big = p.obj(T_U8, 1000, init=bytes((i * 7) & 0xFF for i in range(1000)))
    fl = p.f(1.5, name="fl")
    a, b = p.f(1.0), p.u32(2)
    tree = p.ptr(2, [a, b])
    if not loaded(vm, p, X_NORMAL):
        return
    t = vm.link.mark()
    r = vm.sub([objs[0], objs[1], fl])
    check("SUBSCRIBE", ok(r), str(r))
    got = vm.collect(t, [objs[0], objs[1], fl])
    check("a new subscription sends every object once", (vm.vals(got, objs[0]), vm.vals(got, objs[1]), vm.vals(got, fl)) == (0, 1, 1.5), str(got))
    n = quiet_for(vm, 0.6, settle=0.2)
    check("... and then nothing while nothing changes", n == 0, f"{n} frames")
    t = vm.link.mark()
    vm.write(objs[1], 99)
    got = vm.collect(t, [objs[1]])
    frames = [parse_frame(f) for _, f in tele_frames(vm.link, t)]
    ids_sent = {r[1] for fr in frames if fr for r in fr if r[0] == "val"}
    check("a change sends only the object that changed", ids_sent == {objs[1]} and vm.vals(got, objs[1]) == 99, str(ids_sent))
    t = vm.link.mark()
    vm.write(objs[1], 99)
    time.sleep(0.4)
    check("writing the same value again sends nothing (the value is compared, not the write)", len(tele_frames(vm.link, t)) == 0)
    t = vm.link.mark()
    vm.sub([objs[0], objs[1], fl])
    got = vm.collect(t, [objs[0], objs[1], fl])
    check("subscribing again sends everything again", vm.vals(got, objs[1]) == 99 and vm.vals(got, objs[0]) == 0)
    r = vm.sub([])
    check("SUBSCRIBE with no ids clears the subscription", ok(r), str(r))
    t = vm.link.mark()
    vm.write(objs[1], 5)
    time.sleep(0.5)
    check("... nothing is sent any more", len(tele_frames(vm.link, t)) == 0)

    # unknown ids, the limit
    t = vm.link.mark()
    r = vm.sub([4000, objs[2]])
    got = vm.collect(t, [objs[2]])
    check("an id that is not an object is ignored, the others are sent", ok(r) and vm.vals(got, objs[2]) == 2, str(r))
    t = vm.link.mark()
    r = vm.sub(objs[:128])
    got = vm.collect(t, objs[:128], 4.0)
    now = {o: (5 if o == objs[1] else i) for i, o in enumerate(objs[:128])}  # objs[1] was written above
    check("128 ids (the limit) are accepted and all arrive", ok(r) and all(vm.vals(got, o) == v for o, v in now.items()), f"{len(got)} of 128")
    frames = [f for _, f in tele_frames(vm.link, t)]
    check("... in frames that are well formed and no longer than CONFIG_VM_SUB_MAX_FRAME_LEN (512)", frames and all(len(f) <= 512 and parse_frame(f) is not None for f in frames), f"{len(frames)} frames, longest {max(map(len, frames), default=0)}")
    r = vm.sub(objs[:129])
    check("129 ids -> ERR_VM_RANGE", cause(r) == "ERR_VM_RANGE", str(r))
    t = vm.link.mark()
    vm.write(objs[7], 250)
    got = vm.collect(t, [objs[7]])
    check("... and the subscription that was there stays in force", vm.vals(got, objs[7]) == 250, str(got))

    # one big object in several records
    t = vm.link.mark()
    vm.sub([big])
    got = vm.collect(t, [big], 4.0)
    frames = [parse_frame(f) for _, f in tele_frames(vm.link, t)]
    recs = [r for fr in frames if fr for r in fr if r[0] == "val" and r[1] == big]
    check("a 1000-byte object is split over several records (start_idx grows)", len(recs) >= 2 and recs[0][2] == 0 and recs[1][2] > 0, f"{len(recs)} records")
    check("... and reassembles to what was uploaded", got.get(big) == bytes((i * 7) & 0xFF for i in range(1000)))

    # a tree: the children come with their parent
    t = vm.link.mark()
    vm.sub([tree])
    got = vm.collect(t, [tree, a, b])
    check("a pointer object: its children are sent with it", vm.vals(got, a) == 1.0 and vm.vals(got, b) == 2 and tree in got, str(got.keys()))
    t = vm.link.mark()
    vm.write(a, 8.0)
    got = vm.collect(t, [a])
    frames = [parse_frame(f) for _, f in tele_frames(vm.link, t)]
    ids_sent = {r[1] for fr in frames if fr for r in fr if r[0] == "val"}
    check("a child changes: only the child is sent, not the parent", ids_sent == {a}, str(ids_sent))
    vm.reset()

    # stopped: the snapshot comes at once
    p = Prog()
    x = p.u32(31337)
    r = vm.load(p)
    t = vm.link.mark()
    vm.sub([x])
    got = vm.collect(t, [x], 1.5)
    check("a program that has not started: the snapshot is sent at once (no pass needed)", vm.vals(got, x) == 31337, str(got))
    vm.exec(X_NORMAL)
    vm.reset()
    vm.load(p)
    vm.exec(X_NORMAL)
    t = vm.link.mark()
    time.sleep(0.5)
    check("after a reset the old subscription is gone: no frames until subscribed", len(tele_frames(vm.link, t)) == 0)
    vm.reset()


@test("telemetry/a frame is a command")
def t_telemetry_cmd(vm: Vm) -> None:
    p = Prog()
    a, b = p.u32(7), p.f(2.5)
    name = p.u8(9, name="nm")
    if not loaded(vm, p, X_NORMAL):
        return
    t = vm.link.mark()
    vm.sub([a, b, name])
    got = vm.collect(t, [a, b, name])
    frame = next((f for _, f in tele_frames(vm.link, t) if parse_frame(f)), None)
    check("a telemetry frame is [0x0B][DATA][n] records (no direction byte: it is not a request)", frame is not None and frame[:2] == bytes([KEY_VM, OP_DATA]), str(frame))
    vm.writes({a: 0, b: 0.0, name: 0})
    time.sleep(0.3)
    t2 = vm.link.mark()
    r = vm.link.request(frame[:2], frame[2:])  # the request takes the direction byte after the key: [0x0B][WRITE][DATA][n]
    check("sent back to the VM as a command (with the direction byte) it is accepted", ok(r), str(r))
    got = vm.collect(t2, [a, b, name])
    check("... and sets the values it carries", (vm.vals(got, a), vm.vals(got, b), vm.vals(got, name)) == (7, 2.5, 9), str(got))
    vm.reset()


# =====================================================================================================================
# retained values
# =====================================================================================================================


def retain_prog(cnt_type: int = T_U32, extra: bool = True) -> tuple[Prog, dict]:
    p = Prog()
    ids = {}
    ids["cnt"] = p.obj(cnt_type, 1, flags=F_MUT | F_RET, name="cnt", init=5 if cnt_type != T_F else 5.0)
    if extra:
        ids["vf"] = p.obj(T_F, 1, flags=F_MUT | F_RET, name="vf", init=1.5)
        ids["arr"] = p.obj(T_U8, 4, flags=F_MUT | F_RET, name="arr", init=[1, 2, 3, 4])
        ids["plain"] = p.u8(9)
    return p, ids


@test("retain/values survive a re-upload")
def t_retain(vm: Vm) -> None:
    vm.exec(X_RETAIN_CLEAR)
    p, ids = retain_prog()
    if not loaded(vm, p, X_NORMAL, "a program with retentive objects"):
        return
    check("the first start (nothing stored): the uploaded values", (vm.read(ids["cnt"]), vm.read(ids["vf"])) == (5, 1.5))
    vm.writes({ids["cnt"]: 777, ids["vf"]: 2.5, ids["arr"]: [9, 8, 7, 6], ids["plain"]: 33})
    time.sleep(0.3)
    vm.reset()  # saves what the program holds
    time.sleep(0.5)
    p, ids = retain_prog()
    t = vm.link.mark()
    if not loaded(vm, p, X_NORMAL, "the same program again"):
        return
    got = vm.snap(list(ids.values()))
    check("the retentive values are back (cnt 777, vf 2.5, arr [9,8,7,6])", (vm.vals(got, ids["cnt"]), vm.vals(got, ids["vf"]), vm.vals(got, ids["arr"])) == (777, 2.5, [9, 8, 7, 6]), str(got))
    check("a value that is not retentive keeps the uploaded one", vm.vals(got, ids["plain"]) == 9)
    vm.write(ids["cnt"], 778)
    time.sleep(0.3)
    r = vm.op(OP_OPEN, struct.pack("<HHHI", 0, 0, 0, 64))
    check("OPEN (a new program) saves the old one's values too", ok(r), str(r))
    vm.reset()
    p, ids = retain_prog()
    loaded(vm, p, X_NORMAL, "the program a third time")
    check("... cnt is 778", vm.read(ids["cnt"]) == 778)
    vm.reset()

    # a name that came back with another type or size keeps the uploaded value and says so
    p, ids = retain_prog(cnt_type=T_F, extra=False)
    t = vm.link.mark()
    loaded(vm, p, X_NORMAL, "a program whose 'cnt' is a float now")
    check("the type changed: the uploaded value stays (5.0)", vm.read(ids["cnt"]) == 5.0)
    n = wait_err(vm.link, t, "ERR_VM_RETAIN_MISMATCH", 2.0)
    check("... and ERR_VM_RETAIN_MISMATCH reports it", n is not None, str(err_names(vm.link, t)))
    vm.reset()

    # reset all
    vm.exec(X_RETAIN_CLEAR)
    p, ids = retain_prog()
    loaded(vm, p, X_NORMAL, "the program after RETAIN_CLEAR")
    check("EXEC retain-clear forgets them: the uploaded values again (cnt 5)", (vm.read(ids["cnt"]), vm.read(ids["vf"])) == (5, 1.5))
    vm.reset()
    vm.exec(X_RETAIN_CLEAR)

    # refused at load
    def retentive(name, t, nbytes, flags=F_MUT | F_RET, nm=b"n"):
        tiny(vm, 3, size=8192)
        return vm.op(OP_ADD_OBJECTS, batch(obj_rec(0, t, nbytes, flags, nm)))

    r = retentive("unnamed", T_U32, 4, nm=b"")
    check("a retentive object with no name -> ERR_VM_RETAIN_UNNAMED", cause(r) == "ERR_VM_RETAIN_UNNAMED", str(r))
    r = retentive("ptr", T_PTR, 4)
    check("a retentive pointer is refused (ERR_VM_OBJ_RETENTIVE_PTR)", cause(r) in ("ERR_VM_OBJ_RETENTIVE_PTR", "ERR_VM_RETAIN_UNNAMED") and not ok(r), str(r))
    r = retentive("big", T_U8, 2000)
    check("a retentive object larger than CONFIG_VM_RETAIN_MAX_BYTES (1024) -> ERR_VM_RETAIN_TOO_BIG", cause(r) == "ERR_VM_RETAIN_TOO_BIG", str(r))
    tiny(vm, 3, size=8192)
    r1 = vm.op(OP_ADD_OBJECTS, batch(obj_rec(0, T_U8, 600, F_MUT | F_RET, b"a")))
    r2 = vm.op(OP_ADD_OBJECTS, batch(obj_rec(1, T_U8, 600, F_MUT | F_RET, b"b")))
    check("two retentive objects of 600 bytes: the second one no longer fits the budget -> ERR_VM_RETAIN_TOO_BIG", ok(r1) and cause(r2) == "ERR_VM_RETAIN_TOO_BIG", f"{r1} / {r2}")
    vm.reset()


# =====================================================================================================================
# limits and load
# =====================================================================================================================


@test("stress/a long chain in one pass")
def t_chain(vm: Vm) -> None:
    n = 60
    p = Prog(size=65536)
    xs = [p.f(0.0) for _ in range(n + 1)]
    for i in range(n):
        p.block("EXPR", ins=[xs[i]], outs=[xs[i + 1]], state=expr_state([1.0], [F_IN, 0, F_K, 0, OPF["ADD"], OPF["END"]]))
    r = vm.start(p, X_SCAN)
    check(f"a chain of {n} blocks (and {n + 1} objects, {n} accessors) loads", ok(r), str(r))
    if not ok(r):
        return
    g = vm.step([xs[0], xs[n // 2], xs[n]], {xs[0]: 0.0})
    check(f"one pass runs the whole chain in order: the last value is {n} (each block is triggered by the one before)", (vm.vals(g, xs[n // 2]), vm.vals(g, xs[n])) == (float(n // 2), float(n)), f"{vm.vals(g, xs[n // 2])} {vm.vals(g, xs[n])}")
    g = vm.step([xs[n]], {xs[0]: 10.0})
    check("a new input propagates the same way", vm.vals(g, xs[n]) == float(n + 10), str(vm.vals(g, xs[n])))
    vm.reset()

    # blocks in the wrong order lag one pass behind
    p = Prog()
    a, b, c = p.f(0.0), p.f(0.0), p.f(0.0)
    p.block("EXPR", ins=[b], outs=[c], state=expr_state([1.0], [F_IN, 0, F_K, 0, OPF["ADD"], OPF["END"]]))  # reads b, written by the next block
    p.block("EXPR", ins=[a], outs=[b], state=expr_state([1.0], [F_IN, 0, F_K, 0, OPF["ADD"], OPF["END"]]))
    vm.start(p, X_SCAN)
    g = vm.step([b, c], {a: 10.0})
    check("execution order is upload order: a block placed before its producer sees the value a pass later", (vm.vals(g, b), vm.vals(g, c)) == (11.0, 0.0), f"{vm.vals(g, b)} {vm.vals(g, c)}")
    vm.reset()


@test("stress/reload many times")
def t_reload(vm: Vm) -> None:
    good = 0
    for i in range(8):
        p = Prog()
        a, b, q = p.f(1.0 + i), p.f(2.0), p.f()
        e = p.b()
        p.block("EXPR", ins=[a, b], outs=[q], eno=e, state=expr_state([], [F_IN, 0, F_IN, 1, OPF["MUL"], OPF["END"]]))
        r = vm.start(p, X_SCAN)
        if not ok(r):
            check(f"reload {i}: the program starts", False, str(r))
            break
        g = vm.step([q], kick=True)
        if close(vm.vals(g, q), 2.0 * (1 + i)):
            good += 1
        vm.reset()
    check("8 loads, runs and resets in a row all compute correctly (no leak, no leftover state)", good == 8, f"{good}/8")
    # a load that fails half way, then a good one
    for i in range(3):
        tiny(vm, 4, 1, 1, 4096)
        vm.op(OP_ADD_OBJECTS, batch(obj_rec(0, T_F, 4)))
        vm.op(OP_ADD_OBJECTS, batch(obj_rec(0, T_F, 4)))  # duplicate: the load is thrown away
    p = Prog()
    a = p.f(3.0)
    q = p.f()
    p.block("EXPR", ins=[a], outs=[q], state=expr_state([], fcode_neg()))
    r = vm.start(p, X_SCAN)
    g = vm.step([q], kick=True)
    check("after three failed loads a good program still loads and runs", ok(r) and vm.vals(g, q) == -3.0, str(vm.vals(g, q)))
    vm.reset()


def fcode_neg() -> list[int]:
    return [F_IN, 0, OPF["NEG"], OPF["END"]]


@test("stress/large objects")
def t_large(vm: Vm) -> None:
    for n, must in ((3000, True), (6000, False)):
        pat = bytes((i * 31 + 7) & 0xFF for i in range(n))
        p = Prog(size=40000)
        big = p.obj(T_U8, n, init=pat)
        r = vm.start(p, X_NORMAL)
        check(f"a {n}-byte object loads", ok(r), str(r))
        t = vm.link.mark()
        vm.sub([big])
        got = vm.collect(t, [big], 8.0, quiet=0.8)
        frames = [f for _, f in tele_frames(vm.link, t)]
        check(f"... {n} bytes: no telemetry frame longer than 512 bytes", frames and max(map(len, frames)) <= 512, f"{max(map(len, frames), default=0)}")
        check(f"{n} bytes come back whole over many telemetry records" + ("" if must else " (more than the 4096-byte connector queue: the sampler sends the whole object in one go, the queue drops what does not fit and the next pass starts again from the first byte, so the tail never arrives)"),
              got.get(big) == pat, f"{len(got.get(big, b''))} bytes")
        if must:
            vm.write(big, bytes([0xAB]) * 50, 1500)
            got = vm.snap([big], 10.0)
            check("a runtime write in the middle of it shows in the next snapshot", got.get(big, b"")[1500:1550] == bytes([0xAB]) * 50 and got[big][1499] == pat[1499], "wrong")
        vm.reset()
