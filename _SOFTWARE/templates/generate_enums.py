"""Publishes the enums and #define groups of the firmware as one JSON: templates/enums.json.

Only what is marked is published, so a private enum never becomes wire vocabulary.

  //#ref-enum @alias IO mode
  typedef enum {
    SYS_IO_MODE_ADC = 7,  //@alias ADC @description Measures the voltage on the pin
  } sys_io_mode_e;

  //#ref-defines router_key @alias Command targets @prefix ROUTER_KEY_
  #define ROUTER_KEY_SYS 0x01u  //@alias System @description The system router

Markers:
  //#ref-enum @alias <title>                            directly above `typedef enum [tag] { ... } name;`
  //#ref-defines <group> @alias <title> @prefix <P_>    directly above its `#define` lines

Every member needs `@alias` (the name a user sees; nothing is derived from a C identifier or a plain comment) and may have
`@description`. Text in the comment outside a tag is an error. A value is a literal, an expression of earlier members,
`#define`s (also function-like ones), or a `CONFIG_*` option read from sdkconfig. `*_CNT` / `*_COUNT` sentinels are left out.
`@wire-size <bytes>` and `@mem-size <bytes>` pass through as `wire_size` / `mem_size`.

Output: generated/common/enums.json (generated: do not edit), checked by templates/enums.schema.json
  {"enums": {name: {alias, kind, source_file, members: [{name, value, alias, description?}]}},
   "symbols": {MEMBER: enum name}}

Usage: python templates/generate_enums.py [--check]
"""
import re
from functools import lru_cache

from _common import GENERATED, ROOT, fail, publish, read, rel, sources

OUT = GENERATED / "common" / "enums.json"
SDKCONFIG = ROOT / "sdkconfig"
SCHEMA_VERSION = 1

ENUM_RE = re.compile(r"^//#ref-enum(?P<rest>[^\n]*)\n\s*typedef\s+enum(?:\s+\w+)?\s*\{(?P<body>.*?)\}\s*(?P<name>\w+)\s*;", re.DOTALL | re.MULTILINE)
DEFINES_RE = re.compile(r"^//#ref-defines(?P<rest>[^\n]*)\n(?P<body>(?:[ \t]*#define[^\n]*\n|[ \t]*//[^\n]*\n)+)", re.MULTILINE)
DEFINE_LINE_RE = re.compile(r"^\s*#define\s+(?P<name>\w+)\s+(?P<value>.+?)\s*(?://\s*(?P<comment>.*))?$")
MEMBER_RE = re.compile(r"^(?P<name>[A-Za-z_]\w*)\s*(?:=\s*(?P<value>[^,/]+?))?\s*,?\s*(?://\s*(?P<comment>.*))?$")
DEFINE_RE = re.compile(r"^#define[ \t]+(\w+)(\([^)]*\))?[ \t]+(.+?)[ \t]*(?://.*|/\*.*)?$", re.MULTILINE)
TAG_RE = re.compile(r"(?:^|(?<=\s))@([a-z][a-z0-9-]*)(?=\s|$)")
CAST_RE = re.compile(r"\(\s*(?:u?int(?:8|16|32|64)_t|unsigned|int|size_t)\s*\)")
CONFIG_RE = re.compile(r"^(CONFIG_\w+)=(0x[0-9a-fA-F]+|-?\d+)\s*$", re.MULTILINE)
MEMBER_TAGS = {"alias", "description", "wire-size", "mem-size", "format", "stack-op"}  # format / stack-op belong to the VM block descriptor: accepted, not published
HEAD_TAGS = {"alias", "prefix"}


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


@lru_cache(maxsize=None)
def sdkconfig():
    if not SDKCONFIG.exists():
        fail(f"{rel(SDKCONFIG)} not found: configure the firmware once (idf.py reconfigure) so CONFIG_* values can be read")
    return {n: int(v, 0) for n, v in CONFIG_RE.findall(read(SDKCONFIG))}


