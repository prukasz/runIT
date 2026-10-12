"""VM suite, part 2: the flow blocks (IF, SWITCH, FOR), the data blocks (SET, CLONE), the state blocks (EDGE, LATCH) and the
time blocks (TIMER, PERIODIC), and the blocks that start records and listen to events (ACTION). Run through vm_suite.py."""

from __future__ import annotations

import math
import struct
import time
import zlib

from router_link import Link
from vm_lib import *  # noqa: F401,F403
from vm_t_core import blk_head, expr_state, F_IN, F_K, OPF  # noqa: F401

NAN = struct.unpack("<f", struct.pack("<I", 0x7FC00000))[0]


def loaded(vm: Vm, p: Prog, mode: int = X_SCAN, what: str = "the program") -> bool:
    r = vm.start(p, mode)
    return check(f"{what} loads and starts", ok(r), str(r))


# =====================================================================================================================
# IF and SWITCH
# =====================================================================================================================


@test("flow/if")
def t_if(vm: Vm) -> None:
    p = Prog()
    kinds = {"F": (p.f, [1.0, 0.0, 0.25, -2.0]), "I32": (p.i32, [5, 0, -1, 0]), "U8": (p.u8, [255, 0, 1, 0]), "B": (p.b, [True, False, True, False]),
             "U32": (p.u32, [0x80000000, 0, 1, 0])}
    rows = {}
    for k, (mk, vals) in kinds.items():
        c = mk(vals[0])
        y, n, e = p.b(), p.b(), p.b()
        p.block("IF", ins=[c], outs=[y, n], eno=e)
        rows[k] = (c, y, n, e, vals)
    if not loaded(vm, p):
        return
    ids = [i for r in rows.values() for i in r[1:4]]
    for step in range(4):
        w = {r[0]: r[4][step] for r in rows.values()}
        g = vm.step(ids, w if step else {})
        for k, (c, y, n, e, vals) in rows.items():
            yes = vals[step] not in (0, 0.0, False)
            check(f"IF({k} {vals[step]!r}): yes={int(yes)} no={int(not yes)} ENO=1", (vm.vals(g, y), vm.vals(g, n), vm.vals(g, e)) == (int(yes), int(not yes), 1),
                  str((vm.vals(g, y), vm.vals(g, n), vm.vals(g, e))))
    vm.reset()

    # enables
    p = Prog()
    c = p.f(1.0)
    g1, g2 = p.b(False), p.b(False)
    ya, na, ea = p.b(), p.b(), p.b()
    yb, nb, eb = p.b(), p.b(), p.b()
    yc, nc, ec = p.b(), p.b(), p.b()
    p.block("IF", ins=[c], outs=[ya, na], eno=ea, ens=[g1, g2], en_mode=EN_ANY)
    p.block("IF", ins=[c], outs=[yb, nb], eno=eb, ens=[g1, g2], en_mode=EN_ALL)
    p.block("IF", ins=[c], outs=[yc, nc], eno=ec)  # no enable: always on
    if not loaded(vm, p):
        return
    ids = [ya, na, ea, yb, nb, eb, yc, nc, ec]
    table = [((0, 0), (0, 0, 0), (0, 0, 0)), ((1, 0), (1, 0, 1), (0, 0, 0)), ((0, 1), (1, 0, 1), (0, 0, 0)), ((1, 1), (1, 0, 1), (1, 0, 1)),
             ((0, 0), (0, 0, 0), (0, 0, 0))]
    for (a, b), want_any, want_all in table:
        g = vm.step(ids, {g1: bool(a), g2: bool(b)})
        got_any = (vm.vals(g, ya), vm.vals(g, na), vm.vals(g, ea))
        got_all = (vm.vals(g, yb), vm.vals(g, nb), vm.vals(g, eb))
        check(f"gates {a}{b}: ANY -> {want_any}", got_any == want_any, str(got_any))
        check(f"gates {a}{b}: ALL -> {want_all}", got_all == want_all, str(got_all))
        check(f"gates {a}{b}: a block with no enable always decides (yes)", (vm.vals(g, yc), vm.vals(g, nc), vm.vals(g, ec)) == (1, 0, 1))
    g = vm.step(ids, {c: 0.0, g1: True})
    check("a branch that is not taken is cleared again (yes drops, no rises)", (vm.vals(g, ya), vm.vals(g, na)) == (0, 1), str((vm.vals(g, ya), vm.vals(g, na))))
    vm.reset()

    # shape
    def shape(name, **kw):
        p = Prog()
        c = p.f(1.0)
        o = [p.b() for _ in range(kw.get("nout", 2))]
        ins = [c] * kw.get("nin", 1) if not kw.get("unwired") else [None]
        p.block("IF", ins=ins, outs=o)
        r = vm.load(p)
        check(f"IF {name} -> ERR_VM_BLK_BAD_SHAPE", cause(r) == "ERR_VM_BLK_BAD_SHAPE", str(r))

    shape("with one output", nout=1)
    shape("with three outputs", nout=3)
    shape("with no output", nout=0)
    shape("with two inputs", nin=2)
    shape("with an unwired condition", unwired=True)
    vm.reset()


@test("flow/switch")
def t_switch(vm: Vm) -> None:
    p = Prog()
    sf, si, su = p.f(0.0), p.i32(0), p.u8(0)
    gate = p.b(True)
    outs = {}
    for name, sel in (("F", sf), ("I32", si), ("U8", su)):
        o = [p.b() for _ in range(4)]
        e = p.b()
        p.block("SWITCH", ins=[sel], outs=o, eno=e)
        outs[name] = (sel, o, e)
    o16 = [p.b() for _ in range(16)]
    e16 = p.b()
    s16 = p.i32(0)
    p.block("SWITCH", ins=[s16], outs=o16, eno=e16)
    og = [p.b() for _ in range(3)]
    eg = p.b()
    sg = p.i32(1)
    p.block("SWITCH", ins=[sg], outs=og, eno=eg, ens=[gate])
    if not loaded(vm, p):
        return
    ids = [x for (sel, o, e) in outs.values() for x in (*o, e)] + o16 + [e16] + og + [eg]

    def want(index, n=4):
        return [1 if i == index else 0 for i in range(n)], (1 if index is not None and 0 <= index < n else 0)

    for sel_val in (0, 1, 2, 3, 4, 5, -1, 100):
        g = vm.step(ids, {outs["F"][0]: float(sel_val), outs["I32"][0]: sel_val, outs["U8"][0]: sel_val if sel_val >= 0 else 0}, )
        for name in ("F", "I32", "U8"):
            sel, o, e = outs[name]
            if name == "U8" and sel_val < 0:
                continue
            wo, we = want(sel_val)
            got = [vm.vals(g, x) for x in o], vm.vals(g, e)
            check(f"SWITCH({name}) selector {sel_val}: outputs {wo} ENO {we}", got == (wo, we), str(got))
    for f, idx in ((1.4, 1), (1.6, 2), (2.4, 2), (-0.4, 0), (-0.6, None), (3.4, 3), (3.6, None)):
        g = vm.step(ids, {outs["F"][0]: f})
        wo, we = want(idx)
        got = [vm.vals(g, x) for x in outs["F"][1]], vm.vals(g, outs["F"][2])
        check(f"SWITCH(F) selector {f} is rounded: -> case {idx}", got == (wo, we), str(got))
    for idx in (0, 7, 15, 16, 17):
        g = vm.step(ids, {s16: idx})
        wo, we = want(idx, 16)
        got = [vm.vals(g, x) for x in o16], vm.vals(g, e16)
        check(f"SWITCH with 16 cases, selector {idx}: -> {idx if idx < 16 else 'none'}", got == (wo, we), str(got))
    g = vm.step(ids, {sg: 1, gate: True})
    check("a gated SWITCH decides while the gate is open", [vm.vals(g, x) for x in og] == [0, 1, 0], str([vm.vals(g, x) for x in og]))
    g = vm.step(ids, {gate: False})
    check("... and drives nothing (ENO off) once it closes", [vm.vals(g, x) for x in og] == [0, 0, 0] and vm.vals(g, eg) == 0, str([vm.vals(g, x) for x in og]))
    vm.reset()

    def shape(name, nout, nin=1, unwired=False):
        p = Prog()
        c = p.i32(0)
        o = [p.b() for _ in range(nout)]
        p.block("SWITCH", ins=[None] if unwired else [c] * nin, outs=o)
        r = vm.load(p)
        check(f"SWITCH {name} -> ERR_VM_BLK_BAD_SHAPE", cause(r) == "ERR_VM_BLK_BAD_SHAPE", str(r))

    shape("with no output", 0)
    shape("with 17 outputs", 17)
    shape("with two inputs", 2, nin=2)
    shape("with an unwired selector", 2, unwired=True)
    p = Prog()
    p.block("SWITCH", ins=[p.i32(0)], outs=[p.b()])
    check("SWITCH with one output loads", vm.load(p) is None)
    vm.reset()


