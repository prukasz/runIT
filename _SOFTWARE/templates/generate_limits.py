"""Publishes the limits the board enforces as one JSON: generated/common/limits.json.

A limit is a Kconfig option marked with a comment directly above its `config` line. The value is the one the firmware is
built with (sdkconfig); the description is the first paragraph of the option's `help`.

  #@limit @alias Inputs per block @unit pins
  config VM_BLOCK_MAX_IN
      int "Maximum input accessors per block"
      default 16
      help
          Maximum number of input accessors allowed for a single VM execution block.

Marker:  #@limit @alias <title> [@unit <unit>]      directly above `config <NAME>` (an indented block is fine)

Only what is marked is published (task priorities and stack sizes are not limits a program can hit). `@alias` is required.

Output: generated/common/limits.json (generated: do not edit), checked by templates/limits.schema.json
  {"limits": {NAME: {config, value, alias, description?, unit?, source_file}}}

Usage: python templates/generate_limits.py [--check]
"""
import re

from _common import COMPONENTS_DIR, GENERATED, SKIP_DIRS, fail, publish, read, rel
from generate_enums import parse_tags, sdkconfig

OUT = GENERATED / "common" / "limits.json"
SCHEMA_VERSION = 1

MARKER_RE = re.compile(r"^\s*#@limit(?P<rest>.*)$")
CONFIG_LINE_RE = re.compile(r"^\s*config\s+(?P<name>\w+)\s*$")
HELP_RE = re.compile(r"^(?P<indent>\s*)help\s*$")
NEXT_ENTRY_RE = re.compile(r"^\s*(config|menuconfig|menu|endmenu|choice|endchoice|if|endif|comment|source)\b")


def kconfigs():
    return sorted(p for p in COMPONENTS_DIR.rglob("Kconfig*") if p.is_file() and not SKIP_DIRS & set(p.relative_to(COMPONENTS_DIR).parts))


def help_text(lines, start):
    """The first paragraph of the `help` of the option whose `config` line is lines[start], on one line ('' when it has none)."""
    i = start + 1
    while i < len(lines) and not NEXT_ENTRY_RE.match(lines[i]) and not HELP_RE.match(lines[i]):
        i += 1
    if i >= len(lines) or not HELP_RE.match(lines[i]):
        return ""
    indent = len(HELP_RE.match(lines[i]).group("indent").expandtabs())
    words, i = [], i + 1
    while i < len(lines):
        line = lines[i]
        if line.strip() == "":
            if words:
                break  # end of the first paragraph
        elif len(line) - len(line.lstrip()) <= indent:
            break
        else:
            words.append(line.strip())
        i += 1
    return " ".join(words)


def scan():
    """{NAME: limit} of every `#@limit` option under components/."""
    values = sdkconfig()
    limits = {}
    for path in kconfigs():
        lines = read(path).split("\n")
        for number, line in enumerate(lines):
            marker = MARKER_RE.match(line)
            if not marker:
                continue
            where = f"{rel(path)}:{number + 1}"
            config = CONFIG_LINE_RE.match(lines[number + 1]) if number + 1 < len(lines) else None
            if not config:
                fail(f"{where}: a `#@limit` marker is not directly above a `config <NAME>` line")
            name = config.group("name")
            _, tags = parse_tags(marker.group("rest"), {"alias", "unit"}, where)
            if not tags.get("alias"):
                fail(f"{where}: `#@limit` needs an @alias (the name a user sees)")
            if f"CONFIG_{name}" not in values:
                fail(f"{where}: CONFIG_{name} is not in sdkconfig: configure the firmware once (idf.py reconfigure)")
            if name in limits:
                fail(f"{where}: {name} is published twice (also in {limits[name]['source_file']})")
            description = help_text(lines, number + 1)
            limits[name] = {
                "config": f"CONFIG_{name}",
                "value": values[f"CONFIG_{name}"],
                "alias": tags["alias"],
                **({"description": description} if description else {}),
                **({"unit": tags["unit"]} if tags.get("unit") else {}),
                "source_file": rel(path),
            }
    return limits


def main():
    limits = scan()
    if not limits:
        fail("no `#@limit` marker found under components/")
    publish(OUT, {"$schema": "../../templates/limits.schema.json", "schemaVersion": SCHEMA_VERSION, "kind": "limits", "limits": limits}, "generate_limits.py")


if __name__ == "__main__":
    main()
