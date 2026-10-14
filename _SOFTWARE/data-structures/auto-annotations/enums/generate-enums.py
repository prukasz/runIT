"""The shared vocabulary of the wire: every `//#ref-enum` enum and `//#ref-defines` group under components/ as one JSON.

  //#ref-enum @alias IO Mode
  typedef enum sys_io_mode_e {
    SYS_IO_MODE_ADC = 7,  //@alias ADC @description Measures the voltage on the pin ...
    ...
  } sys_io_mode_e;

  //#ref-defines router_key @alias Command targets @prefix ROUTER_KEY_
  #define ROUTER_KEY_SYS 0x01u  //@alias System @description ...
  #define ROUTER_KEY_I2C 0x02u  //@alias I2C

Only what is marked is published, so a private enum never becomes wire vocabulary. Every member of a published enum has an
`@alias` (the name a user sees; the app never derives one from a C identifier) and may have a `@description` for someone who does
not know the concept. A member's value is a literal, an expression of earlier members, `#define`s and `CONFIG_*` options (resolved
from sdkconfig, so IDs set in Kconfig reach the app unchanged); `*_CNT` / `*_COUNT` sentinels are left out. A `$SYMBOL` in the other generators'
annotations names a member of exactly one published enum.

A member may also say `@wire-size <bytes>` and `@mem-size <bytes>`: the bytes of one element of that type on the wire and in the board's
memory (vm_obj_t_e). They come out as `wire_size` / `mem_size`.

Output: data-structures/enums.json (LF endings, generated: do not edit)
  {"enums": {name: {alias, kind, source_file, members: [{name, value, alias, description?}]}},
   "symbols": {MEMBER: enum name}}

Usage:
  python data-structures/auto-annotations/enums/generate-enums.py [--check]
Other generators load `scan()` from this file.
"""
import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
COMPONENTS_DIR = ROOT / "components"
SDKCONFIG = ROOT / "sdkconfig"
OUT = ROOT / "data-structures" / "enums.json"
SCHEMA_VERSION = 2
SKIP_DIRS = {"build", "legacy"}

ENUM_RE = re.compile(r"//#ref-enum(?P<rest>[^\n]*)\n\s*typedef\s+enum(?:\s+\w+)?\s*\{(?P<body>.*?)\}\s*(?P<name>\w+)\s*;", re.DOTALL)  # a member comment may hold {braces} (@format)
DEFINES_RE = re.compile(r"^//#ref-defines(?P<rest>[^\n]*)\n(?P<body>(?:[ \t]*#define[^\n]*\n|[ \t]*//[^\n]*\n)+)", re.MULTILINE)
DEFINE_LINE_RE = re.compile(r"^\s*#define\s+(?P<name>\w+)\s+(?P<value>.+?)\s*(?://\s*(?P<comment>.*))?$")
MEMBER_RE = re.compile(r"^(?P<name>[A-Za-z_]\w*)\s*(?:=\s*(?P<value>[^,/]+?))?\s*,?\s*(?://\s*(?P<comment>.*))?$")
TAG_RE = re.compile(r"(?:^|(?<=\s))@([a-z][a-z0-9-]*)(?=\s|$)")
MEMBER_TAGS = {"alias", "description", "format", "stack-op", "wire-size", "mem-size"}  # format / stack-op belong to the block descriptor (vm-block-annotations.md)
HEAD_TAGS = {"alias", "prefix"}
CONFIG_RE = re.compile(r"^(CONFIG_\w+)=(0x[0-9a-fA-F]+|-?\d+)\s*$", re.MULTILINE)


def fail(message):
    raise SystemExit(f"ERROR: {message}")


def rel(path):
    return path.relative_to(ROOT).as_posix()


def parse_tags(text, allowed, context):
    """(lead, tags): the text before the first tag and `{tag: value}`; an unknown or repeated tag fails."""
    matches = list(TAG_RE.finditer(text))
    lead = (text[:matches[0].start()] if matches else text).strip()
    tags = {}
    for i, m in enumerate(matches):
        name = m.group(1)
        if name not in allowed:
            fail(f"{context}: unknown tag @{name} (this line takes: {', '.join('@' + t for t in sorted(allowed))})")
        if name in tags:
            fail(f"{context}: @{name} given twice")
        end = matches[i + 1].start() if i + 1 < len(matches) else len(text)
        tags[name] = text[m.end():end].strip()
    return lead, tags


