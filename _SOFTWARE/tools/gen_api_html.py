"""API.html: every router op and device op (opcode, sides, what it is for), from runit-app/generated/.

Run from anywhere: python tools/gen_api_html.py [generated_dir] [out_file]
Defaults: runit-app/generated -> API.html at the root of _SOFTWARE.
"""
import json, glob, os, sys, html

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
GEN = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, "runit-app", "generated")
OUT = sys.argv[2] if len(sys.argv) > 2 else os.path.join(ROOT, "API.html")
E = html.escape


def load(p):
    with open(p, encoding="utf-8") as f:
        return json.load(f)


def fields_txt(spec):
    if not spec:
        return ""
    parts = []
    st = spec.get("struct")
    if spec.get("fields"):
        fs = ", ".join(f'{f["id"]}:{f.get("raw_type", f.get("type", "?"))}' for f in spec["fields"])
        parts.append(("[" if spec.get("array") else "") + fs + (" …]" if spec.get("array") else ""))
    elif spec.get("type"):
        parts.append(spec["type"])
    head = f'<code>{E(st)}</code> ' if st else ""
    desc = spec.get("description")
    body = E(" ".join(parts))
    return head + (f'<span class="f">{body}</span>' if body else "") + (f'<div class="sub">{E(desc)}</div>' if desc else "")


def side_cell(c, side):
    s = c.get(side)
    if s is None:
        return '<td class="na">—</td>'
    out = []
    if s.get("description") and s.get("description") != c.get("description"):
        out.append(f'<div>{E(s["description"])}</div>')
    if s.get("args"):
        out.append(f'<div class="io"><b>arg:</b> {fields_txt(s["args"])}</div>')
    if s.get("returns"):
        out.append(f'<div class="io"><b>&rarr;</b> {fields_txt(s["returns"])}</div>')
    return "<td>" + ("".join(out) or '<span class="sub">bez argumentów</span>') + "</td>"


def rows(cmds, opfmt):
    r = []
    for c in sorted(cmds, key=lambda c: c["op"]):
        sides = ("R" if "read" in c else "") + ("W" if "write" in c else "")
        badges = "".join(f'<span class="b {x}">{x}</span>' for x in sides)
        extra = ""
        if c.get("record"):
            extra += f' <span class="tag">rekord {E(c["record"])}</span>'
        if c.get("urgent"):
            extra += ' <span class="tag">urgent</span>'
        r.append(
            "<tr>"
            f'<td class="op">{opfmt(c["op"])}</td>'
            f'<td><b>{E(c["id"])}</b><div class="sub"><code>{E(c.get("symbol", ""))}</code></div></td>'
            f"<td>{badges}</td>"
            f'<td><b>{E(c.get("label", ""))}</b>{extra}<div>{E(c.get("description", ""))}</div></td>'
            f'{side_cell(c, "read")}{side_cell(c, "write")}'
            "</tr>"
        )
    return "".join(r)


THEAD = "<thead><tr><th>OP</th><th>Nazwa</th><th>R/W</th><th>Do czego</th><th>Read</th><th>Write</th></tr></thead>"

nav, sec = [], []