# =====================================================================================================================
# FOR
# =====================================================================================================================

FOR_ADD, FOR_SUB, FOR_MUL, FOR_DIV = 0, 1, 2, 3
FOR_LT, FOR_LE, FOR_GT, FOR_GE = 0, 1, 2, 3


def for_state(start, end, step, span=(1, 2), max_turns=100, op=FOR_ADD, cmp=FOR_LT) -> bytes:
    return struct.pack("<HHfffHBBB3x", span[0], span[1], start, end, step, max_turns, op, cmp, 0)


def for_prog(start, end, step, op=FOR_ADD, cmp=FOR_LT, max_turns=100, pins=(None, None, None), span=(1, 2), gate=None):
    """FOR (block 0) with an EXPR body (block 1) that adds the index to s. Returns (prog, idx, s, eno)."""
    p = Prog()
    idx, s, eno = p.f(0.0), p.f(0.0), p.b()
    wired = [None if v is None else p.f(v) for v in pins]
    p.block("FOR", ins=wired if any(w is not None for w in wired) else [], outs=[idx], eno=eno, ens=[gate] if gate is not None else [],
            state=for_state(start, end, step, span, max_turns, op, cmp))
    p.block("EXPR", ins=[idx, s], outs=[s], state=expr_state([], [F_IN, 1, F_IN, 0, OPF["ADD"], OPF["END"]]))
    return p, idx, s, eno


@test("loop/for: every operation and comparison")
def t_for(vm: Vm) -> None:
    cases = [
        ("0..5 +1 <", 0, 5, 1, FOR_ADD, FOR_LT, 10, 4, 1),
        ("0..5 +1 <=", 0, 5, 1, FOR_ADD, FOR_LE, 15, 5, 1),
        ("5..0 -1 >", 5, 0, 1, FOR_SUB, FOR_GT, 15, 1, 1),
        ("5..0 -1 >=", 5, 0, 1, FOR_SUB, FOR_GE, 15, 0, 1),
        ("1..100 *2 <", 1, 100, 2, FOR_MUL, FOR_LT, 127, 64, 1),
        ("64..1 /2 >", 64, 1, 2, FOR_DIV, FOR_GT, 126, 2, 1),
        ("0..1 +0.25 <=", 0, 1, 0.25, FOR_ADD, FOR_LE, 2.5, 1.0, 1),
        ("-3..3 +2 <", -3, 3, 2, FOR_ADD, FOR_LT, -3 + -1 + 1, 1, 1),
        ("5..5 +1 < (no turn)", 5, 5, 1, FOR_ADD, FOR_LT, 0, 0, 0),
        ("0..5 +1 > (wrong way, no turn)", 0, 5, 1, FOR_ADD, FOR_GT, 0, 0, 0),
        ("5..5 +1 <= (one turn)", 5, 5, 1, FOR_ADD, FOR_LE, 5, 5, 1),
    ]
    for name, st, en, sp, op, cmp, want_sum, want_idx, want_eno in cases:
        p, idx, s, eno = for_prog(st, en, sp, op, cmp)
        vm.start(p, X_SCAN)
        g = vm.step([idx, s, eno])
        check(f"FOR {name}: sum of the indexes {want_sum}, last index {want_idx}, ENO {want_eno}",
              close(vm.vals(g, s), float(want_sum)) and close(vm.vals(g, idx), float(want_idx)) and vm.vals(g, eno) == want_eno,
              f"sum={vm.vals(g, s)} idx={vm.vals(g, idx)} eno={vm.vals(g, eno)}")
        if name.startswith("0..5 +1 <") and "(" not in name:
            g = vm.step([idx, s, eno])
            check("... every pass runs the whole loop again (the sum doubles)", close(vm.vals(g, s), 2.0 * want_sum), str(vm.vals(g, s)))
    vm.reset()


@test("loop/for: pins, budget, faults")
def t_for_edge(vm: Vm) -> None:
    # Start, End, Step from objects instead of the constants
    p, idx, s, eno = for_prog(100, 100, 100, pins=(2.0, 6.0, 2.0))
    vm.start(p, X_SCAN)
    g = vm.step([idx, s, eno])
    check("Start 2, End 6, Step 2 from pins (the constants are ignored): indexes 2 and 4", close(vm.vals(g, s), 6.0), str(vm.vals(g, s)))
    vm.reset()
    p, idx, s, eno = for_prog(0, 4, 100, pins=(None, None, 1.0))
    vm.start(p, X_SCAN)
    g = vm.step([idx, s, eno])
    check("only Step wired: Start and End come from the constants (0..4 by 1 -> 6)", close(vm.vals(g, s), 6.0), str(vm.vals(g, s)))
    vm.reset()

    # budget: a step of 0 never ends
    p, idx, s, eno = for_prog(3, 10, 0, max_turns=10)
    vm.start(p, X_SCAN)
    t = vm.link.mark()
    g = vm.step([idx, s, eno])
    check("a step of 0 stops at max_turns: the body ran exactly 10 times (10 x 3)", close(vm.vals(g, s), 30.0), str(vm.vals(g, s)))
    n = wait_err(vm.link, t, "ERR_VM_FOR_BAD_LOOP")
    check("... and ERR_VM_FOR_BAD_LOOP reports it (reason 0: capped)", n is not None and n.data[-1] == 0, str(n))
    t = vm.link.mark()
    vm.step([idx, s, eno])
    time.sleep(0.3)
    check("... only once, not on every pass", len(err_nodes(vm.link, t, "ERR_VM_FOR_BAD_LOOP")) == 0)
    vm.reset()
    p, idx, s, eno = for_prog(8, 1, 0, op=FOR_DIV, cmp=FOR_GT)
    vm.start(p, X_SCAN)
    t = vm.link.mark()
    g = vm.step([idx, s, eno])
    n = wait_err(vm.link, t, "ERR_VM_FOR_BAD_LOOP")
    check("8 / 0 is not finite: reported (reason 1)", n is not None and n.data[-1] == 1, str(n))
    check("... the body ran once before it", close(vm.vals(g, s), 8.0), str(vm.vals(g, s)))
    vm.reset()
    p2, idx2, s2, eno2 = for_prog(0, 5, 1, pins=(NAN, None, None))
    vm.start(p2, X_SCAN)
    t = vm.link.mark()
    g = vm.step([idx2, s2, eno2], kick=True)
    check("a NaN start: the loop does not run, ENO false", vm.vals(g, s2) == 0.0 and vm.vals(g, eno2) == 0, f"{vm.vals(g, s2)} {vm.vals(g, eno2)}")
    time.sleep(0.3)
    check("... and it is reported (FOR.md: 'is reported and the loop does not run')", len(err_nodes(vm.link, t)) > 0, "nothing reported")
    vm.reset()

    # an enable
    p, idx, s, eno = for_prog(0, 3, 1)
    gate = p.b(False)
    p.blks[0] = p.block_frame(0, "FOR", outs=[idx], ens=[gate], eno=eno, state=for_state(0, 3, 1))
    vm.start(p, X_SCAN)
    g = vm.step([idx, s, eno, gate])
    check("a FOR whose gate is closed does not run, and its body is skipped with it", vm.vals(g, s) == 0.0 and vm.vals(g, eno) == 0, f"{vm.vals(g, s)} {vm.vals(g, eno)}")
    g = vm.step([idx, s, eno, gate], {gate: True})
    check("... it runs when the gate opens (0+1+2)", close(vm.vals(g, s), 3.0) and vm.vals(g, eno) == 1, f"{vm.vals(g, s)} {vm.vals(g, eno)}")
    vm.reset()

    # shape
    for name, kw in (("an unknown step operation (4)", dict(op=4)), ("an unknown condition (4)", dict(cmp=4))):
        p, idx, s, eno = for_prog(0, 3, 1, **kw)
        r = vm.load(p)
        check(f"FOR with {name} -> ERR_VM_BLK_BAD_SHAPE", cause(r) == "ERR_VM_BLK_BAD_SHAPE", str(r))
    vm.reset()


