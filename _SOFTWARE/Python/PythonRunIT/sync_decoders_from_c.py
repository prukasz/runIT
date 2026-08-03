"""
Scans components/codecs/decoders/dec_*.h and generates decoder_types.py -
ctypes structs for every packet payload, keyed by their class byte (0xXX) and
packet byte (0xYY), so the GUI's command builder never drifts from the C wire
format. Re-run this whenever a decoder header changes.

Parses exactly the shape dec_sys_contracts.h uses:
    #define <PREFIX>_CLASS_HEADER 0xXX
    #define HEADER_packet_foo_t 0xYY
    typedef struct __packed { ... } packet_foo_t;

Two decoder files don't carry their own <PREFIX>_CLASS_HEADER and need a class
resolved another way (see CODECS.MD's "Splitting a class across files" and
"Adding a New Class"):
  - dec_sys_device_install.h has no class header of its own - it's folded into
    dec_sys_contracts.h's packet table via SYS_CONTRACTS_INSTALL_PACKET_LIST,
    same class byte (0x01).
  - dec_sys_actions.h's class header (SYS_ACTIONS_CLASS_HEADER) is defined in
    sys_actions.h, the component header it wraps, not in the dec_*.h file.
Both are resolved by scanning every header under components/ for
<PREFIX>_CLASS_HEADER defines, then matching a decoder file's own
<PREFIX>_PACKET_LIST(X) macro name against that table (exact match, or the
longest known class name that prefixes it - e.g. "SYS_CONTRACTS_INSTALL"
matches class "SYS_CONTRACTS").
"""
import re
from pathlib import Path
from typing import List, Tuple, Dict, Optional

PROJECT_ROOT = Path(__file__).parent.parent.parent  # _SOFTWARE folder
COMPONENTS_DIR = PROJECT_ROOT / "components"
DECODERS_DIR = COMPONENTS_DIR / "codecs" / "decoders"
OUTPUT_FILE = Path(__file__).parent / "decoder_types.py"

C_TYPE_MAP = {
    "uint8_t": "ct.c_uint8",
    "uint16_t": "ct.c_uint16",
    "uint32_t": "ct.c_uint32",
    "uint64_t": "ct.c_uint64",
    "int8_t": "ct.c_int8",
    "int16_t": "ct.c_int16",
    "int32_t": "ct.c_int32",
    "int64_t": "ct.c_int64",
    "bool": "ct.c_bool",
    "float": "ct.c_float",
    "double": "ct.c_double",
}

CLASS_HEADER_RE = re.compile(r"#define\s+(\w+)_CLASS_HEADER\s+(0x[0-9A-Fa-f]+)")
PACKET_LIST_DEFINE_RE = re.compile(r"#define\s+(\w+)_PACKET_LIST\(X\)")
PACKET_HEADER_RE = re.compile(r"#define\s+HEADER_(packet_\w+_t)\s+(0x[0-9A-Fa-f]+)")
STRUCT_RE = re.compile(r"typedef\s+struct\s+__packed\s*\{(.*?)\}\s*(packet_\w+_t)\s*;", re.DOTALL)
FIELD_RE = re.compile(r"(\w+(?:\s+\w+)*?)\s+(\w+)\s*(?:\[\s*(\d+)\s*\])?\s*;")


class PacketField:
    def __init__(self, c_type: str, name: str, array_len: Optional[int] = None):
        self.c_type = c_type.strip()
        self.name = name.strip()
        self.array_len = array_len

    def to_ctypes(self) -> str:
        ct_type = C_TYPE_MAP.get(self.c_type, f"ct.c_uint32  # Unknown C type: {self.c_type}")
        if self.array_len:
            return f'("{self.name}", {ct_type} * {self.array_len})'
        return f'("{self.name}", {ct_type})'


class PacketStruct:
    def __init__(self, name: str, fields: List[PacketField], class_name: str, class_header: str, packet_header: str):
        self.name = name
        self.fields = fields
        self.class_name = class_name  # e.g. "SYS_CONTRACTS" - matches a DecoderClass member
        self.class_header = class_header
        self.packet_header = packet_header
        # packet_sys_device_uninstall_t -> sys_device_uninstall
        self.action_name = name.removeprefix("packet_").removesuffix("_t")

    def to_python_class(self) -> str:
        lines = [f"class {self.name}(ct.LittleEndianStructure):", "    _pack_ = 1"]
        if self.fields:
            lines.append("    _fields_ = [")
            for field in self.fields:
                lines.append(f"        {field.to_ctypes()},")
            lines.append("    ]")
        else:
            lines.append("    _fields_ = []")
        lines.append(f"    _class_header_ = DecoderClass.{self.class_name}")
        lines.append(f"    _packet_header_ = {self.packet_header}")
        lines.append(f'    _action_name_ = "{self.action_name}"')
        lines.append("")
        return "\n".join(lines)


def parse_fields(body: str) -> List[PacketField]:
    body_no_comments = re.sub(r"//.*?$", "", body, flags=re.MULTILINE)
    body_no_comments = re.sub(r"/\*.*?\*/", "", body_no_comments, flags=re.DOTALL)
    fields = []
    for c_type, name, array_len in FIELD_RE.findall(body_no_comments):
        if c_type.strip() == "struct":
            continue
        fields.append(PacketField(c_type, name, int(array_len) if array_len else None))
    return fields