# ---- Routers
services = sorted((load(p) for p in glob.glob(os.path.join(GEN, "services", "*.json"))), key=lambda d: d["router_key"])
dev_contract = load(os.path.join(GEN, "contracts", "contract_device.json"))
nav.append('<div class="nh">Routery (root key)</div>')
router_list = services + [{"_device": True, "router_key": 5}]
router_list.sort(key=lambda d: d["router_key"])
for d in router_list:
    if d.get("_device"):
        aid = "r-DEVICE"
        nav.append(f'<a href="#{aid}"><span class="k">0x05</span> DEVICE</a>')
        sec.append(
            f'<section id="{aid}" class="card"><h3><span class="k">0x05</span> DEVICE — Devices</h3>'
            '<p>Ramka: <code>[0x05][dir][id u16][op u16][args]</code>. <code>op</code> = <code>(contract &lt;&lt; 8) | n</code>; '
            'operacje kontraktów i urządzeń są w sekcji <a href="#contracts">Kontrakty</a> i <a href="#devices">Urządzenia</a>. '
            'Poniżej kontrakt DEVICE (0x00xx), który ma każde urządzenie. <code>id = 0xFFFF</code> — wszystkie urządzenia (sweep).</p>'
            f'<table>{THEAD}<tbody>{rows(dev_contract["commands"], lambda o: f"0x{o:04X}")}</tbody></table></section>'
        )
        continue
    k = d["router_key"]
    aid = f'r-{d["id"]}'
    nav.append(f'<a href="#{aid}"><span class="k">0x{k:02X}</span> {E(d["id"])}</a>')
    sec.append(
        f'<section id="{aid}" class="card"><h3><span class="k">0x{k:02X}</span> {E(d["id"])} — {E(d.get("alias", ""))}</h3>'
        f'<p>{E(d.get("description", ""))}</p><p class="sub">Ramka: <code>[0x{k:02X}][dir][op u8][args]</code></p>'
        f'<table>{THEAD}<tbody>{rows(d["commands"], lambda o: f"0x{o:02X}")}</tbody></table></section>'
    )

router_html = "".join(sec)
sec = []

# ---- Contracts
contracts = [load(p) for p in glob.glob(os.path.join(GEN, "contracts", "*.json"))]
contracts = [c for c in contracts if c["id"] != "DEVICE"]
contracts.sort(key=lambda c: c["contract_id"])
used_by = {}
devices = sorted((load(p) for p in glob.glob(os.path.join(GEN, "devices", "*.json"))), key=lambda d: d["type_id"])
for dv in devices:
    for c in dv.get("contracts", []):
        used_by.setdefault(c, []).append(dv["alias"])


def events_html(evs, base):
    if not evs:
        return ""
    evs = [dict(e, event=e.get("event", i)) for i, e in enumerate(evs)]
    r = "".join(
        f'<tr><td class="op">0x{(base | e["event"]):04X}</td><td><b>{E(e["id"])}</b></td>'
        f'<td><b>{E(e.get("label", ""))}</b><div>{E(e.get("description", ""))}</div></td></tr>'
        for e in sorted(evs, key=lambda e: e["event"])
    )
    return f'<h4>Zdarzenia</h4><table class="ev"><thead><tr><th>Event ID</th><th>Nazwa</th><th>Opis</th></tr></thead><tbody>{r}</tbody></table>'


nav.append('<div class="nh" id="contracts-nav">Kontrakty urządzeń</div>')
all_c = [dev_contract] + contracts
for c in all_c:
    cid = c["contract_id"]
    aid = f'c-{c["id"]}'
    nav.append(f'<a href="#{aid}"><span class="k">0x{cid:02X}</span> {E(c["id"])}</a>')
    base = cid << 8
    who = ", ".join(used_by.get(c["id"], [])) if c["id"] != "DEVICE" else "każde urządzenie"
    sec.append(
        f'<section id="{aid}" class="card"><h3><span class="k">0x{cid:02X}</span> {E(c["id"])} — {E(c.get("alias", ""))}</h3>'
        f'<p>{E(c.get("description", ""))}</p><p class="sub">op = <code>0x{cid:02X}nn</code> · implementują: {E(who or "—")}</p>'
        f'<table>{THEAD}<tbody>{rows(c["commands"], lambda o, b=base: f"0x{(b | o):04X}")}</tbody></table>'
        f'{events_html(c.get("events"), base)}</section>'
    )
contract_html = "".join(sec)
sec = []

