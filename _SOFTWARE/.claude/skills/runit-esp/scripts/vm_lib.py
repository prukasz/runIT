"""Host library of the VM suite (vm_suite.py): program builder, loader, telemetry reader, check bookkeeping.

Everything here talks to the board through router_link.Link (the ROUTER_KEY_VM = 0x0B ops of VM_CORE.MD). A request is
[0x0B][dir][op][payload]: callers give [0x0B][op] and router_link.frame adds the direction byte from the descriptors (every
VM op is a write but STATUS, a read). Telemetry frames carry no direction byte: [0x0B][op][n] records. The numbers that the
firmware owns (block types, expression opcodes) are read from the firmware headers, so the suite follows the code instead
of a copy of it.
"""

from __future__ import annotations

import math
import re
import struct
import time
from dataclasses import dataclass
from typing import Iterable, Sequence

from router_link import ESP, READ, STREAM_ERRORS, STREAM_TELEMETRY, WRITE, Link, Response, frame  # noqa: F401

KEY_VM = 0x0B
OP_RESET, OP_OPEN, OP_ADD_OBJECTS, OP_DATA, OP_ADD_ACCESSORS, OP_ADD_BLOCK, OP_SUBSCRIBE, OP_EXEC, OP_SPEED, OP_STATUS = range(1, 11)
(X_SCAN, X_ONCE, X_BLOCK, X_NEXT, X_RESTART, X_NORMAL, X_PAUSE, X_RESUME, X_RESET, X_ACK, X_RETAIN_CLEAR) = range(11)

T_PTR, T_U8, T_U32, T_I32, T_F, T_B, T_STR = range(1, 8)
WIDTH = {T_PTR: 2, T_U8: 1, T_U32: 4, T_I32: 4, T_F: 4, T_B: 1, T_STR: 1}  # element bytes on the wire
FMT = {T_U8: "<B", T_U32: "<I", T_I32: "<i", T_F: "<f", T_B: "<B", T_STR: "<B", T_PTR: "<H"}

F_MUT, F_RES, F_RET, F_PROT = 0x01, 0x04, 0x10, 0x40  # wire flags (vm_wire.h)
NO_ID = 0xFFFF
EN_ANY, EN_ALL = 0, 1
ON_ERR_STOP, ON_ERR_CONT = 0, 1
DYN_BIT = 0x8000

FRAME_BUDGET = 400  # bytes of payload per load frame (the router takes 512)

# ---- firmware headers --------------------------------------------------------------------------------------------

_BLOCKS = ESP / "components/vm/vm_core/block/vm_block_ids.h"
BLK = {m.group(1): int(m.group(2)) for m in re.finditer(r"#define\s+VM_BLK_(\w+)\s+(\d+)", _BLOCKS.read_text())}
BLK.pop("NONE", None)


def _enum(text: str, name: str) -> dict[str, int]:
    text = re.sub(r"//[^\n]*", "", text)  # the annotations hold braces
    body = re.search(r"typedef enum " + name + r"\s*\{(.*?)\}", text, re.S).group(1)
    out, n = {}, 0
    for item in body.split(","):
        m = re.match(r"\s*(VM_\w+)\s*(?:=\s*(\d+))?\s*$", item)
        if m:
            n = int(m.group(2)) if m.group(2) else n
            out[m.group(1)] = n
            n += 1
    return out


_EXPR_H = (ESP / "components/vm/vm_blocks/core/expr_core/vm_expr_core.h").read_text()
OPF = {k[len("VM_EXPR_"):]: v for k, v in _enum(_EXPR_H, "vm_expr_op_e").items()}
OPB = {k[len("VM_BIT_"):]: v for k, v in _enum(_EXPR_H, "vm_bit_op_e").items()}

# ---- values ------------------------------------------------------------------------------------------------------


def f32(x: float) -> bytes:
    return struct.pack("<f", x)


def u32(x: int) -> bytes:
    return struct.pack("<I", x & 0xFFFFFFFF)


def i32(x: int) -> bytes:
    return struct.pack("<i", x)


