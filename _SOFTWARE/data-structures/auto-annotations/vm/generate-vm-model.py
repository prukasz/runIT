"""Generate the annotated VM model used by TypeScript program builders.

Scans public VM declarations marked with //#vm-struct-ref and imports the
shared //#ref-enum catalog. This describes source-level VM structures; it does
not expose C addresses, resolution caches, or dynamic allocator internals.

Usage:
  python data-structures/auto-annotations/vm/generate-vm-model.py
"""
import importlib.util
import json
import re
from pathlib import Path

PROJECT_ROOT = Path(__file__).parents[3]
VM_ROOT = PROJECT_ROOT / "components" / "VM"
OUT_PATH = PROJECT_ROOT / "data-structures" / "vm" / "vm-model.generated.json"
SCHEMA_PATH = PROJECT_ROOT / "data-structures" / "schema" / "vm-model.schema.json"
MARKER_RE = re.compile(r"^\s*//#vm-struct-ref(?P<tags>[^\n]*)$", re.MULTILINE)
TAG_RE = re.compile(r"@([\w-]+)\b")


def parse_tags(text):
    tags = {}
    matches = list(TAG_RE.finditer(text))
    for i, match in enumerate(matches):
        end = matches[i + 1].start() if i + 1 < len(matches) else len(text)
        tags[match.group(1).lower().replace("-", "_")] = text[match.end():end].strip()
    return tags


def matching_brace(text, start):
    depth = 0
    for pos in range(start, len(text)):
        if text[pos] == "{":
            depth += 1
        elif text[pos] == "}":
            depth -= 1
            if depth == 0:
                return pos
    raise ValueError("unclosed struct or union")


def split_declarations(body):
    declarations, start, depth, pos = [], 0, 0, 0
    while pos < len(body):
        char = body[pos]
        if char == "{":
            depth += 1
        elif char == "}":
            depth -= 1
        elif char == ";" and depth == 0:
            end = pos + 1
            trailing = re.match(r"[ \t]*(?://[^\n]*)?(?:\r?\n|$)", body[end:])
            if trailing:
                end += trailing.end()
            declarations.append(body[start:end])
            start = end
            pos = end
            continue
        pos += 1
    return declarations


def split_comment(declaration):
    comments = list(re.finditer(r"//[^\n]*", declaration))
    if not comments:
        return declaration.strip().rstrip(";").rstrip(), ""
    last = comments[-1]
    return declaration[:last.start()].strip().rstrip(";").rstrip(), last.group()[2:].strip()


def resolved_symbols(value, symbols):
    if not value:
        return []
    tokens = value.strip().strip("[]").split(",")
    out = []
    for token in tokens:
        token = token.strip()
        if not token.startswith("$"):
            continue
        name = token[1:]
        if name not in symbols:
            raise ValueError(f"unknown shared enum symbol: {token}")
        symbol = symbols[name]
        out.append({"symbol": name, "enum": symbol["enum"], "value": symbol["value"], "alias": symbol["alias"], "description": symbol["description"]})
    return out


def annotation(tags, symbols):
    out = dict(tags)
    # Match device and contract descriptors: @enum-ref is canonical.  Accept
    # legacy @ref source annotations while emitting only the canonical key.
    if "ref" in out:
        out["enum_ref"] = out.pop("ref")
    if "one_of" in tags:
        out["one_of"] = resolved_symbols(tags["one_of"], symbols)
    if "case" in tags:
        out["case"] = resolved_symbols(tags["case"], symbols)
    return out


