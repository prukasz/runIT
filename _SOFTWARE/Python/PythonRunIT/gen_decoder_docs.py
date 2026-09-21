"""
Prototype doc generator for components/codecs/decoders/dec_*.h.

Reads the `@tag` comment annotations on packet struct fields (see
dec_sys_device_install.h for examples: @required/@optional, @min/@max,
@available, @ref, @sentinel, @default, @group/@role, @note) and renders a
human-readable Markdown reference, one section per decoder file.

This is a standalone test of the annotation format - it does NOT touch
sync_decoders_from_c.py or decoder_types.py, and nothing in the firmware or
GUI build depends on its output. Re-run after editing a dec_*.h to refresh
DECODERS_API.generated.md.

Design notes (why it works the way it does):
  - Struct fields are parsed WITHOUT stripping comments first (unlike
    sync_decoders_from_c.py, which only needs types/names for ctypes and
    strips comments before parsing). Here the comment IS the payload.
  - A field with no `@tag` at all still renders fine: its whole trailing
    comment becomes the description, so plain hand-written comments already
    in dec_sys_contracts.h / dec_sys_actions.h / dec_features.h work with
    zero edits.
  - A bare `xxx_e` token found in a field's comment is treated as an
    implicit `@ref` even without the tag, e.g. `// sys_io_mode_e` -
    matching the many decoder fields that already document their enum this
    way in plain English.
  - dec_vm_loader.h's packets are cursor-parsed variable-length records, not
    __packed structs - for those, the packet's existing Doxygen block
    (@brief + Wire Layout bullets) is lifted verbatim as a fallback instead
    of trying to force a field table onto something that isn't one.
  - `@sentinel NAME` / `@default NAME` / `@min NAME` / `@max NAME` resolve
    against two symbol tables: every `#define NAME VALUE` found under
    components/, and every `CONFIG_NAME=value` line in the top-level
    `sdkconfig` (Kconfig-generated build config) - so `@max
    CONFIG_SYS_DEVICE_MAX_ID` renders as an actual number (127), not the bare
    symbol, and `@sentinel SYS_GPIO_NONE` renders as 255. A symbol that
    resolves in neither table is left as-is with no crash - this is a
    best-effort lookup, not compile-time enforcement (see the @-tag design
    discussion this script implements). sdkconfig values are a snapshot of
    the LAST BUILD's config, not a compile-time guarantee - a fresh
    `idf.py reconfigure` with different Kconfig choices changes them.
  - Class bytes (RX_PACKET_CLASS_<PREFIX> / TX_PACKET_CLASS_<PREFIX>) live in
    components/utils/Kconfig's "System-Wide Identity Assignments" menu, not
    a `#define` in any *.h file - the unified RX/TX PACKET_CLASS / PACKET_HEADER
    naming scheme adopted project-wide: byte-0 (class) says what module/stream
    the frame belongs to, byte-1 (header, only when a class carries more than
    one packet type) says which specific packet. sync_decoders_from_c.py's
    scan_all_class_headers() only looks for a literal `#define <PREFIX>_...`
    in *.h files (deliberately not touched here, see below), so it can't see
    these. scan_kconfig_class_headers() below fills that gap by parsing every
    Kconfig* file for `config (RX|TX)_PACKET_CLASS_<PREFIX>` / `default 0xNN`
    blocks and returns {<PREFIX>: '0xNN'}, merged on top of
    scan_all_class_headers()'s result before any resolve_class()/
    resolve_class_fallback() call - both of which key the class table by the
    bare prefix (e.g. "SYS_FEATURES"). resolve_class_fallback()'s own token
    scan (for decoder files like dec_vm_loader.h that only *reference* their
    class constant, e.g. CONFIG_RX_PACKET_CLASS_VM_LOADER, rather than
    defining it) matches the same RX_PACKET_CLASS_/TX_PACKET_CLASS_ pattern
    and extracts the prefix directly, ignoring any leading "CONFIG_".
  - Alongside the Markdown, a `.json` file is emitted with the same resolved
    data (numeric, not symbolic) keyed by packet struct name - the same key
    decoder_types.py uses for its generated ctypes classes - so a client app
    can zip the two together (wire layout from decoder_types.py, UI hints
    zip metadata) to auto-generate an install/config form per device without
    hand-writing one per packet.
  - Packets are grouped under the hand-written `// ===== Title =====` section
    banners the decoder files already use (e.g. "Servo Feature Packets
    (0x10 - 0x1F)", "Device Management Packet decoders (0x10 - 0x19)") -
    whichever banner appears earliest in the file, before a packet's own
    `#define HEADER_...`, is that packet's section. This reuses the author's
    own thematic grouping instead of inventing a second taxonomy that could
    drift from it; a file with no banners (dec_sys_device_install.h,
    dec_sys_actions.h, dec_vm_loader.h) just renders its packets ungrouped,
    same as before this existed.
"""
import json
import re
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Dict, List, Optional, Tuple