@lru_cache(maxsize=None)
def defines():
    """({NAME: value}, {NAME: (params, body)}) of the object-like and function-like #defines under components/."""
    plain, macros = {}, {}
    for header in sources(".h"):
        for name, params, body in DEFINE_RE.findall(read(header)):
            if params:
                macros.setdefault(name, ([p.strip() for p in params[1:-1].split(",") if p.strip()], body))
            else:
                plain.setdefault(name, body)
    return plain, macros


def split_args(text):
    args, depth, current = [], 0, ""
    for ch in text:
        if ch == "," and depth == 0:
            args.append(current.strip())
            current = ""
            continue
        depth += (ch == "(") - (ch == ")")
        current += ch
    return args + [current.strip()]


def expand_macros(expr, context):
    """Replaces `NAME(a, b)` of a function-like #define by its body (arguments substituted, C integer casts dropped)."""
    macros = defines()[1]
    for _ in range(8):
        m = next((x for x in re.finditer(r"\b(\w+)\(", expr) if x.group(1) in macros), None)
        if not m:
            break
        depth, end = 1, m.end()
        while end < len(expr) and depth:
            depth += (expr[end] == "(") - (expr[end] == ")")
            end += 1
        params, body = macros[m.group(1)]
        args = split_args(expr[m.end():end - 1])
        if len(args) != len(params):
            fail(f"{context}: {m.group(1)} takes {len(params)} arguments, got {len(args)}")
        for param, arg in zip(params, args):
            body = re.sub(rf"\b{re.escape(param)}\b", lambda _m, a=arg: f"({a})", body)
        expr = expr[:m.start()] + f"({body})" + expr[end:]
    return CAST_RE.sub("", expr)


def evaluate(expr, names, context, depth=0):
    """An integer C expression over literals, earlier members (`names`), `#define`s and CONFIG_* options."""
    if depth > 8:
        fail(f"{context}: `{expr}` refers to itself")
    plain = defines()[0]

    def value_of(m):
        word = m.group(1)
        if word in names:
            return str(names[word])
        if word.startswith("CONFIG_") and word in sdkconfig():
            return str(sdkconfig()[word])
        if word in plain:
            return str(evaluate(plain[word], names, f"{context} ({word})", depth + 1))
        fail(f"{context}: `{word}` in `{expr}` is neither an earlier member, a #define nor a CONFIG_ option set in sdkconfig")

    text = re.sub(r"\b([A-Za-z_]\w*)\b", value_of, re.sub(r"(?<=[0-9a-fA-F])[uUlL]+\b", "", expand_macros(expr, context)))
    if not re.fullmatch(r"[0-9a-fA-FxX<>|&+()\s*/-]+", text.strip()):
        fail(f"{context}: cannot read `{expr}` as an integer constant")
    try:
        return int(eval(text, {"__builtins__": {}}, {}))  # noqa: S307 - only numbers and operators survive the check above
    except Exception:
        fail(f"{context}: cannot read `{expr}` as an integer constant")


def member(name, value, comment, context):
    lead, tags = parse_tags(comment or "", MEMBER_TAGS, context)
    if lead:
        fail(f"{context}: text `{lead}` before the first tag (write `@alias <name>` and `@description <text>`)")
    if not tags.get("alias"):
        fail(f"{context}: needs `@alias <the name a user sees>`")
    out = {"name": name, "value": value, "alias": tags["alias"]}
    if "description" in tags:
        if not tags["description"]:
            fail(f"{context}: @description is empty")
        out["description"] = tags["description"]
    for tag, key in (("wire-size", "wire_size"), ("mem-size", "mem_size")):
        if tag in tags:
            if not re.fullmatch(r"\d+", tags[tag]):
                fail(f"{context}: @{tag} is a number of bytes")
            out[key] = int(tags[tag])
    return out