@test("loop/for: nesting and spans")
def t_for_nest(vm: Vm) -> None:
    def nested(depth: int, turns_outer=3, turns_inner=4):
        """`depth` FORs inside each other, a counter body. The innermost runs turns_inner times, the others turns_outer."""
        p = Prog()
        s = p.f(0.0)
        idxs = [p.f(0.0) for _ in range(depth)]
        n = depth + 1
        for d in range(depth):
            t = turns_inner if d == depth - 1 else turns_outer
            p.block("FOR", outs=[idxs[d]], state=for_state(0, t, 1, span=(d + 1, n)))
        p.block("EXPR", ins=[idxs[-1], s], outs=[s], state=expr_state([1.0], [F_IN, 1, F_K, 0, OPF["ADD"], OPF["END"]]))
        return p, s

    p, s = nested(2)
    vm.start(p, X_SCAN)
    g = vm.step([s])
    check("two nested loops (3 x 4): the body ran 12 times", close(vm.vals(g, s), 12.0), str(vm.vals(g, s)))
    vm.reset()
    p, s = nested(3, 2, 2)
    vm.start(p, X_SCAN)
    t = vm.link.mark()
    g = vm.step([s])
    check("three nested loops (2 x 2 x 2): 8", close(vm.vals(g, s), 8.0), str(vm.vals(g, s)))
    time.sleep(0.2)
    check("... within the span depth (CONFIG_VM_EXEC_MAX_SPAN_DEPTH = 4, the top level counts): no error", "ERR_VM_EXEC_SPAN_DEPTH" not in err_names(vm.link, t))
    vm.reset()
    p, s = nested(4, 2, 2)
    vm.start(p, X_SCAN)
    t = vm.link.mark()
    g = vm.step([s])
    n = wait_err(vm.link, t, "ERR_VM_EXEC_SPAN_DEPTH")
    check("four nested loops are one too deep: ERR_VM_EXEC_SPAN_DEPTH", n is not None, str(err_names(vm.link, t)))
    check("... and the body of the deepest loop did not run", vm.vals(g, s) == 0.0, str(vm.vals(g, s)))
    g = vm.step([s])
    check("... the VM keeps stepping", g.get(s) is not None)
    vm.reset()

    # spans that do not fit
    for name, span in (("ends past the last block", (1, 9)), ("does not start right after the loop", (2, 3)), ("is empty", (1, 1)), ("is reversed", (2, 1))):
        p, idx, s, eno = for_prog(0, 3, 1, span=span)
        vm.start(p, X_SCAN)
        t = vm.link.mark()
        g = vm.step([idx, s, eno])
        n = wait_err(vm.link, t, "ERR_VM_EXEC_BAD_SPAN")
        check(f"a FOR span that {name}: ERR_VM_EXEC_BAD_SPAN", n is not None, str(err_names(vm.link, t)))
        g = vm.step([idx, s, eno])
        check("... and the VM keeps running (no hang)", g.get(s) is not None)
        vm.reset()


# =====================================================================================================================
# SET and CLONE
# =====================================================================================================================


def set_state(v: float = 0.0) -> bytes:
    return f32(v) + bytes(4)


