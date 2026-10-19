"""Checks that every descriptor conforms to its schema, and that every enum a descriptor names is published in enums.json.

  device.schema.json    runit-esp/components/devices/*/device_*.json
  contract.schema.json  runit-esp/components/**/contract_*.json
  vm-block.schema.json  runit-esp/components/vm/vm_blocks/**/block_*.json
  errors.schema.json    runit-esp/components/core/errors/errors.json
  error-texts.schema.json  runit-esp/components/**/errors_<module>.json (the message templates of a module's errors)
  board.schema.json     runit-esp/components/app/runit/board.json
  enums.schema.json     runit-app/generated/common/enums.json
  limits.schema.json    runit-app/generated/common/limits.json

An `"enum": "<name>"` of a field (type `enum`) and an `enum_ref` of an error field must name a group of enums.json.

Needs the jsonschema package (pip install jsonschema). Usage: python templates/validate_schemas.py
"""
import json

from _common import COMPONENTS_DIR, GENERATED, ROOT, fail, read, rel, sources

try:
    from jsonschema import Draft202012Validator
except ImportError:
    fail("the jsonschema package is missing: pip install jsonschema")

TEMPLATES = ROOT / "templates"
ENUMS = GENERATED / "common" / "enums.json"
LIMITS = GENERATED / "common" / "limits.json"


def descriptors(prefix):
    return [p for p in sources(".json") if p.name.startswith(prefix)]


CHECKS = (
    ("device.schema.json", descriptors("device_")),
    ("contract.schema.json", descriptors("contract_")),
    ("vm-block.schema.json", descriptors("block_")),
    ("errors.schema.json", [COMPONENTS_DIR / "core" / "errors" / "errors.json"]),
    ("error-texts.schema.json", descriptors("errors_")),
    ("board.schema.json", [COMPONENTS_DIR / "app" / "runit" / "board.json"]),
    ("enums.schema.json", [ENUMS]),
    ("limits.schema.json", [LIMITS]),
)


def enum_names(node):
    """Every enum group name a descriptor refers to: `{"type": "enum", "enum": name}` and `{"enum_ref": name}`."""
    if isinstance(node, dict):
        if node.get("type") == "enum" and isinstance(node.get("enum"), str):
            yield node["enum"]
        if isinstance(node.get("enum_ref"), str):
            yield node["enum_ref"]
        for value in node.values():
            yield from enum_names(value)
    elif isinstance(node, list):
        for value in node:
            yield from enum_names(value)


def main():
    problems, checked = [], 0
    published = set(json.loads(read(ENUMS))["enums"]) if ENUMS.exists() else set()
    if not published:
        problems.append(f"{rel(ENUMS)} is missing or empty: run python templates/generate_enums.py")
    for schema_name, paths in CHECKS:
        validator = Draft202012Validator(json.loads(read(TEMPLATES / schema_name)))
        for path in paths:
            if not path.exists():
                problems.append(f"{rel(path)}: not found (expected by {schema_name})")
                continue
            checked += 1
            document = json.loads(read(path))
            for error in sorted(validator.iter_errors(document), key=lambda e: list(map(str, e.path))):
                where = "/".join(map(str, error.path)) or "(root)"
                problems.append(f"{rel(path)}: {where}: {error.message[:140]}")
            for name in sorted(set(enum_names(document)) - published):
                problems.append(f"{rel(path)}: enum '{name}' is not published in {rel(ENUMS)} (mark it with //#ref-enum)")
    if problems:
        fail(f"{len(problems)} problem(s) in {checked} descriptors:\n  " + "\n  ".join(problems))
    print(f"{checked} descriptors conform to their schemas; every named enum is published ({len(published)} groups)")


if __name__ == "__main__":
    main()