def scan_all_class_headers() -> Dict[str, str]:
    """<PREFIX>_CLASS_HEADER defines from every header under components/, not
    just decoders/ - some classes (e.g. SYS_ACTIONS) are defined in the
    component header they wrap rather than in their dec_*.h file."""
    found: Dict[str, str] = {}
    for h in COMPONENTS_DIR.rglob("*.h"):
        text = h.read_text(encoding="utf-8", errors="ignore")
        for name, header in CLASS_HEADER_RE.findall(text):
            found[name] = header
    return found


def resolve_class(text: str, all_classes: Dict[str, str]) -> Optional[Tuple[str, str]]:
    """Find (class_name, class_header) for a decoder file's packets, direct or
    via its <PREFIX>_PACKET_LIST(X) macro name (see module docstring)."""
    direct = CLASS_HEADER_RE.search(text)
    if direct:
        return direct.group(1), direct.group(2)

    list_match = PACKET_LIST_DEFINE_RE.search(text)
    if not list_match:
        return None
    prefix = list_match.group(1)
    if prefix in all_classes:
        return prefix, all_classes[prefix]

    best_name = None
    for class_name in all_classes:
        if prefix.startswith(class_name) and (best_name is None or len(class_name) > len(best_name)):
            best_name = class_name
    return (best_name, all_classes[best_name]) if best_name else None


def parse_decoder_file(path: Path, all_classes: Dict[str, str]) -> Tuple[Optional[Tuple[str, str]], List[PacketStruct]]:
    """Returns ((class_name, class_header), [structs]) for one dec_*.h file."""
    text = path.read_text(encoding="utf-8", errors="ignore")

    class_info = resolve_class(text, all_classes)
    if not class_info:
        print(f"  Skipping {path.name}: no <PREFIX>_CLASS_HEADER (direct or via a _PACKET_LIST prefix) found")
        return None, []
    class_name, class_header = class_info

    packet_headers: Dict[str, str] = {name: header for name, header in PACKET_HEADER_RE.findall(text)}

    structs = []
    for body, struct_name in STRUCT_RE.findall(text):
        packet_header = packet_headers.get(struct_name)
        if packet_header is None:
            print(f"  Warning: {struct_name} has no matching HEADER_{struct_name} define, skipping")
            continue
        structs.append(PacketStruct(struct_name, parse_fields(body), class_name, class_header, packet_header))

    return (class_name, class_header), structs


def generate_file(classes: Dict[str, str], structs_by_file: Dict[str, List[PacketStruct]]) -> str:
    lines = [
        '"""',
        "AUTO-GENERATED by sync_decoders_from_c.py - do not edit by hand.",
        "Source: components/codecs/decoders/dec_*.h",
        '"""',
        "import ctypes as ct",
        "from enum import IntEnum",
        "",
        "",
        "class DecoderClass(IntEnum):",
    ]
    for name, header in classes.items():
        lines.append(f"    {name} = {header}")
    lines.append("")
    lines.append("")

    all_structs: List[PacketStruct] = []
    for file_name, structs in structs_by_file.items():
        if not structs:
            continue
        lines.append("# " + "=" * 76)
        lines.append(f"# {file_name} (class {structs[0].class_header} - {structs[0].class_name})")
        lines.append("# " + "=" * 76)
        lines.append("")
        for s in structs:
            lines.append(s.to_python_class())
            all_structs.append(s)

    lines.append("# " + "=" * 76)
    lines.append("# Registry - every generated packet struct, source order. Drives the GUI's")
    lines.append("# class -> packet -> form auto command builder.")
    lines.append("# " + "=" * 76)
    lines.append("PACKET_REGISTRY = [")
    for s in all_structs:
        lines.append(f"    {s.name},")
    lines.append("]")
    lines.append("")

    return "\n".join(lines)


def main() -> bool:
    print("Syncing decoder packet structs from C to Python...")

    if not DECODERS_DIR.exists():
        print(f"Decoders directory not found: {DECODERS_DIR}")
        return False

    header_files = sorted(DECODERS_DIR.glob("dec_*.h"))
    if not header_files:
        print(f"No dec_*.h files found in {DECODERS_DIR}")
        return False

    all_classes = scan_all_class_headers()

    classes: Dict[str, str] = {}
    structs_by_file: Dict[str, List[PacketStruct]] = {}
    total_structs = 0

    for header_file in header_files:
        print(f"Parsing {header_file.name}...")
        class_info, structs = parse_decoder_file(header_file, all_classes)
        if class_info:
            classes[class_info[0]] = class_info[1]
        structs_by_file[header_file.name] = structs
        total_structs += len(structs)
        for s in structs:
            print(f"   {s.class_header}/{s.packet_header}  {s.name}  ({s.action_name})")

    if total_structs == 0:
        print("No packet structs found!")
        return False

    content = generate_file(classes, structs_by_file)

    try:
        OUTPUT_FILE.write_text(content)
        print(f"\nGenerated {OUTPUT_FILE}")
        print(f"   Classes: {len(classes)}")
        print(f"   Packet structs: {total_structs}")
        return True
    except Exception as e:
        print(f"Error writing file: {e}")
        return False


if __name__ == "__main__":
    import sys
    sys.exit(0 if main() else 1)
