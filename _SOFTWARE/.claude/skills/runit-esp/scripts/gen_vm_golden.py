"""Writes app/tests/backend/fixtures/vm-golden.json: the load frames the host suite's builder (vm_lib.Prog) makes for a sample
program, which app/tests/backend/vm.test.ts compares byte for byte with the app's `Program`. The suite is tested against the
board, so this ties the app's encoder to what the board accepts.

  C:\\Espressif\\tools\\python\\v6.1\\venv\\Scripts\\python.exe gen_vm_golden.py
"""
import json
from pathlib import Path

import vm_lib as v

OUT = Path(__file__).resolve().parents[4] / "app" / "tests" / "backend" / "fixtures" / "vm-golden.json"

p = v.Prog(size=20000)
a = p.f(1.5, name="a")
b = p.u32(7, name="b")
c = p.i32(-3)
d = p.obj(v.T_U8, 5, init=[1, 2, 3, 4, 5], name="bytes")
flag = p.b(True, name="flag", flags=v.F_MUT | v.F_RET)
big = p.obj(v.T_F, 150, init=[i * 0.5 for i in range(150)], name="big")
folder = p.ptr(2, children=[a, b], name="folder")
q = p.f(name="q")
eno = p.b()
acc_member = p.acc(folder, [("lit", 1)])
acc_name = p.acc(folder, [("name", "x"), ("lit", 2)])
acc_ref = p.acc(folder, [("ref", acc_member)])
p.block(v.BLK["EXPR"], ins=[a, b, None], outs=[q], ens=[flag], state=bytes(range(12)), eno=eno, en_mode=v.EN_ALL, on_error=v.ON_ERR_STOP)
p.block(v.BLK["IF"], ins=[acc_member, acc_name], outs=[], ens=[], state=b"")
p.block(v.BLK["EXPR"], ins=[acc_ref], outs=[q], state=bytes(40))
for i in range(30):  # enough small objects that the object records need a second frame
    p.u8(i, name=f"o{i}")

frames = [{"op": op, "payload": payload.hex()} for op, payload in p.frames()]
OUT.parent.mkdir(parents=True, exist_ok=True)
OUT.write_text(json.dumps({"blocks": {"EXPR": v.BLK["EXPR"], "IF": v.BLK["IF"]}, "frames": frames}, indent=1) + "\n", encoding="utf-8", newline="\n")
print(f"wrote {OUT}: {len(frames)} frames")
