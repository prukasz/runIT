"""Copies the descriptors that live next to the code in runit-esp/components/ into runit-app/generated/, where the app reads them.

  runit-app/generated/devices/    device_*.json                          (runit-esp/components/devices/*/)
  runit-app/generated/contracts/  contract_*.json with scope "device"    (runit-esp/components/sys/*/ and others)
  runit-app/generated/services/   contract_*.json with scope "system"
  runit-app/generated/blocks/     block_*.json                           (runit-esp/components/vm/vm_blocks/**/)
  runit-app/generated/common/     errors.json, board.json, board_devkit.json (enums.json is written there by generate_enums.py)
  runit-app/generated/manuals/<id>/  manual.md and its pictures            (runit-esp/components/devices/device_*/manual/)
  runit-app/generated/sources/<id>/  the device's own .c and .h            (runit-esp/components/devices/device_*/)

A copy is the same JSON with its `$schema` pointing at templates/ from the new place. Copies of descriptors that no longer
exist are deleted from the four folders. The manual and the source copies are byte-for-byte, keyed by the device's `id`; the
pictures are the manual folder's image files. Nothing in runit-app/generated/ is edited by hand.

Usage: python templates/generate_bundle.py [--check]
"""
import json
import re

from _common import CHECK, COMPONENTS_DIR, GENERATED, fail, read, render, rel, sources, sync_file

IMAGES = (".png", ".jpg", ".jpeg", ".gif", ".svg", ".webp")

ERRORS = COMPONENTS_DIR / "core" / "errors" / "errors.json"
BOARD = COMPONENTS_DIR / "app" / "runit" / "board.json"
BOARD_DEVKIT = COMPONENTS_DIR / "app" / "runit" / "board_devkit.json"  # CONFIG_RUNIT_BOARD_DEVKIT
MANAGED = ("devices", "contracts", "services", "blocks")  # folders whose every .json is owned by this script


def plan():
    """{path under runit-app/generated/: (source file, schema file)} of every copy."""
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
            fail(f"{rel(path)} and {rel(files[target][0])} would both become runit-app/generated/{target}")
        files[target] = (path, schema)
    files["common/errors.json"] = (ERRORS, "errors.schema.json")
    files["common/board.json"] = (BOARD, "board.schema.json")
    files["common/board_devkit.json"] = (BOARD_DEVKIT, "board.schema.json")
    return files


def copy_of(source, schema, target):
    """The descriptor with `$schema` first, pointing at templates/ from runit-app/generated/<target>."""
    if not source.exists():
        fail(f"{rel(source)} not found (expected for runit-app/generated/{target})")
    document = json.loads(read(source))
    document.pop("$schema", None)
    depth = target.count("/") + 1  # runit-app/generated/<folder>/<file>: three levels up reach _SOFTWARE
    return {"$schema": "../" * (depth + 1) + f"templates/{schema}", **document}


def docs_plan():
    """{path under runit-app/generated/: source file} of every device's manual and of its code copy (runit-app/generated/manuals/<id>/, runit-app/generated/sources/<id>/)."""
    files = {}
    for descriptor in sources(".json"):
        if not descriptor.name.startswith("device_"):
            continue
        ident = json.loads(read(descriptor))["id"]
        folder = descriptor.parent
        for code in sorted(folder.glob("device_*.[ch]")):
            files[f"sources/{ident}/{code.name}"] = code
        manual = folder / "manual"
        # manual.md is English; manual.<lang>.md (manual.pl.md) is the same page in another language.
        for text in sorted(manual.glob("manual*.md")) if manual.is_dir() else []:
            if re.fullmatch(r"manual(\.[a-z]{2})?\.md", text.name):
                files[f"manuals/{ident}/{text.name}"] = text
        for picture in sorted(p for p in manual.glob("*") if p.suffix.lower() in IMAGES) if manual.is_dir() else []:
            files[f"manuals/{ident}/{picture.name}"] = picture
    return files


def sync_bytes(out, data):
    """Makes `out` hold `data`; with --check nothing is written. True when `out` was not up to date."""
    if out.exists() and out.read_bytes() == data:
        return False
    if not CHECK:
        out.parent.mkdir(parents=True, exist_ok=True)
        out.write_bytes(data)
    return True


def main():
    files = plan()
    docs = docs_plan()
    stale_docs = [p for root in ("manuals", "sources") for p in sorted((GENERATED / root).rglob("*")) if p.is_file() and p.relative_to(GENERATED).as_posix() not in docs]
    stale = [p for folder in MANAGED for p in sorted((GENERATED / folder).glob("*.json")) if f"{folder}/{p.name}" not in files]
    changed = [target for target, (source, schema) in sorted(files.items()) if sync_file(GENERATED / target, render(copy_of(source, schema, target)))]
    changed += [target for target, source in sorted(docs.items()) if (sync_bytes(GENERATED / target, source.read_bytes()) if source.suffix.lower() in IMAGES else sync_file(GENERATED / target, read(source)))]
    for path in stale_docs:
        changed.append(f"{rel(path)} (stale)")
        if not CHECK:
            path.unlink()
    for path in stale:
        changed.append(f"{rel(path)} (stale)")
        if not CHECK:
            path.unlink()
    if CHECK and changed:
        fail(f"runit-app/generated/ is out of date ({len(changed)}: {', '.join(changed[:5])}{' ...' if len(changed) > 5 else ''}): run python templates/generate_bundle.py")
    print(f"runit-app/generated/ is up to date ({len(files) + len(docs)} copies)" if CHECK else f"runit-app/generated/: {len(files) + len(docs)} copies, {len(changed)} changed, {len(stale) + len(stale_docs)} stale removed")


if __name__ == "__main__":
    main()