sys.path.insert(0, str(Path(__file__).parent))
from sync_decoders_from_c import scan_all_class_headers, resolve_class  # reuse, don't duplicate

PROJECT_ROOT = Path(__file__).parent.parent.parent  # _SOFTWARE folder
COMPONENTS_DIR = PROJECT_ROOT / "components"
DECODERS_DIR = COMPONENTS_DIR / "codecs" / "decoders"
DEFAULT_OUT_MD = DECODERS_DIR / "DECODERS_API.generated.md"
DEFAULT_OUT_JSON = DECODERS_DIR / "DECODERS_API.generated.json"
SDKCONFIG_PATH = PROJECT_ROOT / "sdkconfig"

PACKET_HEADER_RE = re.compile(r"#define\s+HEADER_(packet_\w+)\s+(0x[0-9A-Fa-f]+)")
STRUCT_RE = re.compile(r"typedef\s+struct\s+(?:__packed\s*)?\{(.*?)\}\s*(packet_\w+_t)\s*;", re.DOTALL)
FIELD_LINE_RE = re.compile(
    r"^\s*(?P<type>[A-Za-z_][\w ]*?)\s+(?P<name>\w+)\s*(?:\[\s*(?P<arr>\d+)\s*\])?\s*;\s*(?://\s*(?P<comment>.*))?\s*$"
)
TAG_RE = re.compile(r"@(\w+)\b")
ENUM_RE = re.compile(r"typedef\s+enum(?:\s+\w+)?\s*\{(.*?)\}\s*(\w+)\s*;", re.DOTALL)
ENUM_TOKEN_RE = re.compile(r"\b(\w+_e)\b")
DEFINE_RE = re.compile(r"#define\s+([A-Z][A-Z0-9_]*)\s+([^\s/][^\n/]*)")
SDKCONFIG_RE = re.compile(r"^(CONFIG_\w+)=(.+)$", re.MULTILINE)
BANNER_RE = re.compile(r"^// ={10,}\n// (.+?)\s*\n// ={10,}", re.MULTILINE)
KCONFIG_CONFIG_RE = re.compile(r"^\s*config\s+(\w+)\s*$")
KCONFIG_DEFAULT_RE = re.compile(r"^\s*default\s+(\S+)")


@dataclass
class Field:
    type: str
    name: str
    array_len: Optional[int]
    tags: Dict[str, str]


@dataclass
class PacketDoc:
    header_name: str
    header_value: str
    struct_name: Optional[str]
    fields: Optional[List[Field]]
    doc_comment: Optional[str]
    source_file: str
    section: Optional[str] = None


def find_banners(text: str) -> List[Tuple[int, str]]:
    """(offset, title) for every `// ===== Title =====` banner in file order."""
    return [(m.start(), m.group(1).strip()) for m in BANNER_RE.finditer(text)]