@test("data/set: copy")
def t_set_copy(vm: Vm) -> None:
    p = Prog()
    pairs = {}

    def pair(name, mk, val, mk_dst=None, n=1, dst_n=None):
        src = mk(val) if n == 1 else p.obj({"F": T_F, "U32": T_U32, "I32": T_I32, "U8": T_U8, "B": T_B}[name], n, init=val)
        dst = (mk_dst or mk)() if n == 1 else p.obj({"F": T_F, "U32": T_U32, "I32": T_I32, "U8": T_U8, "B": T_B}[name], dst_n or n)
        eno = p.b()
        p.block("SET", ins=[src, dst], eno=eno, state=set_state())
        pairs[name + (f"[{n}]" if n > 1 else "") + (f"->[{dst_n}]" if dst_n else "")] = (src, dst, eno, val)

    pair("F", p.f, 3.5)
    pair("U32", p.u32, 4000000000)
    pair("I32", p.i32, -123456)
    pair("U8", p.u8, 200)
    pair("B", p.b, True)
    pair("F", None, [1.0, 2.0, 3.0, 4.0], n=4)
    pair("U8", None, [9, 8, 7], n=3)
    mism = p.f(1.5)
    mdst = p.u32()
    meno = p.b()
    p.block("SET", ins=[mism, mdst], eno=meno, state=set_state())
    sz_src = p.obj(T_F, 4, init=[1, 2, 3, 4])
    sz_dst = p.obj(T_F, 3)
    sz_eno = p.b()
    p.block("SET", ins=[sz_src, sz_dst], eno=sz_eno, state=set_state())
    ro_src = p.f(5.0)
    ro_dst = p.f(1.0, flags=0)
    ro_eno = p.b()
    p.block("SET", ins=[ro_src, ro_dst], eno=ro_eno, state=set_state())
    pr_src = p.f(5.0)
    pr_dst = p.f(1.0, flags=F_MUT | F_PROT)
    pr_eno = p.b()
    p.block("SET", ins=[pr_src, pr_dst], eno=pr_eno, state=set_state())
    el_src = p.f(6.25)
    el_dst = p.obj(T_F, 3, init=[1.0, 2.0, 3.0])
    el_acc = p.acc(el_dst, [("lit", 2)])
    el_eno = p.b()
    p.block("SET", ins=[el_src, el_acc], eno=el_eno, state=set_state())
    if not loaded(vm, p):
        return
    t = vm.link.mark()
    ids = [x for v in pairs.values() for x in v[1:3]] + [mdst, meno, sz_dst, sz_eno, ro_dst, ro_eno, pr_dst, pr_eno, el_dst, el_eno]
    g = vm.step(ids, kick=True)
    for name, (src, dst, eno, val) in pairs.items():
        got = vm.vals(g, dst)
        want = val
        floats = any(isinstance(x, float) for x in (want if isinstance(want, list) else [want]))
        eq = close(got, want) if floats else got == want
        check(f"SET copies a {name} (a fresh source reaches the destination), ENO 1", eq and vm.vals(g, eno) == 1, f"{got} eno={vm.vals(g, eno)}")
    check("SET from F into U32 (types differ): the destination is left alone, ENO 0", vm.vals(g, mdst) == 0 and vm.vals(g, meno) == 0, f"{vm.vals(g, mdst)} {vm.vals(g, meno)}")
    check("SET between arrays of different length: refused, ENO 0", vm.vals(g, sz_dst) == [0.0, 0.0, 0.0] and vm.vals(g, sz_eno) == 0, f"{vm.vals(g, sz_dst)} {vm.vals(g, sz_eno)}")
    check("SET into a destination that is not mutable: refused, ENO 0", vm.vals(g, ro_dst) == 1.0 and vm.vals(g, ro_eno) == 0, f"{vm.vals(g, ro_dst)} {vm.vals(g, ro_eno)}")
    check("SET into a protected destination: refused, ENO 0", vm.vals(g, pr_dst) == 1.0 and vm.vals(g, pr_eno) == 0, f"{vm.vals(g, pr_dst)} {vm.vals(g, pr_eno)}")
    check("SET into one element of an array (accessor with an index): only that element", vm.vals(g, el_dst) == [1.0, 2.0, 6.25] and vm.vals(g, el_eno) == 1, str(vm.vals(g, el_dst)))
    time.sleep(0.3)
    names = err_names(vm.link, t)
    check("the type mismatch is reported (ERR_VM_OBJ_COPY_MISMATCH)", "ERR_VM_OBJ_COPY_MISMATCH" in names, str(sorted(set(names))))
    check("the protected destination is reported (ERR_VM_OBJ_USR_PROTECTED)", "ERR_VM_OBJ_USR_PROTECTED" in names or "ERR_VM_OBJ_NOT_MUTABLE" in names, str(sorted(set(names))))
    check("the read-only destination is reported (ERR_VM_OBJ_NOT_MUTABLE)", "ERR_VM_OBJ_NOT_MUTABLE" in names, str(sorted(set(names))))

    # a source that is not fresh does not copy
    vm.write(pairs["F"][1], 0.0)
    g = vm.step([pairs["F"][1], pairs["F"][2]])
    check("a source that is not fresh copies nothing (and ENO is off)", vm.vals(g, pairs["F"][1]) == 0.0 and vm.vals(g, pairs["F"][2]) == 0, f"{vm.vals(g, pairs['F'][1])}")
    g = vm.step([pairs["F"][1], pairs["F"][2]], {pairs["F"][0]: 7.0})
    check("... writing the source copies again", vm.vals(g, pairs["F"][1]) == 7.0 and vm.vals(g, pairs["F"][2]) == 1, str(vm.vals(g, pairs["F"][1])))
    vm.reset()


@test("data/set: constant mode and shape")
def t_set_const(vm: Vm) -> None:
    p = Prog()
    cases = {}

    def const(name, mk, value, want):
        dst = mk()
        eno = p.b()
        p.block("SET", ins=[None, dst], eno=eno, state=set_state(value))
        cases[name] = (dst, eno, want)

    const("F 2.5", p.f, 2.5, 2.5)
    const("U32 3.7 -> 4", p.u32, 3.7, 4)
    const("U32 3.2 -> 3", p.u32, 3.2, 3)
    const("I32 -7.5 -> -8", p.i32, -7.5, -8)
    const("I32 2.5 -> 3", p.i32, 2.5, 3)
    const("U8 300 -> 255 (saturates)", p.u8, 300.0, 255)
    const("U8 -5 -> 0 (saturates)", p.u8, -5.0, 0)
    const("B 1.0 -> true", p.b, 1.0, 1)
    const("B 0.0 -> false", lambda: p.b(True), 0.0, 0)
    gate = p.b(False)
    gdst, geno = p.f(), p.b()
    p.block("SET", ins=[None, gdst], ens=[gate], eno=geno, state=set_state(9.5))
    arr = p.obj(T_F, 3, init=[1.0, 2.0, 3.0])
    aeno = p.b()
    p.block("SET", ins=[None, arr], eno=aeno, state=set_state(4.0))
    if not loaded(vm, p):
        return
    ids = [x for d, e, _ in cases.values() for x in (d, e)] + [gdst, geno, gate, arr, aeno]
    g = vm.step(ids)
    for name, (dst, eno, want) in cases.items():
        check(f"SET with no source writes its constant: {name}", close(vm.vals(g, dst), float(want)) and vm.vals(g, eno) == 1, f"{vm.vals(g, dst)} eno={vm.vals(g, eno)}")
    check("... into the first element of an array only", vm.vals(g, arr) == [4.0, 2.0, 3.0], str(vm.vals(g, arr)))
    check("a SET with a gate does not write until the gate fires", vm.vals(g, gdst) == 0.0 and vm.vals(g, geno) == 0, f"{vm.vals(g, gdst)} {vm.vals(g, geno)}")
    # the constant is written once (no enable) and not again
    first = cases["F 2.5"][0]
    vm.write(first, 0.0)
    g = vm.step(ids)
    check("... the constant of a SET with no enable is written once (it is not written back on the next pass)", vm.vals(g, first) == 0.0 and vm.vals(g, cases['F 2.5'][1]) == 0, f"{vm.vals(g, first)}")
    g = vm.step(ids, {gate: True})
    check("an open gate (fresh) writes the constant", vm.vals(g, gdst) == 9.5 and vm.vals(g, geno) == 1, f"{vm.vals(g, gdst)} {vm.vals(g, geno)}")
    vm.write(gdst, 0.0)
    g = vm.step(ids)
    check("... a gate that stays open but is not rewritten does not write again", vm.vals(g, gdst) == 0.0, str(vm.vals(g, gdst)))
    g = vm.step(ids, {gate: True})
    check("... writing the gate again does", vm.vals(g, gdst) == 9.5, str(vm.vals(g, gdst)))
    vm.reset()

    # shape
    def shape(name, **kw):
        p = Prog()
        a, b = p.f(1.0), p.f(0.0)
        ins = kw.get("ins", [a, b])
        ins = [{"a": a, "b": b}.get(x, x) for x in ins]
        p.block("SET", ins=ins, state=kw.get("state", set_state()))
        r = vm.load(p)
        check(f"SET {name} -> ERR_VM_BLK_BAD_SHAPE", cause(r) == "ERR_VM_BLK_BAD_SHAPE", str(r))

    shape("with an unwired destination", ins=["a", None])
    shape("with one input", ins=["a"])
    shape("with three inputs", ins=["a", "b", "a"])
    shape("with a NaN constant", state=struct.pack("<I", 0x7FC00000) + bytes(4))
    shape("with an infinite constant", state=struct.pack("<I", 0x7F800000) + bytes(4))
    shape("with a state shorter than 8 bytes", state=bytes(4))
    vm.reset()