def u16(x: int) -> bytes:
    return struct.pack("<H", x)


def as_f32(x: float) -> float:
    """x rounded to a float32, as the board holds it."""
    return struct.unpack("<f", struct.pack("<f", x))[0]


def enc(t: int, v) -> bytes:
    """One value (or a list) as the bytes of an object of type t."""
    if isinstance(v, (bytes, bytearray)):
        return bytes(v)
    if isinstance(v, (list, tuple)):
        return b"".join(enc(t, x) for x in v)
    if t == T_B:
        return bytes([1 if v else 0])
    return struct.pack(FMT[t], v)


def dec(t: int, b: bytes | None):
    """The value of an object (a scalar, or a list for several elements); None when b is None."""
    if b is None:
        return None
    w = WIDTH[t]
    vals = [struct.unpack_from(FMT[t], b, i)[0] for i in range(0, len(b) - w + 1, w)]
    return vals[0] if len(vals) == 1 else vals


def close(a, b, rel: float = 2e-4, ab: float = 1e-5) -> bool:
    """Float compare with a tolerance (the board computes in float32)."""
    if a is None or b is None:
        return False
    if isinstance(a, list) or isinstance(b, list):
        return isinstance(a, list) and isinstance(b, list) and len(a) == len(b) and all(close(x, y, rel, ab) for x, y in zip(a, b))
    if math.isnan(b):
        return math.isnan(a)
    return abs(a - b) <= max(ab, rel * abs(b))


# ---- test registry ---------------------------------------------------------------------------------------------

TESTS: list[tuple[str, bool, object]] = []


def test(name: str, hw: bool = False):
    """Registers a test function(vm). hw=True: it touches a pin or reads a chip (skipped by --skip-hw)."""

    def deco(fn):
        TESTS.append((name, hw, fn))
        return fn

    return deco


# ---- check bookkeeping -------------------------------------------------------------------------------------------

RESULTS: list[tuple[str, str, bool, str]] = []
_section = ["-"]


def section(name: str) -> None:
    _section[0] = name
    print(f"-- {name}", flush=True)


def check(name: str, ok: bool, detail: str = "") -> bool:
    ok = bool(ok)
    RESULTS.append((_section[0], name, ok, detail))
    print(f"{'PASS' if ok else 'FAIL'}  {name}" + (f"  -- {detail}" if detail and not ok else ""), flush=True)
    return ok


# ---- responses and errors ----------------------------------------------------------------------------------------


def cause(r: Response | None) -> str:
    """Root cause of an error answer ('' for OK, no answer or an answer that cannot be decoded)."""
    try:
        return r.record.cause.name if r and r.record and r.record.cause else ""
    except (IndexError, struct.error):
        return ""


def chain(r: Response | None) -> list[str]:
    try:
        return r.record.names() if r and r.record else []
    except (IndexError, struct.error):
        return []


def ok(r: Response | None) -> bool:
    return r is not None and r.ok


def err_nodes(link: Link, t: float, name: str | None = None):
    """Nodes of the error records the board streamed since t (all of them, or only those called `name`)."""
    out = []
    for rec in link.error_records_since(t):
        out.extend(n for n in rec.nodes if name is None or n.name == name)
    return out


def err_names(link: Link, t: float) -> list[str]:
    return [n.name for n in err_nodes(link, t)]


def wait_err(link: Link, t: float, name: str, timeout: float = 2.0):
    """The first error node `name` streamed since t, waiting for it; None on timeout."""
    end = time.time() + timeout
    while True:
        nodes = err_nodes(link, t, name)
        if nodes:
            return nodes[0]
        if time.time() >= end:
            return None
        time.sleep(0.05)


# ---- telemetry ---------------------------------------------------------------------------------------------------