def section_at(banners: List[Tuple[int, str]], offset: int) -> Optional[str]:
    """Title of the last banner appearing at or before `offset`, or None if
    the packet comes before any banner (or the file has none)."""
    title = None
    for pos, name in banners:
        if pos <= offset:
            title = name
        else:
            break
    return title


def parse_tags(comment: str) -> Dict[str, str]:
    """Split a trailing field comment into @tag values. Untagged leading
    text (or the whole comment, if it has no @tag at all) becomes @desc -
    this is what lets plain hand-written comments render without edits."""
    tags: Dict[str, str] = {}
    matches = list(TAG_RE.finditer(comment))
    if not matches:
        text = comment.strip()
        if text:
            tags["desc"] = text
        return tags
    prefix = comment[: matches[0].start()].strip()
    if prefix:
        tags["desc"] = prefix
    for i, m in enumerate(matches):
        name = m.group(1).lower()
        start = m.end()
        end = matches[i + 1].start() if i + 1 < len(matches) else len(comment)
        tags[name] = comment[start:end].strip()
    return tags


def parse_struct_fields(body: str) -> List[Field]:
    fields = []
    for raw_line in body.splitlines():
        line = raw_line.strip()
        if not line:
            continue
        m = FIELD_LINE_RE.match(line)
        if not m:
            continue
        arr = m.group("arr")
        fields.append(
            Field(
                type=m.group("type").strip(),
                name=m.group("name"),
                array_len=int(arr) if arr else None,
                tags=parse_tags(m.group("comment") or ""),
            )
        )
    return fields


def group_fields(fields: List[Field]) -> Tuple[List[Tuple[str, object]], Dict[str, List[Field]]]:
    """Split fields into (single fields, @group-tagged clusters), preserving
    source order for the ungrouped ones. A @group is how the annotation
    format expresses "these N flat wire fields are really one optional
    struct" (e.g. a sys_io_pin_ref_t flattened to _device_id/_pin/_mode)."""
    order: List[Tuple[str, object]] = []
    groups: Dict[str, List[Field]] = {}
    for f in fields:
        g = f.tags.get("group")
        if g:
            if g not in groups:
                groups[g] = []
                order.append(("group", g))
            groups[g].append(f)
        else:
            order.append(("field", f))
    return order, groups


def infer_ref(field: Field, enums: Dict[str, List[Tuple[str, str]]]) -> Optional[str]:
    if "ref" in field.tags:
        return field.tags["ref"]
    haystack = " ".join(field.tags.get(k, "") for k in ("desc", "note"))
    for tok in ENUM_TOKEN_RE.findall(haystack):
        if tok in enums:
            return tok
    return None


def scan_enums(root: Path) -> Dict[str, List[Tuple[str, str]]]:
    enums: Dict[str, List[Tuple[str, str]]] = {}
    for h in root.rglob("*.h"):
        text = h.read_text(encoding="utf-8", errors="ignore")
        for body, name in ENUM_RE.findall(text):
            enums[name] = parse_enum_body(body)
    return enums


def parse_enum_body(body: str) -> List[Tuple[str, str]]:
    body = re.sub(r"//.*", "", body)
    body = re.sub(r"/\*.*?\*/", "", body, flags=re.DOTALL)
    members = []
    val = 0
    for part in body.split(","):
        part = part.strip()
        if not part:
            continue
        m = re.match(r"(\w+)\s*(?:=\s*(.+))?$", part)
        if not m:
            continue
        name, explicit = m.group(1), m.group(2)
        if explicit:
            try:
                val = int(explicit.strip(), 0)
            except ValueError:
                pass
        members.append((str(val), name))
        val += 1
    return members


KCONFIG_CLASS_NAME_RE = re.compile(r"^(?:RX|TX)_PACKET_CLASS_(\w+)$")