@test("data/set: a tree")
def t_set_tree(vm: Vm) -> None:
    p = Prog()
    a, b = p.f(1.5, name="a"), p.u32(77, name="b")
    a2, b2 = p.f(name="a"), p.u32(name="b")
    src = p.ptr(2, [a, b])
    dst = p.ptr(2, [a2, b2])
    gate, eno = p.b(False), p.b()
    p.block("SET", ins=[src, dst], ens=[gate], eno=eno, state=set_state())
    if not loaded(vm, p):
        return
    g = vm.step([src, dst, eno, gate])
    check("the tree is not copied until the source or a gate is fresh", g.get(a2) == f32(0.0), str(g.get(a2)))
    g = vm.step([src, dst, eno, gate], {gate: True})
    check("a gate opens: the children of the source are copied into the children of the destination", close(vm.vals(g, a2), 1.5) and vm.vals(g, b2) == 77, f"{vm.vals(g, a2)} {vm.vals(g, b2)}")
    check("... ENO 1", vm.vals(g, eno) == 1)
    g = vm.step([src, dst, eno, gate], {a: 9.0, gate: True})
    check("a changed child is copied again", close(vm.vals(g, a2), 9.0), str(vm.vals(g, a2)))
    vm.reset()

    # shapes that differ
    p = Prog()
    a, b = p.f(1.5), p.u32(7)
    a2, b2 = p.u32(), p.u32()  # the first child has another type
    src, dst = p.ptr(2, [a, b]), p.ptr(2, [a2, b2])
    gate, eno = p.b(False), p.b()
    p.block("SET", ins=[src, dst], ens=[gate], eno=eno, state=set_state())
    vm.start(p, X_SCAN)
    t = vm.link.mark()
    g = vm.step([dst, eno], {gate: True}, kick=False)
    check("trees of different shape: refused, ENO 0", vm.vals(g, eno) == 0, str(vm.vals(g, eno)))
    time.sleep(0.3)
    check("... and reported", any(n.startswith("ERR_VM_OBJ_COPY") for n in err_names(vm.link, t)), str(sorted(set(err_names(vm.link, t)))))
    vm.reset()


@test("data/clone: heap objects")
def t_clone(vm: Vm) -> None:
    p = Prog()
    s3 = p.obj(T_F, 3, init=[1.0, 2.0, 3.0])
    s5 = p.obj(T_F, 5, init=[10.0, 20.0, 30.0, 40.0, 50.0])
    cell = p.ptr(1)
    ga, gb = p.b(False), p.b(False)
    ea, eb = p.b(), p.b()
    cell_acc = p.acc(cell, [("lit", 0)])
    p.block("CLONE", ins=[s3, cell_acc], ens=[ga], eno=ea)
    p.block("CLONE", ins=[s5, cell_acc], ens=[gb], eno=eb)
    if not loaded(vm, p):
        return
    ids = [cell, ea, eb]
    g = vm.step(ids)
    check("before any gate fires the cell is empty (id 0xFFFF)", g.get(cell) == u16(0xFFFF), str(g.get(cell)))
    g = vm.step(ids, {ga: True})
    child = struct.unpack("<H", g[cell][:2])[0] if g.get(cell) else None
    check("the first CLONE builds a heap object: the cell holds its id (dyn bit set)", child is not None and child & DYN_BIT, str(child))
    check("... described to the app as a float array of 3", child in vm.dyn and vm.dyn[child] == (T_F, 3), str(vm.dyn))
    check("... with the values of the source", vm.vals(g, child) == [1.0, 2.0, 3.0], str(vm.vals(g, child)))
    check("... ENO 1", vm.vals(g, ea) == 1)
    first = child
    g = vm.step(ids, {s3: [4.0, 5.0, 6.0], ga: True})
    child = struct.unpack("<H", g[cell][:2])[0]
    check("a changed source refills the same heap object (same shape, no new allocation)", child == first and vm.vals(g, child) == [4.0, 5.0, 6.0], f"{child:#x} {vm.vals(g, child)}")
    g = vm.step(ids, {gb: True})
    child = struct.unpack("<H", g[cell][:2])[0]
    check("a source of another shape is rebuilt: a float array of 5", vm.dyn.get(child) == (T_F, 5) and vm.vals(g, child) == [10.0, 20.0, 30.0, 40.0, 50.0], f"{vm.dyn.get(child)} {vm.vals(g, child)}")
    g = vm.step(ids)
    check("a clone is fresh for one pass only: no new copy without news", vm.vals(g, ea) == 0 and vm.vals(g, eb) == 0)
    vm.reset()

    # shape of the block
    def shape(name, ins, expect="ERR_VM_BLK_BAD_SHAPE"):
        p = Prog()
        s = p.f(1.0)
        c = p.ptr(1)
        mapping = {"s": s, "c": p.acc(c, [("lit", 0)]), None: None}
        p.block("CLONE", ins=[mapping[x] for x in ins])
        r = vm.load(p)
        check(f"CLONE {name} -> {expect}", cause(r) == expect, str(r))

    shape("with one input", ["s"])
    shape("with an unwired cell", ["s", None])
    shape("with an unwired source", [None, "c"])
    p = Prog()
    s, c = p.f(1.0), p.ptr(1)
    p.block("CLONE", ins=[s, p.acc(c, [("lit", 0)])])
    check("CLONE with a source and a cell loads", vm.load(p) is None)
    # a cell that is not a pointer
    p = Prog()
    s, c, gate = p.f(1.0), p.f(0.0), p.b(False)
    e = p.b()
    p.block("CLONE", ins=[s, c], ens=[gate], eno=e)
    vm.start(p, X_SCAN)
    t = vm.link.mark()
    g = vm.step([e], {gate: True})
    check("a cell that is not a pointer: ENO 0 and an error", vm.vals(g, e) == 0 and wait_err(vm.link, t, "ERR_VM_BLOCK_FAILED") is not None, str(vm.vals(g, e)))
    vm.reset()


# =====================================================================================================================
# EDGE and LATCH
# =====================================================================================================================


@test("state/edge")
def t_edge(vm: Vm) -> None:
    for start_level in (False, True):
        p = Prog()
        g = p.b(start_level)
        eR, eF, eB = p.b(), p.b(), p.b()
        for typ, e in ((0, eR), (1, eF), (2, eB)):
            p.block("EDGE", ens=[g], eno=e, state=bytes([typ, 0, 0, 0]))
        if not loaded(vm, p, what=f"EDGE program (the gate starts {int(start_level)})"):
            return
        ids = [eR, eF, eB]

        def pulses(g_):
            return (vm.vals(g_, eR), vm.vals(g_, eF), vm.vals(g_, eB))

        got = pulses(vm.step(ids))
        check("the first sample only records the level: no pulse", got == (0, 0, 0), str(got))
        seq = [(not start_level, (1, 0, 1) if not start_level else (0, 1, 1)), (not start_level, (0, 0, 0)), (start_level, (0, 1, 1) if not start_level else (1, 0, 1)),
               (start_level, (0, 0, 0))]
        for level, want in seq:
            got = pulses(vm.step(ids, {g: level}))
            check(f"gate -> {int(level)}: (rising, falling, both) = {want}", got == want, str(got))
    vm.reset()

    # two enables
    p = Prog()
    g1, g2 = p.b(False), p.b(False)
    ea, eall = p.b(), p.b()
    p.block("EDGE", ens=[g1, g2], eno=ea, state=bytes([0, 0, 0, 0]), en_mode=EN_ANY)
    p.block("EDGE", ens=[g1, g2], eno=eall, state=bytes([0, 0, 0, 0]), en_mode=EN_ALL)
    vm.start(p, X_SCAN)
    ids = [ea, eall]
    vm.step(ids)
    got = []
    for w in ({g1: True}, {g2: True}, {g1: False, g2: False}, {g1: True, g2: True}):
        gg = vm.step(ids, w)
        got.append(tuple(vm.vals(gg, i) for i in ids))
    check("EDGE over two enables (rising): ANY pulses with the first, ALL when the second joins, both again from off, both together", got == [(1, 0), (0, 1), (0, 0), (1, 1)], str(got))
    vm.reset()

    # shape
    for name, kw in (("with no enable", dict(ens=[])), ("with an unknown edge type (3)", dict(state=bytes([3, 0, 0, 0]))), ("with an output", dict(outs=1)),
                     ("with an input", dict(ins=1))):
        p = Prog()
        g = p.b()
        outs = [p.b()] if kw.get("outs") else []
        ins = [p.b()] if kw.get("ins") else []
        p.block("EDGE", ins=ins, outs=outs, ens=kw.get("ens", [g]), state=kw.get("state", bytes(4)))
        r = vm.load(p)
        check(f"EDGE {name} -> ERR_VM_BLK_BAD_SHAPE", cause(r) == "ERR_VM_BLK_BAD_SHAPE", str(r))
    vm.reset()