def sdkconfig():
    if not SDKCONFIG.exists():
        fail(f"{rel(SDKCONFIG)} not found: configure the firmware once (idf.py reconfigure) so CONFIG_* values can be read")
    return {n: int(v, 0) for n, v in CONFIG_RE.findall(SDKCONFIG.read_text(encoding="utf-8", errors="ignore"))}


def headers():
    return sorted(p for p in COMPONENTS_DIR.rglob("*.h") if not SKIP_DIRS & set(p.relative_to(COMPONENTS_DIR).parts))


def all_defines():
    found = {}
    for header in headers():
        for line in header.read_text(encoding="utf-8", errors="ignore").replace("\r\n", "\n").split("\n"):
            m = re.match(r"^#define\s+(\w+)\s+([^\n/]+?)\s*(?://.*|/\*.*)?$", line)
            if m and "(" not in m.group(1):
                found.setdefault(m.group(1), m.group(2))
    return found


def evaluate(expr, names, context, defines=None, depth=0):
    """An integer C expression over literals, earlier members (`names`), `#define`s and CONFIG_* options."""
    if depth > 8:
        fail(f"{context}: `{expr}` refers to itself")
    config = sdkconfig() if re.search(r"\bCONFIG_\w+", expr) else {}
    defines = all_defines() if defines is None and re.search(r"[A-Za-z_]", re.sub(r"0[xX][0-9a-fA-F]+|\d+[uUlL]*", "", expr)) else (defines or {})

    def name_value(m):
        word = m.group(1)
        if word in names:
            return str(names[word])
        if word in config:
            return str(config[word])
        if word in defines:
            return str(evaluate(defines[word], names, f"{context} ({word})", defines, depth + 1))
        fail(f"{context}: `{word}` in `{expr}` is neither an earlier member, a #define nor a CONFIG_ option set in sdkconfig")
    text = re.sub(r"\b([A-Za-z_]\w*)\b", name_value, re.sub(r"(?<=[0-9a-fA-F])[uUlL]+\b", "", expr))
    if not re.fullmatch(r"[0-9a-fA-FxX<>|&+()\s*/-]+", text.strip()):
        fail(f"{context}: cannot read `{expr}` as an integer constant")
    try:
        return int(eval(text, {"__builtins__": {}}, {}))  # noqa: S307 - only numbers and operators survive the check above
    except Exception:
        fail(f"{context}: cannot read `{expr}` as an integer constant")


def member(name, value, comment, context):
    out = {"name": name, "value": value}
    lead, tags = parse_tags(comment or "", MEMBER_TAGS, context)
    if lead:
        fail(f"{context}: text `{lead}` before the first tag (write `@alias <name>` and `@description <text>`)")
    if not tags.get("alias"):
        fail(f"{context}: needs `@alias <the name a user sees>`")
    out["alias"] = tags["alias"]
    if "description" in tags:
        if not tags["description"]:
            fail(f"{context}: @description is empty")
        out["description"] = tags["description"]
    for tag, key in (("wire-size", "wire_size"), ("mem-size", "mem_size")):  # bytes of one element on the wire / in the board's memory
        if tag in tags:
            if not re.fullmatch(r"\d+", tags[tag]):
                fail(f"{context}: @{tag} is a number of bytes")
            out[key] = int(tags[tag])
    return out


def parse_enum(m, path):
    name = m.group("name")
    where = f"{rel(path)}: {name}"
    _, head = parse_tags(m.group("rest"), {"alias"}, where)
    if not head.get("alias"):
        fail(f"{where}: `//#ref-enum` needs `@alias <the enum's title>`")
    members, names, value = [], {}, -1
    for raw in m.group("body").split("\n"):
        line = raw.strip()
        if not line or line.startswith("//"):
            continue
        mm = MEMBER_RE.match(line)
        if not mm:
            fail(f"{where}: cannot parse `{line}`")
        value = evaluate(mm.group("value"), names, f"{where}: {mm.group('name')}") if mm.group("value") else value + 1
        names[mm.group("name")] = value
        if mm.group("name").endswith(("_CNT", "_COUNT")):
            continue
        members.append(member(mm.group("name"), value, mm.group("comment"), f"{where}: {mm.group('name')}"))
    if not members:
        fail(f"{where}: no members")
    values = [x["value"] for x in members]
    if len(values) != len(set(values)):
        fail(f"{where}: two members have the same value")
    return name, {"alias": head["alias"], "kind": "enum", "source_file": rel(path), "members": members}