def scan_kconfig_class_headers(root: Path) -> Dict[str, str]:
    """Class bytes now live in Kconfig as `config RX_PACKET_CLASS_<PREFIX>` /
    `config TX_PACKET_CLASS_<PREFIX>` / `hex` / `default 0xNN`, not a #define -
    sync_decoders_from_c.py's scan_all_class_headers() only looks at *.h
    files, so it can't see these (and this script doesn't touch that file,
    see the module docstring). Returns {prefix: '0xNN'} keyed the same way
    scan_all_class_headers() does (bare class name, e.g. "SYS_FEATURES"), so
    the two dicts merge directly. RX and TX entries for the same prefix
    (e.g. VM_LOADER, which has both by design - see utils/Kconfig) collapse
    to one entry since they're required to hold the same value."""
    found: Dict[str, str] = {}
    for kc in root.rglob("Kconfig*"):
        pending_name = None
        for line in kc.read_text(encoding="utf-8", errors="ignore").splitlines():
            m = KCONFIG_CONFIG_RE.match(line)
            if m:
                pending_name = m.group(1)
                continue
            if pending_name:
                d = KCONFIG_DEFAULT_RE.match(line)
                if d:
                    cm = KCONFIG_CLASS_NAME_RE.match(pending_name)
                    if cm:
                        found[cm.group(1)] = d.group(1)
                    pending_name = None
    return found


def scan_defines(root: Path) -> Dict[str, str]:
    defines: Dict[str, str] = {}
    for h in root.rglob("*.h"):
        text = h.read_text(encoding="utf-8", errors="ignore")
        for name, value in DEFINE_RE.findall(text):
            defines.setdefault(name, value.strip())
    return defines


def scan_sdkconfig(path: Path) -> Dict[str, int]:
    """CONFIG_* values from the last-built sdkconfig. Only numeric-looking
    ones are kept (y/n booleans and quoted strings aren't useful as @min/@max
    bounds)."""
    if not path.exists():
        return {}
    text = path.read_text(encoding="utf-8", errors="ignore")
    out: Dict[str, int] = {}
    for name, raw in SDKCONFIG_RE.findall(text):
        v = to_int(raw.strip())
        if v is not None:
            out[name] = v
    return out


def to_int(token: str) -> Optional[int]:
    token = token.strip().strip('"')
    try:
        return int(token, 0)
    except ValueError:
        return None


def clean_doc_comment(raw: str) -> str:
    lines = raw.splitlines()
    out = []
    for line in lines:
        line = line.strip()
        line = re.sub(r"^/\*\*", "", line)
        line = re.sub(r"\*/$", "", line)
        line = re.sub(r"^\*\s?", "", line)
        out.append(line)
    return "\n".join(out).strip()


def resolve_numeric(token: str, defines: Dict[str, str], sdkconfig: Dict[str, int]) -> Tuple[Optional[int], str]:
    """Resolve an @min/@max/@default/@sentinel value to a real number.

    Priority: literal ("0x40", "127") -> sdkconfig CONFIG_* value -> #define
    value (itself re-resolved once, since a #define can point at another
    #define, e.g. a threshold defined as another macro's name). Returns
    (numeric_value_or_None, display_string) - display always leads with the
    number when one was found, keeping the source symbol in parens only for
    traceability back to the header/sdkconfig it came from."""
    token = token.strip()
    direct = to_int(token)
    if direct is not None:
        return direct, token
    if token in sdkconfig:
        return sdkconfig[token], f"{sdkconfig[token]} ({token})"
    if token in defines:
        inner = to_int(defines[token])
        if inner is None:
            inner = to_int(defines.get(defines[token], ""))
        return (inner, f"{inner} ({token})") if inner is not None else (None, f"{defines[token]} ({token}, unresolved)")
    return None, f"{token} (unresolved)"