@test("state/latch")
def t_latch(vm: Vm) -> None:
    p = Prog()
    s, r = p.b(False), p.b(False)
    sr, rs, nores = p.b(), p.b(), p.b()
    p.block("LATCH", ins=[r], ens=[s], eno=sr, state=bytes([0, 0, 0, 0]))
    p.block("LATCH", ins=[r], ens=[s], eno=rs, state=bytes([1, 0, 0, 0]))
    p.block("LATCH", ins=[None], ens=[s], eno=nores, state=bytes([0, 0, 0, 0]))
    if not loaded(vm, p):
        return
    ids = [sr, rs, nores]
    steps = [("idle", {}, (0, 0, 0)), ("set", {s: True}, (1, 1, 1)), ("set released: the latch holds", {s: False}, (1, 1, 1)), ("still held a pass later", {}, (1, 1, 1)),
             ("reset", {r: True}, (0, 0, 1)), ("reset released: stays clear", {r: False}, (0, 0, 1)), ("set and reset together", {s: True, r: True}, (1, 0, 1)),
             ("set off, reset on: SR clears", {s: False}, (0, 0, 1)), ("reset off", {r: False}, (0, 0, 1)), ("set again", {s: True}, (1, 1, 1)),
             ("reset while set (the set is still on)", {r: True}, (1, 0, 1))]
    for name, w, want in steps:
        g = vm.step(ids, w)
        got = tuple(vm.vals(g, i) for i in ids)
        check(f"LATCH {name}: (set-dominant, reset-dominant, no reset pin) = {want}", got == want, str(got))
    vm.reset()
    for name, kw in (("with no enable", dict(ens=[])), ("with an unknown mode (2)", dict(state=bytes([2, 0, 0, 0]))), ("with two inputs", dict(ins=2)),
                     ("with an output", dict(outs=1))):
        p = Prog()
        a = p.b()
        p.block("LATCH", ins=[a] * kw.get("ins", 0), outs=[p.b()] if kw.get("outs") else [], ens=kw.get("ens", [a]), state=kw.get("state", bytes(4)))
        r = vm.load(p)
        check(f"LATCH {name} -> ERR_VM_BLK_BAD_SHAPE", cause(r) == "ERR_VM_BLK_BAD_SHAPE", str(r))
    vm.reset()


# =====================================================================================================================
# TIMER and PERIODIC (real time)
# =====================================================================================================================


def timer_state(mode: int, pt: int, unit: int = 0, flags: int = 0) -> bytes:
    """vm_block_timer_data_t: mode, flags, time_base, pad, pad u32, pt u32, 4 pad, start_ms u64 @16, elapsed u32 @24."""
    return struct.pack("<BBBBII4xQI4x", mode, flags, unit, 0, 0, pt, 0, 0)


