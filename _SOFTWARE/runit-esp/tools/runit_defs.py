"""Runtime loader for synced firmware definitions from runit-app/generated/.

Run `templates/sync.sh` (or `python templates/generate_*.py`) whenever firmware
IDs, enums, errors, contracts, or limits change. Every tool and hardware test
in `runit-esp/tools/` imports its wire IDs from here so renumbering never
breaks tests.
"""
from __future__ import annotations

import json
from functools import lru_cache
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
GENERATED = ROOT / "runit-app" / "generated"


def _load_json(path: Path) -> dict:
    if not path.exists():
        raise FileNotFoundError(
            f"{path} is missing; run templates/sync.sh to regenerate descriptors"
        )
    return json.loads(path.read_text(encoding="utf-8"))


@lru_cache(maxsize=None)
def _enums_by_symbol() -> dict[str, int]:
    doc = _load_json(GENERATED / "common" / "enums.json")
    out: dict[str, int] = {}
    for group in doc["enums"].values():
        for m in group["members"]:
            out[m["name"]] = int(m["value"])
    return out


@lru_cache(maxsize=None)
def _limits() -> dict[str, int]:
    doc = _load_json(GENERATED / "common" / "limits.json")
    return {name: int(entry["value"]) for name, entry in doc["limits"].items()}


@lru_cache(maxsize=None)
def _errors_by_name() -> dict[str, int]:
    doc = _load_json(GENERATED / "common" / "errors.json")
    out: dict[str, int] = {}
    for mod in doc["modules"]:
        for tag in mod["tags"]:
            out[tag["name"]] = int(tag["tag"])
    return out


@lru_cache(maxsize=None)
def _descriptors(folder: str) -> dict[str, dict]:
    out: dict[str, dict] = {}
    for p in sorted((GENERATED / folder).glob("*.json")):
        doc = _load_json(p)
        out[doc["id"]] = doc
    return out


def sym(name: str) -> int:
    """Return integer value of any synced enum/#define/MODULE_ID_* symbol."""
    return _enums_by_symbol()[name]


def limit(name: str) -> int:
    """Return integer value of a synced Kconfig #@limit option."""
    return _limits()[name]


def err(name: str) -> int:
    """Return 32-bit error tag ((module << 16) | code) by ERR_* symbol name."""
    return _errors_by_name()[name]


def service(name: str) -> dict:
    return _descriptors("services")[name]


def service_key(name: str) -> int:
    return int(service(name)["router_key"])


def service_op(name: str, op_id: str) -> int:
    for cmd in service(name)["commands"]:
        if cmd["id"] == op_id:
            return int(cmd["op"])
    raise KeyError(f"service {name!r} has no command {op_id!r}")


def service_event(name: str, event_id: str) -> int:
    for ev in service(name).get("events", []):
        if ev["id"] == event_id:
            return int(ev["code"])
    raise KeyError(f"service {name!r} has no event {event_id!r}")


def contract(name: str) -> dict:
    return _descriptors("contracts")[name]


def contract_id(name: str) -> int:
    return int(contract(name)["contract_id"])


def contract_op(name: str, op_id: str) -> int:
    """Return full 16-bit device op: (contract_id << 8) | op."""
    doc = contract(name)
    cid = int(doc["contract_id"])
    for cmd in doc["commands"]:
        if cmd["id"] == op_id:
            return (cid << 8) | int(cmd["op"])
    raise KeyError(f"contract {name!r} has no command {op_id!r}")


def contract_local_op(name: str, raw_op: int) -> int:
    """Return full 16-bit device op for a LOCAL contract op omitted from JSON."""
    return (contract_id(name) << 8) | raw_op


def contract_event(name: str, event_id: str) -> int:
    """Return full 32-bit event tag: (contract_id << 16) | event."""
    for ev in contract(name).get("events", []):
        if ev["id"] == event_id:
            return int(ev["code"])
    raise KeyError(f"contract {name!r} has no event {event_id!r}")


def device(name: str) -> dict:
    return _descriptors("devices")[name]


def device_type(name: str) -> int:
    return int(device(name)["type_id"])


def device_own_op(name: str, op_id: str) -> int:
    """Return full 16-bit device own op: (SYS_DEVICE_CONTRACT_OWN << 8) | op."""
    own = sym("SYS_DEVICE_CONTRACT_OWN")
    for cmd in device(name).get("commands", []):
        if cmd["id"] == op_id:
            return (own << 8) | int(cmd["op"])
    raise KeyError(f"device {name!r} has no own command {op_id!r}")


# ---- The pin the session tools drive ----------------------------------------------------------------------------
# A simulated pin of a testdev the tool creates and removes, so no pin of the board is touched.

PIN_DEV = 29  # an ID no board device takes
PIN = 1       # one of its pins


def pin_dev_create() -> bytes:
    """The command that creates it: [key][dir][id][op u16][type u16][flags u8][pins, channels, uart mode, baud u32]."""
    op = contract_op("DEVICE", "CREATE").to_bytes(2, "little")
    cfg = bytes([16, 4, 1]) + (115200).to_bytes(4, "little")
    head = bytes([sym("MODULE_ID_SYS_DEVICE"), sym("CMD_DIR_WRITE"), PIN_DEV]) + op
    return head + device_type("testdev").to_bytes(2, "little") + bytes([0]) + cfg


def pin_dev_remove() -> bytes:
    op = contract_op("DEVICE", "REMOVE").to_bytes(2, "little")
    return bytes([sym("MODULE_ID_SYS_DEVICE"), sym("CMD_DIR_WRITE"), PIN_DEV]) + op