def display_name(f: Field) -> str:
    """Field-name cell: shows the human `@alias` label when one is given
    (e.g. **ADC Reference Voltage**), hiding the raw wire name entirely -
    the doc is meant to read like an API reference, not a struct dump. Falls
    back to the raw `field_name` (as `` `code` ``) when there's no alias.
    The raw name is never actually lost: it's still the JSON key and the
    struct field decoder_types.py generates, so nothing downstream breaks -
    this only changes what a human reads in the Markdown table."""
    if "alias" in f.tags:
        return f"**{f.tags['alias']}**"
    arr = f"[{f.array_len}]" if f.array_len else ""
    return f"`{f.name}{arr}`"


def render_field_row(f: Field, enums: Dict[str, List[Tuple[str, str]]], defines: Dict[str, str], sdkconfig: Dict[str, int]) -> str:
    req = "required" if "required" in f.tags else ("optional" if "optional" in f.tags else "—")

    constraint_parts = []
    if "min" in f.tags or "max" in f.tags:
        lo = resolve_numeric(f.tags["min"], defines, sdkconfig)[1] if "min" in f.tags else "-"
        hi = resolve_numeric(f.tags["max"], defines, sdkconfig)[1] if "max" in f.tags else "-"
        constraint_parts.append(f"{lo} – {hi}")
    if "available" in f.tags:
        constraint_parts.append(f'one of {f.tags["available"]}')
    if "default" in f.tags:
        constraint_parts.append(f'default {resolve_numeric(f.tags["default"], defines, sdkconfig)[1]}')
    if "unit" in f.tags:
        constraint_parts.append(f'unit: {f.tags["unit"]}')
    constraint = "; ".join(constraint_parts) if constraint_parts else "—"

    ref = infer_ref(f, enums)
    desc_parts = []
    if f.tags.get("desc"):
        desc_parts.append(f.tags["desc"])
    if ref:
        if ref in enums:
            members = ", ".join(f"{v}={n}" for v, n in enums[ref])
            desc_parts.append(f"`{ref}`: {members}")
        else:
            desc_parts.append(f"`{ref}` (enum not found by scan)")
    if "sentinel" in f.tags:
        desc_parts.append(f'sentinel {resolve_numeric(f.tags["sentinel"], defines, sdkconfig)[1]} means absent')
    if "note" in f.tags:
        desc_parts.append(f.tags["note"])
    desc = " — ".join(desc_parts)

    return f"| {display_name(f)} | `{f.type}` | {req} | {constraint} | {desc} |"


def render_group(name: str, fields: List[Field], enums: Dict[str, List[Tuple[str, str]]], defines: Dict[str, str], sdkconfig: Dict[str, int]) -> List[str]:
    lines = [f"**{name}** (flattened struct - fields form one logical value)", ""]
    sentinel_field = next((f for f in fields if "sentinel" in f.tags), None)
    if sentinel_field:
        sym = resolve_numeric(sentinel_field.tags["sentinel"], defines, sdkconfig)[1]
        lines.append(
            f"Absent when `{sentinel_field.name} == {sym}`; the group's other fields are ignored in that case."
        )
        lines.append("")
    lines.append("| Field | Role | Type | Notes |")
    lines.append("|---|---|---|---|")
    for f in fields:
        role = f.tags.get("role", "—")
        ref = infer_ref(f, enums)
        note_parts = []
        if ref:
            note_parts.append(f"`{ref}`: " + ", ".join(f"{v}={n}" for v, n in enums[ref]) if ref in enums else f"`{ref}` (enum not found)")
        if "unit" in f.tags:
            note_parts.append(f'unit: {f.tags["unit"]}')
        lines.append(f"| {display_name(f)} | {role} | `{f.type}` | {' — '.join(note_parts)} |")
    lines.append("")
    return lines


