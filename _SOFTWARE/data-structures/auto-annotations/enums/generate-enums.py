"""
Combined enum JSON generator for every //#ref-enum-marked enum under components/.

Scans all *.h files for:
  //#ref-enum [@alias <Enum Title>]
  typedef enum [tag] {
    MEMBER [= value], // [@alias <label>] [@description <text>]
    ...
  } enum_name;

Only enums explicitly opted in via //#ref-enum (marker directly above the
typedef) are included - a deliberate opt-in, not a blanket scan, so an
internal/private enum never accidentally becomes public wire vocabulary.
This is the single source of truth for every $SYMBOL a device annotation can
reference - see data-structures/auto-annotations/device/device-annotations.md.

Usage:
  python data-structures/auto-annotations/enums/generate-enums.py <target_dir> <target_name>
"""
import argparse
import json
import re
import sys
from pathlib import Path

PROJECT_ROOT = Path(__file__).parents[3]  # _SOFTWARE folder
COMPONENTS_DIR = PROJECT_ROOT / "components"

MARKED_ENUM_RE = re.compile(
    r"//#ref-enum(?P<marker_rest>[^\n]*)\n\s*typedef\s+enum(?:\s+\w+)?\s*\{(?P<body>.*?)\}\s*(?P<name>\w+)\s*;",
    re.DOTALL,
)
MEMBER_RE = re.compile(r"^\s*(?P<name>[A-Za-z_]\w*)\s*(?:=\s*(?P<val>[^,]+?))?\s*,?\s*(?://\s*(?P<comment>.*))?\s*$")
TAG_RE = re.compile(r"@(\w+)\b")


def parse_tags(comment: str) -> dict:
    tags = {}
    matches = list(TAG_RE.finditer(comment))
    for i, m in enumerate(matches):
        start = m.end()
        end = matches[i + 1].start() if i + 1 < len(matches) else len(comment)
        tags[m.group(1).lower()] = comment[start:end].strip()
    return tags


def to_int(token: str):
    try:
        return int(token.strip(), 0)
    except ValueError:
        return None


def parse_members(body: str) -> list:
    members = []
    val = 0
    for raw_line in body.splitlines():
        line = raw_line.strip()
        if not line:
            continue
        m = MEMBER_RE.match(line)
        if not m:
            continue
        explicit = m.group("val")
        if explicit:
            resolved = to_int(explicit)
            if resolved is not None:
                val = resolved
        tags = parse_tags(m.group("comment") or "")
        members.append({"name": m.group("name"), "value": val, "alias": tags.get("alias"), "description": tags.get("description")})
        val += 1
    return members


def scan(root: Path) -> dict:
    enums = {}
    symbols = {}
    for h in sorted(root.rglob("*.h")):
        text = h.read_text(encoding="utf-8", errors="ignore")
        for m in MARKED_ENUM_RE.finditer(text):
            enum_name = m.group("name")
            marker_tags = parse_tags(m.group("marker_rest") or "")
            source_file = h.relative_to(PROJECT_ROOT).as_posix()
            members = parse_members(m.group("body"))

            if enum_name in enums:
                sys.exit(f"ERROR: enum '{enum_name}' is //#ref-enum-marked in two places: "
                          f"{enums[enum_name]['source_file']} and {source_file}")

            enums[enum_name] = {"alias": marker_tags.get("alias"), "source_file": source_file, "members": members}

            for mem in members:
                if mem["name"] in symbols:
                    other = symbols[mem["name"]]["enum"]
                    sys.exit(f"ERROR: symbol '{mem['name']}' is defined in two different //#ref-enum enums: "
                              f"'{other}' and '{enum_name}' - $SYMBOL resolution would be ambiguous")
                symbols[mem["name"]] = {"enum": enum_name, "value": mem["value"], "alias": mem["alias"], "description": mem["description"]}

    return {"enums": enums, "symbols": symbols}


def main() -> int:
    parser = argparse.ArgumentParser(description="Generate a combined JSON descriptor of every //#ref-enum enum.")
    parser.add_argument("target_dir", help="Output directory (created if missing)")
    parser.add_argument("target_name", help="Output file name, with or without .json")
    args = parser.parse_args()

    if not COMPONENTS_DIR.exists():
        print(f"Components directory not found: {COMPONENTS_DIR}")
        return 1

    doc = scan(COMPONENTS_DIR)

    out_name = args.target_name if args.target_name.endswith(".json") else args.target_name + ".json"
    out_path = Path(args.target_dir) / out_name
    out_path.parent.mkdir(parents=True, exist_ok=True)
    out_path.write_text(json.dumps(doc, indent=2) + "\n", encoding="utf-8")

    print(f"Wrote {out_path}")
    print(f"  Enums scanned : {len(doc['enums'])}")
    print(f"  Symbols total : {len(doc['symbols'])}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