# ---- Devices
nav.append('<div class="nh" id="devices-nav">Urządzenia (typy)</div>')
for dv in devices:
    aid = f'd-{dv["id"]}'
    nav.append(f'<a href="#{aid}"><span class="k">{dv["type_id"]}</span> {E(dv["alias"])}</a>')
    cs = " ".join(f'<a class="tag" href="#c-{E(c)}">{E(c)}</a>' for c in dv.get("contracts", []))
    own = dv.get("commands", [])
    tbl = (f'<h4>Operacje własne (0xFFnn)</h4><table>{THEAD}<tbody>{rows(own, lambda o: f"0x{(0xFF00 | o):04X}")}</tbody></table>'
           if own else '<p class="sub">Brak operacji własnych — tylko kontrakty.</p>')
    sec.append(
        f'<section id="{aid}" class="card"><h3><span class="k">type {dv["type_id"]}</span> {E(dv["alias"])} <code>{E(dv["id"])}</code></h3>'
        f'<p>{E(dv.get("description", ""))}</p><p>Kontrakty: {cs}</p>{tbl}'
        f'{events_html(dv.get("events"), 0xFF00)}</section>'
    )
device_html = "".join(sec)

n_router = sum(len(d["commands"]) for d in services) + len(dev_contract["commands"])
n_c = sum(len(c["commands"]) for c in contracts)
n_d = sum(len(d.get("commands", [])) for d in devices)