def render_packet(p: PacketDoc, class_hex: str, enums: Dict[str, List[Tuple[str, str]]], defines: Dict[str, str], sdkconfig: Dict[str, int]) -> List[str]:
    title = p.header_name
    if title.startswith("packet_"):
        title = title[len("packet_") :]
    lines = [f"#### `{class_hex} {p.header_value}` — {title}", ""]

    if p.fields is not None:
        if not p.fields:
            lines.append("_No payload bytes._")
            lines.append("")
        else:
            order, groups = group_fields(p.fields)
            single_rows = [render_field_row(item, enums, defines, sdkconfig) for kind, item in order if kind == "field"]
            if single_rows:
                lines.append("| Field | Type | Req | Constraint | Description |")
                lines.append("|---|---|---|---|---|")
                lines.extend(single_rows)
                lines.append("")
            for kind, item in order:
                if kind == "group":
                    lines.extend(render_group(item, groups[item], enums, defines, sdkconfig))
    elif p.doc_comment:
        lines.append(p.doc_comment)
        lines.append("")
    else:
        lines.append("_No struct and no Doxygen block found for this packet - undocumented._")
        lines.append("")

    lines.append(f"Decoder: `decoder_{p.header_name}()`")
    lines.append("")
    return lines


CLASS_HEADER_TOKEN_RE = re.compile(r"(?:RX|TX)_PACKET_CLASS_(\w+)")


def resolve_class_fallback(text: str, all_classes: Dict[str, str]) -> Optional[Tuple[str, str]]:
    """resolve_class() (from sync_decoders_from_c.py) only finds a class via a
    direct #define in the file or a <PREFIX>_PACKET_LIST(X) macro name. A file
    like dec_vm_loader.h has neither - it dispatches with a plain switch - but
    it still *references* its class constant (e.g. CONFIG_RX_PACKET_CLASS_VM_LOADER
    in an error payload), so look for any RX_PACKET_CLASS_/TX_PACKET_CLASS_
    token used anywhere in the file (no \\b anchor before RX/TX - the token is
    typically "CONFIG_RX_PACKET_CLASS_..." with no word-boundary between the
    "CONFIG_" prefix and "RX", so this matches it as a substring and captures
    just the bare class name, e.g. "VM_LOADER") and match it against the
    merged class table."""
    for prefix in CLASS_HEADER_TOKEN_RE.findall(text):
        if prefix in all_classes:
            return prefix, all_classes[prefix]
    return None


def parse_file(path: Path, all_classes: Dict[str, str]) -> Tuple[Optional[Tuple[str, str]], List[PacketDoc]]:
    text = path.read_text(encoding="utf-8", errors="ignore")
    class_info = resolve_class(text, all_classes) or resolve_class_fallback(text, all_classes)

    banners = find_banners(text)
    structs = {m.group(2): m.group(1) for m in STRUCT_RE.finditer(text)}

    packets = []
    for header_match in PACKET_HEADER_RE.finditer(text):
        name, hexval = header_match.group(1), header_match.group(2)
        struct_name = name if name in structs else (name + "_t" if (name + "_t") in structs else None)
        fields = parse_struct_fields(structs[struct_name]) if struct_name else None

        doc_comment = None
        if fields is None:
            # (?:(?!\*/).)*? forbids the doc-comment body from containing a
            # "*/" - without it, a lazy .*? still lets re.search anchor at the
            # file's very first /** (its @file banner) and stretch across
            # every intervening comment/function to reach a later target,
            # since re.search tries the leftmost start position first. This
            # forces each candidate /** to close at its own next */, so the
            # search only succeeds at the block immediately before the target
            # decoder function - the nearest one, not the first one in the file.
            fn_re = re.compile(
                r"(/\*\*(?:(?!\*/).)*?\*/)\s*static inline err_h\s+decoder_" + re.escape(name) + r"\s*\(",
                re.DOTALL,
            )
            m = fn_re.search(text)
            if m:
                doc_comment = clean_doc_comment(m.group(1))

        section = section_at(banners, header_match.start())
        packets.append(PacketDoc(name, hexval, struct_name, fields, doc_comment, path.name, section))
    return class_info, packets