def parse_defines(m, path):
    _, head = parse_tags(m.group("rest").split(None, 1)[1] if len(m.group("rest").split(None, 1)) > 1 else "", HEAD_TAGS, rel(path))
    name = m.group("rest").split()[0] if m.group("rest").split() else ""
    where = f"{rel(path)}: //#ref-defines {name}"
    if not re.fullmatch(r"[a-z][a-z0-9_]*", name) or not head.get("alias") or not head.get("prefix"):
        fail(f"{where}: `//#ref-defines <name> @alias <title> @prefix <PREFIX_>`")
    members, names = [], {}
    for line in m.group("body").split("\n"):
        if not line.strip() or line.strip().startswith("//"):
            continue
        dm = DEFINE_LINE_RE.match(line)
        if not dm:
            fail(f"{where}: cannot parse `{line.strip()}`")
        if not dm.group("name").startswith(head["prefix"]):
            fail(f"{where}: {dm.group('name')} does not start with the prefix {head['prefix']}")
        value = evaluate(dm.group("value"), names, f"{where}: {dm.group('name')}")
        names[dm.group("name")] = value
        members.append(member(dm.group("name"), value, dm.group("comment"), f"{where}: {dm.group('name')}"))
    if not members:
        fail(f"{where}: no #define lines under it")
    return name, {"alias": head["alias"], "kind": "defines", "prefix": head["prefix"], "source_file": rel(path), "members": members}


def scan():
    """({name: enum}, {symbol: enum name}) of everything published under components/."""
    enums = {}
    for header in headers():
        text = header.read_text(encoding="utf-8", errors="ignore").replace("\r\n", "\n")
        if "//#ref-" not in text:
            continue
        for m in ENUM_RE.finditer(text):
            name, enum = parse_enum(m, header)
            if name in enums:
                fail(f"{rel(header)}: enum {name} is published twice (also in {enums[name]['source_file']})")
            enums[name] = enum
        for m in DEFINES_RE.finditer(text):
            name, group = parse_defines(m, header)
            if name in enums:
                fail(f"{rel(header)}: //#ref-defines {name} is published twice (also in {enums[name]['source_file']})")
            enums[name] = group
        stray = len(re.findall(r"^//#ref-enum", text, re.MULTILINE)) + len(re.findall(r"^//#ref-defines", text, re.MULTILINE))
        found = len(ENUM_RE.findall(text)) + len(DEFINES_RE.findall(text))
        if stray != found:
            fail(f"{rel(header)}: a `//#ref-enum` / `//#ref-defines` marker is not directly above a `typedef enum {{...}} name;` / its #define lines")
    symbols = {}
    for name, enum in enums.items():
        for mem in enum["members"]:
            if mem["name"] in symbols:
                fail(f"{enum['source_file']}: member {mem['name']} is in {name} and in {symbols[mem['name']]}: a $SYMBOL must name one member")
            symbols[mem["name"]] = name
    return dict(sorted(enums.items())), dict(sorted(symbols.items()))


def render():
    enums, symbols = scan()
    document = {"schemaVersion": SCHEMA_VERSION, "kind": "enums", "enums": enums, "symbols": symbols}
    return json.dumps(document, indent=2, ensure_ascii=False) + "\n"


def main(argv):
    content = render()
    if "--check" in argv:
        if not OUT.exists() or OUT.read_text(encoding="utf-8").replace("\r\n", "\n") != content:
            print(f"out of date: {rel(OUT)}")
            return 1
        print("enums: up to date")
        return 0
    OUT.parent.mkdir(parents=True, exist_ok=True)
    with open(OUT, "w", encoding="utf-8", newline="\n") as f:
        f.write(content)
    enums = json.loads(content)["enums"]
    print(f"Wrote {rel(OUT)}: {len(enums)} enums, {sum(len(e['members']) for e in enums.values())} members")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