def parse_frame(frame: bytes):
    """A telemetry frame [0x0B][op][n] records (no direction byte: it is not a request) -> [('desc', id, size, type,
    name_len, flags, name) | ('val', id, start, data)], or None when it is not a well-formed VM frame."""
    if len(frame) < 3 or frame[0] != KEY_VM or frame[1] not in (OP_ADD_OBJECTS, OP_DATA):
        return None
    op, n, off, recs = frame[1], frame[2], 3, []
    for _ in range(n):
        if op == OP_ADD_OBJECTS:
            if off + 6 > len(frame):
                return None
            oid, size, tn, fl = struct.unpack_from("<HHBB", frame, off)
            off += 6
            nl = tn >> 4
            if off + nl > len(frame):
                return None
            recs.append(("desc", oid, size, tn & 0x0F, nl, fl, frame[off:off + nl]))
            off += nl
        else:
            if off + 6 > len(frame):
                return None
            oid, start, ln = struct.unpack_from("<HHH", frame, off)
            off += 6
            if off + ln > len(frame):
                return None
            recs.append(("val", oid, start, frame[off:off + ln]))
            off += ln
    return recs if off == len(frame) else None


def tele_frames(link: Link, t: float) -> list[tuple[float, bytes]]:
    """(arrival time, payload) of every telemetry frame since t."""
    with link._lock:
        return [(ts, p) for (ts, s, p) in link.streams if s == STREAM_TELEMETRY and ts >= t]


# ---- program builder ---------------------------------------------------------------------------------------------


@dataclass(frozen=True)
class Ref:
    """An accessor id (the result of Prog.acc)."""

    id: int