def parse_field(declaration, symbols):
    raw = declaration.strip()
    if not raw:
        return None
    nested = re.match(r"^(struct|union)\s*\{", raw, re.DOTALL)
    if nested:
        open_brace = raw.index("{")
        close_brace = matching_brace(raw, open_brace)
        tail = raw[close_brace + 1:].split("//", 1)[0].strip().rstrip(";").strip()
        header_comment = raw[open_brace + 1:raw.find("\n", open_brace)]
        trailing_comment = raw[close_brace + 1:].partition("//")[2]
        tags = {**parse_tags(header_comment), **parse_tags(trailing_comment)}
        name_match = re.match(r"(?P<name>\w+)$", tail)
        return {
            "name": name_match.group("name") if name_match else None,
            "kind": nested.group(1),
            "annotations": annotation(tags, symbols),
            "fields": [field for field in (parse_field(item, symbols) for item in split_declarations(raw[open_brace + 1:close_brace])) if field],
        }

    code, comment = split_comment(raw)
    code = re.sub(r"^\s*(?://[^\n]*\n\s*)+", "", code)
    if not code:
        return None
    tags = parse_tags(comment)

    match = re.match(r"^(?P<type>.+?)\s+(?P<name>\w+)(?:\s*\[(?P<array>[^\]]*)\])?\s*(?::\s*(?P<bits>\d+))?$", code, re.DOTALL)
    if not match:
        raise ValueError(f"cannot parse field: {code}")
    out = {"name": match.group("name"), "kind": "field", "c_type": " ".join(match.group("type").split()), "annotations": annotation(tags, symbols)}
    if match.group("bits"):
        out["bit_width"] = int(match.group("bits"))
    if match.group("array") is not None:
        raw_length = match.group("array").strip()
        out["array"] = {"flexible": raw_length == ""}
        if raw_length:
            out["array"]["length"] = raw_length
    return out


def parse_marked_structures(symbols):
    structures = []
    for path in sorted(VM_ROOT.rglob("*.h")):
        text = path.read_text(encoding="utf-8", errors="ignore")
        for marker in MARKER_RE.finditer(text):
            tail = text[marker.end():]
            start = re.search(r"\b(?:typedef\s+)?struct\b", tail)
            if not start:
                raise ValueError(f"{path}: marker has no following struct")
            declaration_start = marker.end() + start.start()
            open_brace = text.find("{", declaration_start)
            close_brace = matching_brace(text, open_brace)
            prefix = text[declaration_start:open_brace]
            suffix = text[close_brace + 1:text.find(";", close_brace)].strip()
            if prefix.startswith("typedef"):
                name = suffix
            else:
                named = re.search(r"struct\s+(\w+)", prefix)
                if not named:
                    raise ValueError(f"{path}: unnamed non-typedef struct")
                name = named.group(1)
            structures.append({
                "name": name,
                "source_file": path.relative_to(PROJECT_ROOT).as_posix(),
                "annotations": annotation(parse_tags(marker.group("tags")), symbols),
                "fields": [field for field in (parse_field(item, symbols) for item in split_declarations(text[open_brace + 1:close_brace])) if field],
            })
    return structures


def main():
    spec = importlib.util.spec_from_file_location("generate_enums", PROJECT_ROOT / "data-structures" / "auto-annotations" / "enums" / "generate-enums.py")
    enum_generator = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(enum_generator)
    catalog = enum_generator.scan(enum_generator.COMPONENTS_DIR)
    vm_enums = {name: entry for name, entry in catalog["enums"].items() if entry["source_file"].startswith("components/VM/")}
    model = {
        "$schema": SCHEMA_PATH.relative_to(PROJECT_ROOT).as_posix(),
        "schemaVersion": 1,
        "kind": "vm-model",
        "enums": vm_enums,
        "structures": parse_marked_structures(catalog["symbols"]),
    }
    OUT_PATH.parent.mkdir(parents=True, exist_ok=True)
    OUT_PATH.write_text(json.dumps(model, indent=2) + "\n", encoding="utf-8")
    print(f"Wrote {OUT_PATH.relative_to(PROJECT_ROOT)}")
    print(f"  VM enums      : {len(vm_enums)}")
    print(f"  VM structures : {len(model['structures'])}")


if __name__ == "__main__":
    main()