def build_members(items, where):
    """The members of a group from `(name, value expression or None, comment)`; a missing value is the previous one plus one."""
    members, names, value = [], {}, -1
    for name, expr, comment in items:
        value = evaluate(expr, names, f"{where}: {name}") if expr else value + 1
        names[name] = value
        if not name.endswith(("_CNT", "_COUNT")):
            members.append(member(name, value, comment, f"{where}: {name}"))
    if not members:
        fail(f"{where}: no members")
    if len({m["value"] for m in members}) != len(members):
        fail(f"{where}: two members have the same value")
    return members


def parse_enum(m, path):
    name = m.group("name")
    where = f"{rel(path)}: {name}"
    _, head = parse_tags(m.group("rest"), {"alias"}, where)
    if not head.get("alias"):
        fail(f"{where}: `//#ref-enum` needs `@alias <the enum's title>`")
    items = []
    for line in (x.strip() for x in m.group("body").split("\n")):
        if not line or line.startswith(("//", "/*", "*")):
            continue
        mm = MEMBER_RE.match(line)
        if not mm:
            fail(f"{where}: cannot parse `{line}`")
        items.append((mm.group("name"), mm.group("value"), mm.group("comment")))
    return name, {"alias": head["alias"], "kind": "enum", "source_file": rel(path), "members": build_members(items, where)}


def parse_defines(m, path):
    parts = m.group("rest").split(None, 1)
    name = parts[0] if parts else ""
    _, head = parse_tags(parts[1] if len(parts) > 1 else "", HEAD_TAGS, rel(path))
    where = f"{rel(path)}: //#ref-defines {name}"
    if not re.fullmatch(r"[a-z][a-z0-9_]*", name) or not head.get("alias") or not head.get("prefix"):
        fail(f"{where}: `//#ref-defines <group> @alias <title> @prefix <PREFIX_>`")
    items = []
    for line in m.group("body").split("\n"):
        if not line.strip() or line.strip().startswith("//"):
            continue
        dm = DEFINE_LINE_RE.match(line)
        if not dm:
            fail(f"{where}: cannot parse `{line.strip()}`")
        if not dm.group("name").startswith(head["prefix"]):
            fail(f"{where}: {dm.group('name')} does not start with the prefix {head['prefix']}")
        items.append((dm.group("name"), dm.group("value"), dm.group("comment")))
    return name, {"alias": head["alias"], "kind": "defines", "prefix": head["prefix"], "source_file": rel(path), "members": build_members(items, where)}


def scan():
    """({name: group}, {member: group name}) of everything published under components/."""
    groups = {}
    for header in sources(".h"):
        text = read(header)
        if "//#ref-" not in text:
            continue
        found = 0
        for regex, parse in ((ENUM_RE, parse_enum), (DEFINES_RE, parse_defines)):
            for m in regex.finditer(text):
                name, group = parse(m, header)
                if name in groups:
                    fail(f"{rel(header)}: {name} is published twice (also in {groups[name]['source_file']})")
                groups[name] = group
                found += 1
        if found != len(re.findall(r"^//#ref-(?:enum|defines)", text, re.MULTILINE)):
            fail(f"{rel(header)}: a `//#ref-enum` / `//#ref-defines` marker is not directly above a `typedef enum {{...}} name;` / its #define lines")
    symbols = {}
    for name, group in groups.items():
        for mem in group["members"]:
            if mem["name"] in symbols:
                fail(f"{group['source_file']}: member {mem['name']} is in {name} and in {symbols[mem['name']]}: a symbol must name one member")
            symbols[mem["name"]] = name
    return dict(sorted(groups.items())), dict(sorted(symbols.items()))


def main():
    groups, symbols = scan()
    publish(OUT, {"$schema": "../../templates/enums.schema.json", "schemaVersion": SCHEMA_VERSION, "kind": "enums", "enums": groups, "symbols": symbols}, "generate_enums.py")


if __name__ == "__main__":
    main()
