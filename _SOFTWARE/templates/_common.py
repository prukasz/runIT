"""Helpers shared by the generators and the validator in templates/."""
import json
import sys
from functools import lru_cache
from pathlib import Path
from typing import NoReturn

ROOT = Path(__file__).resolve().parents[1]
COMPONENTS_DIR = ROOT / "components"
GENERATED = ROOT / "generated"
SKIP_DIRS = {"build", "legacy", ".git"}


def fail(message) -> NoReturn:
    print(f"ERROR: {message}", file=sys.stderr)
    raise SystemExit(1)


def rel(path):
    return path.relative_to(ROOT).as_posix()


@lru_cache(maxsize=None)
def sources(*suffixes):
    """Every file under components/ with one of the suffixes (build output is skipped)."""
    return tuple(sorted(
        p for p in COMPONENTS_DIR.rglob("*")
        if p.suffix in suffixes and not SKIP_DIRS & set(p.relative_to(COMPONENTS_DIR).parts)
    ))


@lru_cache(maxsize=None)
def read(path):
    """The text of a file with LF line endings."""
    return path.read_text(encoding="utf-8", errors="replace").replace("\r\n", "\n")


CHECK = "--check" in sys.argv


def render(document):
    return json.dumps(document, indent=2, ensure_ascii=False) + "\n"


def sync_file(out, text):
    """Makes `out` hold `text` (LF endings); with --check nothing is written. True when `out` was not up to date."""
    if out.exists() and read(out) == text:
        return False
    if not CHECK:
        out.parent.mkdir(parents=True, exist_ok=True)
        out.write_text(text, encoding="utf-8", newline="\n")
    return True


def publish(out, document, script):
    """Writes `document` to `out` as JSON; with --check it only compares and fails when `out` is stale."""
    if sync_file(out, render(document)) and CHECK:
        fail(f"{rel(out)} is out of date: run python templates/{script}")
    print(f"{rel(out)} is up to date" if CHECK else f"wrote {rel(out)}")