@test("time/timer")
def t_timer(vm: Vm) -> None:
    assert len(timer_state(0, 1)) == 32
    p = Prog()
    g = p.b(False)
    names = ["TON", "TOF", "TP", "TON_INV", "TOF_INV", "TP_INV"]
    qs, ets = {}, {}
    for mode, nm in enumerate(names):
        q, et = p.b(), p.u32()
        p.block("TIMER", outs=[et], ens=[g], eno=q, state=timer_state(mode, 300))
        qs[nm], ets[nm] = q, et
    if not loaded(vm, p, X_NORMAL):
        return
    time.sleep(0.4)
    ids = list(qs.values())
    init = vm.snap(ids)
    got = {nm: vm.vals(init, qs[nm]) for nm in names}
    check("with the gate closed: TON/TOF/TP are off and their inverted twins on", got == {"TON": 0, "TOF": 0, "TP": 0, "TON_INV": 1, "TOF_INV": 1, "TP_INV": 1}, str(got))
    t = vm.link.mark()
    vm.sub(ids)
    time.sleep(0.2)
    tw = time.time()
    vm.write(g, True)
    time.sleep(1.4)
    first = {nm: vm.wait_value(tw - 0.01, qs[nm], 1 if i < 3 else 0, 0.1) for i, nm in enumerate(names)}
    check("gate on: TON turns on after ~300 ms", first["TON"] is not None and 0.2 <= first["TON"] - tw <= 0.9, f"{None if first['TON'] is None else first['TON'] - tw}")
    check("gate on: TOF is on at once", first["TOF"] is not None and first["TOF"] - tw <= 0.4, f"{None if first['TOF'] is None else first['TOF'] - tw}")
    check("gate on: TP starts its pulse at once", first["TP"] is not None and first["TP"] - tw <= 0.4, f"{None if first['TP'] is None else first['TP'] - tw}")
    off = vm.wait_value(tw - 0.01, qs["TP"], 0, 0.1)
    # the pulse ends ~300 ms after it started although the gate is still on
    tp_off = [ts for ts, raw in vm.history(tw, qs["TP"]) if dec(T_B, raw) == 0 and ts > first["TP"]]
    check("TP's pulse ends by itself after ~300 ms (the gate is still on)", tp_off and 0.2 <= tp_off[0] - first["TP"] <= 0.7, f"{tp_off[:1]} {first['TP']}")
    check("the inverted twins mirror them: TON_INV falls when TON rises", vm.wait_value(tw - 0.01, qs["TON_INV"], 0, 0.1) is not None)
    snap = vm.snap(ids)
    check("steady state with the gate on: TON on, TOF on, TP off", (vm.vals(snap, qs["TON"]), vm.vals(snap, qs["TOF"]), vm.vals(snap, qs["TP"])) == (1, 1, 0), str(snap))

    tw = time.time()
    vm.write(g, False)
    time.sleep(0.15)
    ton_off = vm.wait_value(tw - 0.01, qs["TON"], 0, 0.1)
    check("gate off: TON drops at once", ton_off is not None and ton_off - tw <= 0.4, f"{None if ton_off is None else ton_off - tw}")
    time.sleep(0.8)
    tof_off = [ts for ts, raw in vm.history(tw - 0.01, qs["TOF"]) if dec(T_B, raw) == 0]
    check("... and drops ~300 ms after the gate", tof_off and 0.2 <= tof_off[0] - tw <= 0.9, f"{[x - tw for x in tof_off[:1]]}")
    vm.reset()

    # unit and preset pin, elapsed time
    p = Prog()
    g = p.b(False)
    pt = p.u32(400)
    q1, et1 = p.b(), p.u32()
    q2, et2 = p.b(), p.u32()
    p.block("TIMER", ins=[pt], outs=[et1], ens=[g], eno=q1, state=timer_state(0, 99999))
    p.block("TIMER", outs=[et2], ens=[g], eno=q2, state=timer_state(0, 1, unit=1))
    if not loaded(vm, p, X_NORMAL, "a timer with its preset on a pin, and one in seconds"):
        return
    # only the Q objects are watched while timing: an elapsed-time output changes on every pass and floods the link,
    # which delays the arrival times on the host by seconds
    vm.sub([q1, q2])
    time.sleep(0.2)
    tw = time.time()
    vm.write(g, True)
    time.sleep(1.6)
    t1 = vm.wait_value(tw - 0.01, q1, 1, 0.1)
    check("TON with the preset on a pin (400 ms) turns on after ~400 ms (not the 99999 of its state)", t1 is not None and 0.3 <= t1 - tw <= 0.9, f"{None if t1 is None else t1 - tw}")
    t2 = vm.wait_value(tw - 0.01, q2, 1, 0.1)
    check("TON of 1 second (time base seconds): on after ~1 s", t2 is not None and 0.8 <= t2 - tw <= 1.5, f"{None if t2 is None else t2 - tw}")
    check("... and not before 0.8 s", t2 is not None and t2 - tw >= 0.8)
    g2 = vm.snap([et1, et2])
    check("the elapsed time stops at the preset, in the unit of the timer (400 ms, 1 s)", (vm.vals(g2, et1), vm.vals(g2, et2)) == (400, 1), str((vm.vals(g2, et1), vm.vals(g2, et2))))
    vm.write(g, False)
    time.sleep(0.3)
    g2 = vm.snap([et1, et2, q1, q2])
    check("the gate closed: the elapsed time and Q fall back to 0", (vm.vals(g2, et1), vm.vals(g2, et2), vm.vals(g2, q1), vm.vals(g2, q2)) == (0, 0, 0, 0), str(g2))
    vm.reset()

    # program time: a pause is not counted
    p = Prog()
    g, q, et = p.b(False), p.b(), p.u32()
    p.block("TIMER", outs=[et], ens=[g], eno=q, state=timer_state(0, 1000))
    if not loaded(vm, p, X_NORMAL, "a 1 s timer"):
        return
    vm.sub([q])
    time.sleep(0.2)
    vm.write(g, True)
    time.sleep(0.35)
    vm.exec(X_PAUSE)
    time.sleep(1.6)
    vm.exec(X_RESUME)
    t_res = time.time()
    time.sleep(1.8)
    t_on = vm.wait_value(t_res - 0.01, q, 1, 0.1)
    check("a pause is not program time: 1.6 s paused, and the 1 s timer turns on ~0.65 s after the resume (not at once)", t_on is not None and 0.45 <= t_on - t_res <= 1.2, f"{None if t_on is None else t_on - t_res}")
    vm.reset()

    # shape
    for name, st in (("an unknown mode (6)", timer_state(6, 100)), ("an unknown unit (4)", timer_state(0, 100, unit=4)), ("a state of 31 bytes", timer_state(0, 100)[:31])):
        p = Prog()
        p.block("TIMER", ens=[p.b()], state=st)
        r = vm.load(p)
        check(f"TIMER with {name} -> ERR_VM_BLK_BAD_SHAPE", cause(r) == "ERR_VM_BLK_BAD_SHAPE", str(r))
    p = Prog()
    p.block("TIMER", ins=[p.u32(1), p.u32(1)], state=timer_state(0, 100))
    check("TIMER with two inputs -> refused", cause(vm.load(p)) == "ERR_VM_BLK_BAD_SHAPE")
    p = Prog()
    p.block("TIMER", state=timer_state(0, 100))
    check("TIMER with no enable loads (always running)", vm.load(p) is None)
    vm.reset()


def periodic_state(period: int, unit: int = 0) -> bytes:
    return struct.pack("<IBBHQ", period, unit, 0, 0, 0)


def ticks(vm: Vm, t: float, oid: int) -> list[float]:
    """Arrival times of the ticks (ENO records that hold 1) since t."""
    return [ts for ts, raw in vm.history(t, oid) if dec(T_B, raw) == 1]


@test("time/periodic")
def t_periodic(vm: Vm) -> None:
    p = Prog()
    g = p.b(False)
    q100, q_pin, q_s = p.b(), p.b(), p.b()
    per = p.u32(100)
    p.block("PERIODIC", ens=[g], eno=q100, state=periodic_state(100))
    p.block("PERIODIC", ins=[per], ens=[g], eno=q_pin, state=periodic_state(7))
    p.block("PERIODIC", ens=[g], eno=q_s, state=periodic_state(1, unit=1))
    if not loaded(vm, p, X_NORMAL):
        return
    vm.sub([q100, q_pin, q_s])
    time.sleep(0.5)
    check("a closed gate: no ticks in 0.5 s", not ticks(vm, vm.link.mark() - 0.5, q100) and not ticks(vm, vm.link.mark() - 0.5, q_s))
    tw = time.time()
    vm.write(g, True)
    time.sleep(1.05)
    n = ticks(vm, tw, q100)
    check("100 ms period, 1.05 s open: about 10 ticks", 8 <= len(n) <= 12, f"{len(n)}")
    check("the first tick comes at once (on the pass it is enabled)", n and n[0] - tw < 0.35, f"{n[:1] and n[0] - tw}")
    gaps = [b - a for a, b in zip(n, n[1:])]
    check("... then evenly (every gap within 50..180 ms)", gaps and all(0.05 <= x <= 0.18 for x in gaps), str([round(x, 3) for x in gaps]))
    s = ticks(vm, tw, q_s)
    check("1 s period (time base seconds): a tick at once and the next one ~1 s later", len(s) == 2 and 0.8 <= s[1] - s[0] <= 1.3, f"{[round(x - tw, 2) for x in s]}")

    # period from the pin
    tp = ticks(vm, tw, q_pin)
    check("the period pin (100) overrides the constant (7 ms): about 10 ticks", 8 <= len(tp) <= 12, f"{len(tp)}")
    tw2 = time.time()
    vm.write(per, 50)
    time.sleep(1.0)
    n50 = ticks(vm, tw2, q_pin)
    check("the pin changed to 50 ms: about 20 ticks a second", 16 <= len(n50) <= 24, f"{len(n50)}")
    tw3 = time.time()
    vm.write(per, 0)
    time.sleep(0.6)
    check("the pin at 0 pauses the ticking", len(ticks(vm, tw3 + 0.15, q_pin)) == 0, f"{len(ticks(vm, tw3 + 0.15, q_pin))}")
    tw4 = time.time()
    vm.write(per, 100)
    time.sleep(0.35)
    check("... and back at 100 it ticks again at once", ticks(vm, tw4, q_pin) and ticks(vm, tw4, q_pin)[0] - tw4 < 0.35)

    # a gate that closes and opens again restarts the phase
    vm.write(g, False)
    time.sleep(0.35)
    tw5 = time.time()
    vm.write(g, True)
    time.sleep(0.25)
    n = ticks(vm, tw5, q100)
    check("closing and opening the gate restarts the rhythm with a tick at once", n and n[0] - tw5 < 0.3, str(n))
    vm.write(g, False)
    time.sleep(0.4)
    check("the closed gate stops it", not ticks(vm, vm.link.mark() - 0.3, q100))
    vm.reset()

    # a period shorter than a pass
    p = Prog()
    q = p.b()
    p.block("PERIODIC", eno=q, state=periodic_state(1))
    vm.start(p, X_NORMAL)
    t = vm.link.mark()
    vm.sub([q])
    time.sleep(1.2)
    over = err_nodes(vm.link, t, "ERR_VM_PERIODIC_OVERRUN")
    check("a 1 ms period (shorter than a pass) drops ticks and says so once per episode", 1 <= len(over) <= 2, f"{len(over)} reports")
    check("... and ENO then stays on (a tick every pass: the value no longer changes, so telemetry shows it once)", vm.vals(vm.snap([q]), q) == 1)
    vm.reset()

    # shape
    for name, kw in (("a period of 0 with no pin", dict(state=periodic_state(0))), ("an unknown time base (4)", dict(state=periodic_state(5, 4))),
                     ("two inputs", dict(ins=2)), ("an output", dict(outs=1))):
        p = Prog()
        a = p.u32(1)
        p.block("PERIODIC", ins=[a] * kw.get("ins", 0), outs=[p.b()] if kw.get("outs") else [], state=kw.get("state", periodic_state(10)))
        r = vm.load(p)
        check(f"PERIODIC with {name} -> ERR_VM_BLK_BAD_SHAPE", cause(r) == "ERR_VM_BLK_BAD_SHAPE", str(r))
    p = Prog()
    p.block("PERIODIC", ins=[p.u32(5)], state=periodic_state(0))
    check("PERIODIC with a period pin and a constant of 0 loads", vm.load(p) is None)
    vm.reset()


