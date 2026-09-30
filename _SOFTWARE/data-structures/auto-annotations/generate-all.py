"""Regenerate every descriptor under data-structures/ from the annotated C, in dependency order.

  python data-structures/auto-annotations/generate-all.py            regenerate everything
  python data-structures/auto-annotations/generate-all.py --check    change nothing; exit 1 and name the files that are out of date
  python data-structures/auto-annotations/generate-all.py --test     also run the generators' unit tests

Run it after changing any annotated header, a Kconfig option a descriptor
reads, an error map or the ESP-IDF version. Output files are written with LF
line endings on every platform, so a regeneration diffs the same everywhere.
"""
import importlib.util
import subprocess
import sys
import unittest
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
DATA = ROOT / "data-structures"

# In dependency order: the enum catalog first, the rest read it (and each other) live.
STEPS = [
    ("enums", HERE / "enums" / "generate-enums.py", [str(DATA), "enums.json"]),
    ("devices", HERE / "device" / "generate-devices.py", [str(ROOT / "components" / "codecs" / "decoders"), str(DATA / "devices")]),
    ("vm model", HERE / "vm" / "generate-vm-model.py", []),
    ("vm blocks", HERE / "vm" / "generate-vm-blocks.py", []),
    ("vm program", HERE / "vm" / "generate-vm-program.py", []),
    ("contracts", HERE / "contracts" / "generate-contracts.py", []),
    ("settings", HERE / "settings" / "generate-settings.py", []),
    ("board", HERE / "board" / "generate-board.py", []),
    ("errors", HERE / "errors" / "generate-errors.py", []),
    ("streams", HERE / "streams" / "generate-streams.py", []),
]
TESTS = [HERE / "device" / "test_generate_devices.py", HERE / "vm" / "test_vm_block_editor.py"]


def outputs():
    """Every generated file: everything under data-structures/ except the schemas, the annotation tooling and docs."""
    found = {}
    for path in DATA.rglob("*.json"):
        relative = path.relative_to(DATA).parts
        if relative[0] in ("schema", "auto-annotations") or "__pycache__" in relative:
            continue
        found[path] = path.read_bytes()
    return found


def normalize(data):
    return data.replace(b"\r\n", b"\n")


def run(check):
    before = outputs()
    for label, script, args in STEPS:
        print(f"-- {label}")
        done = subprocess.run([sys.executable, str(script), *args], cwd=ROOT, capture_output=True, text=True)
        if done.returncode != 0:
            print(done.stdout + done.stderr)
            print(f"FAILED: {label} ({script.relative_to(ROOT)})")
            restore(before)
            return 1
        last = (done.stdout.strip().splitlines() or [""])[-1]
        print(f"   {last}")
    after = outputs()
    for path, data in after.items():
        if b"\r\n" in data:
            path.write_bytes(normalize(data))
    after = outputs()
    changed = sorted({p.relative_to(DATA).as_posix() for p in after if p not in before or normalize(before[p]) != after[p]} |
                     {p.relative_to(DATA).as_posix() for p in before if p not in after})
    if check:
        restore(before)
        if changed:
            print("Out of date (run generate-all.py without --check):\n  " + "\n  ".join(changed))
            return 1
        print(f"All {len(after)} generated files are up to date.")
        return 0
    print(f"Regenerated {len(after)} files; {len(changed)} changed" + (":\n  " + "\n  ".join(changed) if changed else "."))
    return 0


def restore(before):
    for path in outputs():
        if path not in before:
            path.unlink()
    for path, data in before.items():
        if not path.exists() or path.read_bytes() != data:
            path.write_bytes(data)


def test():
    suite = unittest.TestSuite()
    for path in TESTS:
        spec = importlib.util.spec_from_file_location(path.stem, path)
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        suite.addTests(unittest.defaultTestLoader.loadTestsFromModule(module))
    result = unittest.TextTestRunner(verbosity=1).run(suite)
    return 0 if result.wasSuccessful() else 1


if __name__ == "__main__":
    args = set(sys.argv[1:])
    unknown = args - {"--check", "--test"}
    if unknown:
        sys.exit(f"unknown option {', '.join(sorted(unknown))} (use --check, --test)")
    status = run("--check" in args)
    if status == 0 and "--test" in args:
        status = test()
    sys.exit(status)