def field_json(f: Field, enums: Dict[str, List[Tuple[str, str]]], defines: Dict[str, str], sdkconfig: Dict[str, int]) -> dict:
    """Numeric-only metadata for one field - the app-facing counterpart of
    render_field_row(). Resolved values are real ints (or null when a symbol
    couldn't be resolved), never bare CONFIG_/#define names, so a client can
    use them directly as spinner bounds without its own symbol table."""
    out: dict = {"type": f.type}
    if f.array_len:
        out["array_len"] = f.array_len
    if "alias" in f.tags:
        out["alias"] = f.tags["alias"]
    if "required" in f.tags:
        out["required"] = True
    elif "optional" in f.tags:
        out["required"] = False
    if "min" in f.tags:
        out["min"] = resolve_numeric(f.tags["min"], defines, sdkconfig)[0]
    if "max" in f.tags:
        out["max"] = resolve_numeric(f.tags["max"], defines, sdkconfig)[0]
    if "available" in f.tags:
        out["available"] = parse_available_list(f.tags["available"])
    if "default" in f.tags:
        out["default"] = resolve_numeric(f.tags["default"], defines, sdkconfig)[0]
    if "sentinel" in f.tags:
        out["sentinel"] = resolve_numeric(f.tags["sentinel"], defines, sdkconfig)[0]
    if "unit" in f.tags:
        out["unit"] = f.tags["unit"]
    ref = infer_ref(f, enums)
    if ref:
        out["ref"] = ref
    if "group" in f.tags:
        out["group"] = f.tags["group"]
    if "role" in f.tags:
        out["role"] = f.tags["role"]
    if f.tags.get("desc"):
        out["desc"] = f.tags["desc"]
    if "note" in f.tags:
        out["note"] = f.tags["note"]
    return out


def parse_available_list(raw: str) -> List[int]:
    inner = raw.strip().lstrip("[").rstrip("]")
    values = []
    for part in inner.split(","):
        v = to_int(part.strip())
        if v is not None:
            values.append(v)
    return values


def build_json(
    header_files: List[Path],
    all_classes: Dict[str, str],
    enums: Dict[str, List[Tuple[str, str]]],
    defines: Dict[str, str],
    sdkconfig: Dict[str, int],
) -> dict:
    """Packets keyed by struct name - the exact key decoder_types.py uses for
    its generated ctypes classes (packet_sys_device_install_pca9685_t, ...),
    so a client can zip wire layout + this metadata per packet without a
    second lookup table."""
    packets_out: Dict[str, dict] = {}
    for path in header_files:
        class_info, packets = parse_file(path, all_classes)
        class_name, class_hex = class_info if class_info else (None, None)
        for p in packets:
            entry: dict = {
                "source_file": p.source_file,
                "section": p.section,
                "class_name": class_name,
                "class_header": class_hex,
                "packet_header": p.header_value,
                "decoder": f"decoder_{p.header_name}()",
            }
            if p.fields is not None:
                order, groups = group_fields(p.fields)
                entry["field_order"] = [f.name for f in p.fields]
                entry["fields"] = {f.name: field_json(f, enums, defines, sdkconfig) for f in p.fields}
                if groups:
                    entry["groups"] = {
                        gname: {
                            "fields": [f.name for f in gfields],
                            "sentinel_field": next((f.name for f in gfields if "sentinel" in f.tags), None),
                        }
                        for gname, gfields in groups.items()
                    }
            elif p.doc_comment:
                entry["doc"] = p.doc_comment
            packets_out[p.header_name] = entry

    return {
        "classes": all_classes,
        "enums": {name: [[int(v), n] for v, n in members] for name, members in enums.items()},
        "packets": packets_out,
    }