class Prog:
    """A program in the making: objects, accessors, blocks; load() uploads it.

    Block pins take an object id (a whole-object accessor is made for it), a Ref (an accessor from acc()) or None
    (an unwired input). Outputs and the ENO are object ids.
    """

    def __init__(self, size: int = 32768):
        self.size = size
        self.objs: list[tuple[int, int, int, int, bytes]] = []  # id, type, payload bytes, flags, name
        self.types: dict[int, tuple[int, int]] = {}             # id -> (type, element count)
        self.init: list[tuple[int, int, bytes]] = []            # (id, start, wire bytes)
        self.accs: list[tuple[int, int, tuple]] = []
        self.blks: list[bytes] = []
        self._plain: dict[int, int] = {}

    # objects
    def obj(self, t: int, n: int = 1, flags: int = F_MUT | F_RES, name: bytes | str = b"", init=None) -> int:
        oid = len(self.objs)
        name = name.encode() if isinstance(name, str) else name
        self.objs.append((oid, t, n * (4 if t == T_PTR else WIDTH[t]), flags, name))
        self.types[oid] = (t, n)
        if init is not None:
            self.init.append((oid, 0, enc(t, init)))
        return oid

    def f(self, v=None, **kw):
        return self.obj(T_F, init=v, **kw)

    def u32(self, v=None, **kw):
        return self.obj(T_U32, init=v, **kw)

    def i32(self, v=None, **kw):
        return self.obj(T_I32, init=v, **kw)

    def u8(self, v=None, **kw):
        return self.obj(T_U8, init=v, **kw)

    def b(self, v=None, **kw):
        return self.obj(T_B, init=v, **kw)

    def ptr(self, n: int = 1, children: Sequence[int] | None = None, **kw):
        return self.obj(T_PTR, n, init=None if children is None else b"".join(u16(c) for c in children), **kw)

    def val(self, oid: int, v, start: int = 0) -> None:
        self.init.append((oid, start, enc(self.types[oid][0], v)))

    # accessors
    def acc(self, root: int, steps: Iterable[tuple] = ()) -> Ref:
        aid = len(self.accs)
        self.accs.append((aid, root, tuple(steps)))
        return Ref(aid)

    def pin(self, x) -> int:
        if x is None:
            return NO_ID
        if isinstance(x, Ref):
            return x.id
        if x not in self._plain:
            self._plain[x] = self.acc(x).id
        return self._plain[x]

    # blocks
    def block(self, btype, ins=(), outs=(), ens=(), state: bytes = b"", eno: int | None = None, en_mode: int = EN_ANY,
              on_error: int = ON_ERR_CONT) -> int:
        bid = len(self.blks)
        self.blks.append(self.block_frame(bid, btype, ins, outs, ens, state, eno, en_mode, on_error))
        return bid

    def block_frame(self, bid, btype, ins=(), outs=(), ens=(), state=b"", eno=None, en_mode=EN_ANY, on_error=ON_ERR_CONT,
                    counts: tuple[int, int, int] | None = None) -> bytes:
        """The ADD_BLOCK payload. `counts` overrides (in, out, en) counts in the head, to build malformed frames."""
        btype = BLK[btype] if isinstance(btype, str) else btype
        pins_in = [self.pin(i) for i in ins]
        pins_en = [self.pin(e) for e in ens]
        ci, co, ce = counts if counts else (len(pins_in), len(outs), len(pins_en))
        head = struct.pack("<HHBBBBBBHH", bid, bid, btype, ci, co, ce, en_mode, on_error, len(state), NO_ID if eno is None else eno)
        pins = b"".join(u16(p) for p in (*pins_in, *outs, *pins_en))
        return head + pins + state

    # upload
    def frames(self) -> list[tuple[int, bytes]]:
        """(op, payload) of every load frame, OPEN first."""
        out = [(OP_OPEN, struct.pack("<HHHI", len(self.objs), len(self.accs), len(self.blks), self.size))]

        def pack(op: int, recs: list[bytes]) -> None:
            cur, size = [], 0
            for rec in recs:
                if cur and (size + len(rec) > FRAME_BUDGET or len(cur) == 255):
                    out.append((op, bytes([len(cur)]) + b"".join(cur)))
                    cur, size = [], 0
                cur.append(rec)
                size += len(rec)
            if cur:
                out.append((op, bytes([len(cur)]) + b"".join(cur)))

        pack(OP_ADD_OBJECTS, [struct.pack("<HHBB", oid, size, t | (len(nm) << 4), fl) + nm for (oid, t, size, fl, nm) in self.objs])
        data = []
        for oid, start, raw in self.init:
            w = WIDTH[self.types[oid][0]]
            step = (192 // w) * w
            for off in range(0, max(len(raw), 1), step):
                chunk = raw[off:off + step]
                data.append(struct.pack("<HHH", oid, start + off // w, len(chunk)) + chunk)
        pack(OP_DATA, data)
        accs = []
        for aid, root, steps in self.accs:
            body = b""
            for st in steps:
                if st[0] == "lit":
                    body += struct.pack("<BI", 0, st[1])
                elif st[0] == "ref":
                    body += struct.pack("<BH", 1, st[1].id if isinstance(st[1], Ref) else st[1])
                elif st[0] == "name":
                    nm = st[1].encode() if isinstance(st[1], str) else st[1]
                    body += struct.pack("<BB", 2, len(nm)) + nm
                else:
                    body += bytes(st[1:])  # ('raw', bytes...): a hand-made step
            accs.append(struct.pack("<HHBB", aid, root, len(steps), len(body)) + body)
        pack(OP_ADD_ACCESSORS, accs)
        for b in self.blks:
            out.append((OP_ADD_BLOCK, b))
        return out

    def load(self, link: Link) -> Response | None:
        """Upload; the first failing (or missing) answer, else None."""
        for op, payload in self.frames():
            r = link.request(bytes([KEY_VM, op]), payload, timeout=3.0)
            if r is None or not r.ok:
                return r if r is not None else Response(0, 255, b"", 0.0)  # no answer: status 255, not ok
        return None


# ---- the VM session ----------------------------------------------------------------------------------------------


class Vm:
    """Thin helpers over the ops; keeps the program that was loaded last (for the types of the objects)."""

    def __init__(self, link: Link):
        self.link = link
        self.p: Prog | None = None
        self.mode = X_RESET  # the last mode command that started the program
        self.dyn: dict[int, tuple[int, int]] = {}  # heap object id -> (type, element count), from describe records

    # ops
    def op(self, op: int, payload: bytes = b"", timeout: float = 2.0, dir: int | None = None,
           raw: bool = False) -> Response | None:
        """One VM op. The direction byte is the op's side (`dir` overrides it); `raw`: `op` is the bytes after the key
        as they go, the direction byte included (a test of the direction byte itself)."""
        if raw:
            return self.link.request(bytes([KEY_VM]) + bytes(op), payload, timeout=timeout, raw=True)
        return self.link.request(bytes([KEY_VM, op]), payload, timeout=timeout, dir=dir)

    def exec(self, cmd: int) -> Response | None:
        r = self.op(OP_EXEC, bytes([cmd]))
        if cmd in (X_SCAN, X_NORMAL, X_BLOCK, X_RESET) and ok(r):
            self.mode = cmd
        return r

    def reset(self) -> Response | None:
        return self.op(OP_RESET)

    def load(self, p: Prog) -> Response | None:
        self.p = p
        self.dyn = {}
        return p.load(self.link)

    def start(self, p: Prog, mode: int = X_NORMAL) -> Response | None:
        """load + exec; the first failing answer (or the exec's)."""
        r = self.load(p)
        return r if r is not None else self.exec(mode)

    def width(self, oid: int) -> int:
        if oid & DYN_BIT:
            return WIDTH[self.dyn[oid][0]] if oid in self.dyn else 1
        return WIDTH[self.p.types[oid][0]]

    # writes
    @staticmethod
    def data_rec(oid: int, raw: bytes, start: int = 0) -> bytes:
        return struct.pack("<HHH", oid, start, len(raw)) + raw

    def write_raw(self, *recs: bytes) -> Response | None:
        return self.op(OP_DATA, bytes([len(recs)]) + b"".join(recs))

    def write(self, oid: int, v, start: int = 0) -> Response | None:
        t = self.p.types[oid][0]
        return self.write_raw(self.data_rec(oid, enc(t, v), start))

    def writes(self, items: dict | Sequence) -> Response | None:
        """Several objects in one frame: {oid: value}."""
        pairs = items.items() if isinstance(items, dict) else items
        return self.write_raw(*(self.data_rec(o, enc(self.p.types[o][0], v)) for o, v in pairs))

    # reads: subscribe, then collect the snapshot
    def sub(self, ids: Sequence[int]) -> Response | None:
        return self.op(OP_SUBSCRIBE, bytes([len(ids)]) + b"".join(u16(i) for i in ids))

    def collect(self, t: float, want: Sequence[int], timeout: float = 2.0, quiet: float = 0.12) -> dict[int, bytes]:
        """Telemetry since t folded into {id: bytes} (the latest value of each, split objects reassembled), waiting
        until every id in `want` arrived and the stream was quiet for `quiet` seconds."""
        end, got, last = time.time() + timeout, {}, time.time()
        seen = 0
        while time.time() < end:
            frames = tele_frames(self.link, t)
            if len(frames) != seen:
                seen, last = len(frames), time.time()
                got = self._fold(frames)
            if all(w in got for w in want) and time.time() - last >= quiet:
                break
            time.sleep(0.03)
        return got

    def _fold(self, frames) -> dict[int, bytes]:
        got: dict[int, bytearray] = {}
        for _, f in frames:
            recs = parse_frame(f)
            if recs is None:
                continue
            for r in recs:
                if r[0] == "desc":
                    self.dyn[r[1]] = (r[3], r[2] // (4 if r[3] == T_PTR else WIDTH[r[3]]))
                else:
                    _, oid, start, data = r
                    w = self.width(oid)
                    buf = got.setdefault(oid, bytearray())
                    if len(buf) < start * w + len(data):
                        buf.extend(bytes(start * w + len(data) - len(buf)))
                    buf[start * w:start * w + len(data)] = data
        return {k: bytes(v) for k, v in got.items()}

    def snap(self, ids: Sequence[int], timeout: float = 2.0) -> dict[int, bytes]:
        """The current value of the objects (a fresh subscription: everything is sent once). For a running or a
        stopped VM; a paused or stepping one sends at the end of its next pass (use step)."""
        t = self.link.mark()
        self.sub(list(ids))
        return self.collect(t, ids, timeout)

    def snap_raw(self, ids: Sequence[int], timeout: float = 2.0) -> dict[int, bytes]:
        """Like snap, for objects that are not in the loaded Prog (raw bytes, one byte per element)."""
        t = self.link.mark()
        self.sub(list(ids))
        end, got = time.time() + timeout, {}
        while time.time() < end and not all(i in got for i in ids):
            for _, f in tele_frames(self.link, t):
                for r in parse_frame(f) or []:
                    if r[0] == "val":
                        got[r[1]] = r[3]
            time.sleep(0.03)
        return got

    def read(self, oid: int, timeout: float = 2.0):
        """The decoded current value of one object."""
        d = self.snap([oid], timeout)
        t = self.dyn[oid][0] if oid & DYN_BIT else self.p.types[oid][0]
        return dec(t, d.get(oid))

    def kick(self, per_pass: int = 36) -> None:
        """Write every initial value again as a runtime write. Values uploaded with the program are not fresh (they
        trigger nothing); a runtime write is, so this is what makes the blocks see their inputs. The writes wait in a
        ring (CONFIG_VM_OVERRIDE_BUF_SIZE bytes) that a pass empties, so a long list is sent in groups with a pass
        between them (a scan-mode VM is stepped, a running one gets a pause)."""
        flags = {o[0]: o[3] for o in self.p.objs}
        recs = [self.data_rec(oid, raw, start) for (oid, start, raw) in self.p.init
                if self.p.types[oid][0] != T_PTR and flags[oid] & F_MUT and not flags[oid] & F_PROT]
        for g in range(0, len(recs), per_pass):
            group = recs[g:g + per_pass]
            cur, size = [], 0
            for rec in group:
                if cur and size + len(rec) > FRAME_BUDGET:
                    self.write_raw(*cur)
                    cur, size = [], 0
                cur.append(rec)
                size += len(rec)
            if cur:
                self.write_raw(*cur)
            if g + per_pass < len(recs):
                if self.mode == X_SCAN:
                    self.op(OP_EXEC, bytes([X_ONCE]))
                else:
                    time.sleep(0.08)

    def step(self, ids: Sequence[int], writes: dict | Sequence = (), timeout: float = 2.0, kick: bool = False) -> dict[int, bytes]:
        """Scan mode: apply the writes, run exactly one pass (ONCE) and return the subscribed objects as they stand at
        its end. The VM must be in scan mode (start(..., X_SCAN)). kick=True first rewrites the initial values."""
        if kick:
            self.kick()
        if writes:
            self.writes(writes)
        t = self.link.mark()
        self.sub(list(ids))
        r = self.exec(X_ONCE)
        if not ok(r):
            return {}
        return self.collect(t, ids, timeout)

    def vals(self, d: dict[int, bytes], oid: int):
        t = self.dyn[oid][0] if oid & DYN_BIT else self.p.types[oid][0]
        return dec(t, d.get(oid))

    def history(self, t: float, oid: int) -> list[tuple[float, bytes]]:
        """(arrival time, bytes) of every whole-object record of `oid` in the telemetry since t."""
        w = self.width(oid)
        n = self.p.types[oid][1] if not oid & DYN_BIT else self.dyn[oid][1]
        out = []
        for ts, f in tele_frames(self.link, t):
            for r in parse_frame(f) or []:
                if r[0] == "val" and r[1] == oid and r[2] == 0 and len(r[3]) == w * n:
                    out.append((ts, r[3]))
        return out

    def wait_value(self, t: float, oid: int, want, timeout: float = 3.0):
        """Arrival time of the first telemetry record since t that holds `want` (decoded compare), else None."""
        end = time.time() + timeout
        ty = self.p.types[oid][0]
        while time.time() < end:
            for ts, raw in self.history(t, oid):
                v = dec(ty, raw)
                if v == want or (isinstance(want, float) and close(v, want)):
                    return ts
            time.sleep(0.03)
        return None