# =====================================================================================================================
# ACTION (records of sys_actions)
# =====================================================================================================================

KEY_ACTIONS = 0x06
A_RUN, A_BEGIN, A_WRITE, A_COMMIT, A_REMOVE = 0x01, 0x04, 0x05, 0x06, 0x0A


def upload_record(link: Link, rid: int, *frames: bytes) -> Response | None:
    """A record of the store (SYS_ACTIONS.MD): the frames as [u16 len][keys + payload], BEGIN / WRITE / COMMIT."""
    img = b"".join(struct.pack("<H", len(f)) + f for f in frames)
    req = lambda op, pl: link.request(bytes([KEY_ACTIONS, op]), pl)  # noqa: E731
    r = req(A_BEGIN, struct.pack("<IH", rid, len(img)))
    if r is None or not r.ok:
        return r
    for off in range(0, len(img), 64):
        r = req(A_WRITE, struct.pack("<H", off) + img[off:off + 64])
        if r is None or not r.ok:
            return r
    return req(A_COMMIT, struct.pack("<I", zlib.crc32(img)))


def vm_write_frame(oid: int, raw: bytes, start: int = 0) -> bytes:
    return bytes([KEY_VM, OP_SET_DATA, 1]) + struct.pack("<HHH", oid, start, len(raw)) + raw


@test("action/records")
def t_action(vm: Vm) -> None:
    for rid in (7, 8, 9):
        vm.link.request(bytes([KEY_ACTIONS, A_REMOVE]), u32(rid))
    for rid, val in ((7, 42.0), (8, 43.0), (9, 44.0)):
        # the records write object 0 of the programs below (their `target`) through the VM's own SET_DATA command
        r = upload_record(vm.link, rid, vm_write_frame(0, f32(val)))
        check(f"record {rid} uploaded (sets object 0 to {val})", ok(r), str(r))

    def target_is(p: Prog, target: int, want: float, timeout: float = 2.0) -> bool:
        end = time.time() + timeout
        while time.time() < end:
            if vm.read(target) == want:
                return True
            time.sleep(0.1)
        return False

    # a gate starts record 7 (the id from the state)
    p = Prog()
    target, gate, e = p.f(0.0), p.b(False), p.b()
    p.block("ACTION", ens=[gate], eno=e, state=u32(7) + bytes(4))
    if not loaded(vm, p, X_NORMAL):
        return
    time.sleep(0.3)
    check("a gate that never opened: no record ran", vm.read(target) == 0.0)
    vm.write(gate, True)
    check("the gate rises: record 7 runs (the target becomes 42)", target_is(p, target, 42.0))
    vm.write(target, 0.0)
    time.sleep(0.7)
    check("a gate that stays open does not fire again", vm.read(target) == 0.0)
    vm.write(gate, False)
    time.sleep(0.2)
    vm.write(gate, True)
    check("closing and opening the gate fires it again", target_is(p, target, 42.0))
    vm.reset()

    # the id from the pin replaces the state's
    p = Prog()
    target, gate, idpin, e = p.f(0.0), p.b(False), p.u32(8), p.b()
    p.block("ACTION", ins=[idpin], ens=[gate], eno=e, state=u32(7) + bytes(4))
    loaded(vm, p, X_NORMAL, "a program with the id on a pin")
    vm.write(gate, True)
    check("the pin's id (8) runs record 8 although the state says 7", target_is(p, target, 43.0))
    vm.write(gate, False)
    vm.write(idpin, 9)
    time.sleep(0.2)
    vm.write(target, 0.0)
    vm.write(gate, True)
    check("the id on the pin is read when the block fires (9)", target_is(p, target, 44.0))
    vm.reset()

    # no enable: once, when the program starts
    p = Prog()
    target, e = p.f(0.0), p.b()
    p.block("ACTION", eno=e, state=u32(9) + bytes(4))
    loaded(vm, p, X_NORMAL, "a block with no enable")
    check("a block with no enable fires once when the program starts (record 9)", target_is(p, target, 44.0))
    vm.write(target, 0.0)
    time.sleep(0.6)
    check("... and only once", vm.read(target) == 0.0)
    vm.reset()

    # a missing record is reported when the request runs
    p = Prog()
    gate, e = p.b(False), p.b()
    p.block("ACTION", ens=[gate], eno=e, state=u32(0x00F0F0F0) + bytes(4))
    vm.start(p, X_NORMAL)
    t = vm.link.mark()
    vm.write(gate, True)
    n = wait_err(vm.link, t, "ERR_ACTION_NOT_FOUND", 2.0)
    check("a request for a record that does not exist is reported (ERR_ACTION_NOT_FOUND)", n is not None, str(err_names(vm.link, t)))
    vm.reset()
    for rid in (7, 8, 9):
        vm.link.request(bytes([KEY_ACTIONS, A_REMOVE]), u32(rid))

    # shape
    p = Prog()
    p.block("ACTION", state=u32(0) + bytes(4))
    check("ACTION with no id pin and id 0 -> ERR_VM_BLK_BAD_SHAPE", cause(vm.load(p)) == "ERR_VM_BLK_BAD_SHAPE")
    p = Prog()
    p.block("ACTION", ins=[p.u32(5)], state=u32(0) + bytes(4))
    check("ACTION with an id pin and id 0 in the state loads", vm.load(p) is None)
    p = Prog()
    p.block("ACTION", ins=[p.u32(5), p.u32(6)], state=u32(1) + bytes(4))
    check("ACTION with two inputs -> ERR_VM_BLK_BAD_SHAPE", cause(vm.load(p)) == "ERR_VM_BLK_BAD_SHAPE")
    p = Prog()
    p.block("ACTION", outs=[p.b()], state=u32(1) + bytes(4))
    check("ACTION with an output -> ERR_VM_BLK_BAD_SHAPE", cause(vm.load(p)) == "ERR_VM_BLK_BAD_SHAPE")
    vm.reset()