def main() -> bool:
    if not DECODERS_DIR.exists():
        print(f"Decoders directory not found: {DECODERS_DIR}")
        return False

    header_files = sorted(DECODERS_DIR.glob("dec_*.h"))
    if not header_files:
        print(f"No dec_*.h files found in {DECODERS_DIR}")
        return False

    print("Scanning components/ for enums and #defines (for @ref/@sentinel/@default resolution)...")
    all_classes = scan_all_class_headers()
    all_classes.update(scan_kconfig_class_headers(COMPONENTS_DIR))
    enums = scan_enums(COMPONENTS_DIR)
    defines = scan_defines(COMPONENTS_DIR)
    sdkconfig = scan_sdkconfig(SDKCONFIG_PATH)
    if not sdkconfig:
        print(f"  WARNING: no sdkconfig found at {SDKCONFIG_PATH} - CONFIG_* symbols in @min/@max will stay unresolved")

    out_lines = [
        "# Decoder Packet Reference (prototype)",
        "",
        "> AUTO-GENERATED by `gen_decoder_docs.py` from the `@tag` comment annotations",
        "> in `components/codecs/decoders/dec_*.h`. Do not edit by hand.",
        "",
    ]

    total_packets = total_fields = total_groups = 0
    unresolved_refs: set = set()

    for path in header_files:
        text = path.read_text(encoding="utf-8", errors="ignore")
        class_info = resolve_class(text, all_classes) or resolve_class_fallback(text, all_classes)
        class_name, class_hex = class_info if class_info else ("UNKNOWN", "0x??")

        print(f"Parsing {path.name} (class {class_hex} / {class_name})...")
        _, packets = parse_file(path, all_classes)

        out_lines.append(f"## {path.name} — class `{class_hex}` ({class_name})")
        out_lines.append("")

        last_section = object()  # sentinel that != None, so a leading section=None group still triggers no heading but a real first section does
        for p in packets:
            if p.section != last_section:
                if p.section is not None:
                    out_lines.append(f"### {p.section}")
                    out_lines.append("")
                last_section = p.section
            out_lines.extend(render_packet(p, class_hex, enums, defines, sdkconfig))
            total_packets += 1
            if p.fields:
                total_fields += len(p.fields)
                _, groups = group_fields(p.fields)
                total_groups += len(groups)
                for f in p.fields:
                    ref = infer_ref(f, enums)
                    if ref and ref not in enums:
                        unresolved_refs.add(ref)

    DEFAULT_OUT_MD.write_text("\n".join(out_lines), encoding="utf-8")

    schema = build_json(header_files, all_classes, enums, defines, sdkconfig)
    DEFAULT_OUT_JSON.write_text(json.dumps(schema, indent=2), encoding="utf-8")

    unresolved_numeric = sorted(
        {
            f'{name}.{fname}.{key}={raw}'
            for name, entry in schema["packets"].items()
            for fname, fmeta in entry.get("fields", {}).items()
            for key, raw in (("min", fmeta.get("min")), ("max", fmeta.get("max")), ("sentinel", fmeta.get("sentinel")), ("default", fmeta.get("default")))
            if key in fmeta and raw is None
        }
    )

    print(f"\nWrote {DEFAULT_OUT_MD}")
    print(f"Wrote {DEFAULT_OUT_JSON}")
    print(f"  Decoder files scanned : {len(header_files)}")
    print(f"  Packets documented    : {total_packets}")
    print(f"  Annotated fields      : {total_fields}")
    print(f"  Field groups found    : {total_groups}")
    print(f"  Enums resolved        : {len(enums)}")
    print(f"  #defines resolved     : {len(defines)}")
    print(f"  sdkconfig CONFIG_* resolved : {len(sdkconfig)}")
    if unresolved_refs:
        print(f"  WARNING: @ref names not found as any known enum: {sorted(unresolved_refs)}")
    if unresolved_numeric:
        print(f"  WARNING: numeric symbols left unresolved (null in JSON): {unresolved_numeric}")
    return True


if __name__ == "__main__":
    sys.exit(0 if main() else 1)
