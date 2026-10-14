"""Copies the descriptors that live next to the code in components/ into generated/, where the app reads them.

  generated/devices/    device_*.json                          (components/devices/*/)
  generated/contracts/  contract_*.json with scope "device"    (components/sys/*/ and others)
  generated/services/   contract_*.json with scope "system"
  generated/blocks/     block_*.json                           (components/vm/vm_blocks/**/)
  generated/common/     errors.json, board.json                (enums.json is written there by generate_enums.py)

A copy is the same JSON with its `$schema` pointing at templates/ from the new place. Copies of descriptors that no longer
exist are deleted from the four folders. Nothing in generated/ is edited by hand.

Usage: python templates/generate_bundle.py [--check]
"""
import json

from _common import CHECK, COMPONENTS_DIR, GENERATED, fail, read, render, rel, sources, sync_file

ERRORS = COMPONENTS_DIR / "core" / "errors" / "errors.json"
BOARD = COMPONENTS_DIR / "app" / "runit" / "board.json"
MANAGED = ("devices", "contracts", "services", "blocks")  # folders whose every .json is owned by this script


def plan():
    """{path under generated/: (source file, schema file)} of every copy."""
    files = {}
    for path in sources(".json"):
        if path.name.startswith("device_"):
            target, schema = f"devices/{path.name}", "device.schema.json"
        elif path.name.startswith("contract_"):
            scope = json.loads(read(path)).get("scope", "device")
            target, schema = f"{'services' if scope == 'system' else 'contracts'}/{path.name}", "contract.schema.json"
        elif path.name.startswith("block_"):
            target, schema = f"blocks/{path.name}", "vm-block.schema.json"
        else:
            continue
        if target in files:
            fail(f"{rel(path)} and {rel(files[target][0])} would both become generated/{target}")
        files[target] = (path, schema)
    files["common/errors.json"] = (ERRORS, "errors.schema.json")
    files["common/board.json"] = (BOARD, "board.schema.json")
    return files


def copy_of(source, schema, target):
    """The descriptor with `$schema` first, pointing at templates/ from generated/<target>."""
    if not source.exists():
        fail(f"{rel(source)} not found (expected for generated/{target})")
    document = json.loads(read(source))
    document.pop("$schema", None)
    depth = target.count("/") + 1  # generated/<folder>/<file>: two levels up reach the repository root
    return {"$schema": "../" * depth + f"templates/{schema}", **document}


def main():
    files = plan()
    stale = [p for folder in MANAGED for p in sorted((GENERATED / folder).glob("*.json")) if f"{folder}/{p.name}" not in files]
    changed = [target for target, (source, schema) in sorted(files.items()) if sync_file(GENERATED / target, render(copy_of(source, schema, target)))]
    for path in stale:
        changed.append(f"{rel(path)} (stale)")
        if not CHECK:
            path.unlink()
    if CHECK and changed:
        fail(f"generated/ is out of date ({len(changed)}: {', '.join(changed[:5])}{' ...' if len(changed) > 5 else ''}): run python templates/generate_bundle.py")
    print(f"generated/ is up to date ({len(files)} copies)" if CHECK else f"generated/: {len(files)} copies, {len(changed)} changed, {len(stale)} stale removed")


if __name__ == "__main__":
    main()