page = f"""<!doctype html><html lang="pl"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>runIT API opcodes</title><style>
:root{{--bg:#f7f7f5;--card:#fff;--fg:#1d1d1b;--mut:#6b6b66;--line:#e2e1dc;--acc:#2f5fd0;--r:#1f8a4c;--w:#c0561d;--code:#efeee9}}
@media (prefers-color-scheme:dark){{:root:not([data-theme=light]){{--bg:#151514;--card:#1e1e1c;--fg:#e8e7e2;--mut:#9a9992;--line:#33332f;--acc:#7ea2ff;--r:#4fc684;--w:#f0915c;--code:#2a2a27}}}}
:root[data-theme=dark]{{--bg:#151514;--card:#1e1e1c;--fg:#e8e7e2;--mut:#9a9992;--line:#33332f;--acc:#7ea2ff;--r:#4fc684;--w:#f0915c;--code:#2a2a27}}
*{{box-sizing:border-box}}body{{margin:0;background:var(--bg);color:var(--fg);font:14px/1.45 system-ui,Segoe UI,sans-serif}}
.wrap{{display:grid;grid-template-columns:240px 1fr;min-height:100vh}}
nav{{position:sticky;top:0;height:100vh;overflow:auto;border-right:1px solid var(--line);padding:12px;background:var(--card)}}
nav a{{display:block;padding:3px 6px;border-radius:4px;color:var(--fg);text-decoration:none;font-size:13px;white-space:nowrap;overflow:hidden;text-overflow:ellipsis}}
nav a:hover{{background:var(--code)}}.nh{{margin:14px 0 4px;font-size:11px;text-transform:uppercase;letter-spacing:.06em;color:var(--mut)}}
main{{padding:20px 24px;min-width:0}}h1{{margin:0 0 4px;font-size:22px}}h2{{margin:28px 0 10px;font-size:18px}}h3{{margin:0 0 6px;font-size:16px}}h4{{margin:14px 0 6px;font-size:13px;color:var(--mut)}}
.card{{background:var(--card);border:1px solid var(--line);border-radius:8px;padding:14px 16px;margin:0 0 14px;overflow-x:auto}}
.k{{display:inline-block;font:600 12px ui-monospace,Consolas,monospace;background:var(--code);padding:1px 6px;border-radius:4px;margin-right:4px}}
code{{font:12px ui-monospace,Consolas,monospace;background:var(--code);padding:0 4px;border-radius:3px}}
table{{border-collapse:collapse;width:100%;font-size:13px}}th,td{{text-align:left;vertical-align:top;padding:6px 8px;border-top:1px solid var(--line)}}
th{{font-size:11px;text-transform:uppercase;color:var(--mut);border-top:0}}td.op{{font:600 13px ui-monospace,Consolas,monospace;white-space:nowrap}}
td.na{{color:var(--mut)}}.sub{{color:var(--mut);font-size:12px}}.io{{margin-top:2px}}.f{{font-family:ui-monospace,Consolas,monospace;font-size:12px}}
.b{{display:inline-block;font:700 11px ui-monospace,monospace;padding:1px 5px;border-radius:3px;color:#fff;margin-right:2px}}.b.R{{background:var(--r)}}.b.W{{background:var(--w)}}
.tag{{display:inline-block;font-size:11px;padding:0 6px;border:1px solid var(--line);border-radius:10px;color:var(--mut);text-decoration:none;margin-right:3px}}
#q{{width:100%;padding:7px 9px;border:1px solid var(--line);border-radius:6px;background:var(--bg);color:var(--fg);font:inherit}}
.frame td,.frame th{{font-size:13px}}
@media (max-width:800px){{.wrap{{grid-template-columns:1fr}}nav{{position:static;height:auto;max-height:40vh}}main{{padding:16px}}}}
</style></head><body><div class="wrap"><nav><input id="q" placeholder="Szukaj (np. SCAN, 0x0A, servo)…">{"".join(nav)}</nav><main>
<h1>runIT — API: opcode'y routerów i urządzeń</h1>
<p class="sub">Wygenerowane z <code>runit-app/generated/</code> (contract_*.json, device_*.json); opy tylko lokalne (np. DISPLAY FLUSH) nie są komendami i ich tu nie ma. Odświeżenie: <code>python tools/gen_api_html.py</code>. Routery: {n_router} operacji · kontrakty: {n_c} · operacje własne urządzeń: {n_d}.</p>
<div class="card"><h3>Format ramek</h3><table class="frame"><thead><tr><th>Cel</th><th>Ramka (po nagłówku sesji, little-endian, packed)</th></tr></thead><tbody>
<tr><td>Router modułu</td><td><code>[router_key u8][dir u8][op u8][args…]</code></td></tr>
<tr><td>Urządzenie</td><td><code>[0x05][dir u8][device id u16][op u16][args…]</code> — op = <code>(contract &lt;&lt; 8) | n</code>, własne urządzenia: <code>0xFFnn</code></td></tr>
<tr><td>dir</td><td><span class="b R">R</span> <code>CMD_DIR_READ = 0</code> — pyta o wartość · <span class="b W">W</span> <code>CMD_DIR_WRITE = 1</code> — ustawia / wykonuje akcję</td></tr>
<tr><td>Zdarzenia</td><td><code>[0x07][NOTIFY 0x01][sys_event_t]</code> na strumieniu events (bez komend); event ID = <code>(contract &lt;&lt; 8) | n</code></td></tr>
</tbody></table><p class="sub">Klucze wolne: 0x09 (nigdy nie przydzielać). Log: op 0x02 nieużywany.</p></div>
<h2 id="routers">Routery</h2>{router_html}
<h2 id="contracts">Kontrakty urządzeń</h2>{contract_html}
<h2 id="devices">Urządzenia</h2>{device_html}
</main></div><script>
const q=document.getElementById('q');q.addEventListener('input',()=>{{const s=q.value.trim().toLowerCase();
document.querySelectorAll('main section').forEach(sec=>{{let any=false;sec.querySelectorAll('tbody tr').forEach(tr=>{{const m=!s||tr.textContent.toLowerCase().includes(s);tr.style.display=m?'':'none';any=any||m}});
const head=sec.querySelector('h3,p').textContent.toLowerCase();const hm=!s||sec.textContent.toLowerCase().includes(s);
if(s&&sec.querySelector('h3').textContent.toLowerCase().includes(s)){{sec.querySelectorAll('tbody tr').forEach(tr=>tr.style.display='');any=true}}
sec.style.display=(any||!s)?'':'none'}})}});
</script></body></html>"""

with open(OUT, "w", encoding="utf-8") as f:
    f.write(page)
print("ok", n_router, n_c, n_d)
