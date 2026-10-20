"""Checks the references between descriptors, which no single schema can see.

  device     "contracts" name existing contracts
  command    one with a read and a write side is a mirror: read.returns has the fields of write.args (id, raw_type, size)
  device     a contract with a `channel` field needs the device's provides.channels (or provides.pins)
  field      a `control` fits its field: slider = a number with min and max, switch = a choice of two options
  vm-block   wire_id is unique, below VM_BLOCK_TYPES_MAX and equals VM_BLK_<ID> of vm_block_ids.h
             state_len equals the `.state_len` of the block's VM_BLOCK_TYPE_DEFINE (a sizeof resolved from a _Static_assert; 0 when absent)
             device.contract / device.device_type name an existing contract / device
             variant "op" is a command of that contract, "dir" one of its sides (needed when it has both); "args" keys are
             arguments of that side
             the "idle" options of a device block are 0 or the op code of a command with a write side: (contract_id << 8) | op,
             or 0xFF00 | op for a device's own command
             "fixed" / "default" / condition values fit the enum group, the options or the contract choice they refer to
             "{name}" in a header or body is a field, an argument, a pin, "device" or "variant"
             conditions name a field of the block, "device.<config id>" of the device, or a variant of the block

Blocks without a descriptor are listed, not failed: descriptors are written one block at a time.

Usage: python templates/crosscheck.py
"""
import json
import re

from _common import COMPONENTS_DIR, GENERATED, fail, read, rel, sources

BLOCK_IDS = COMPONENTS_DIR / "vm" / "vm_core" / "block" / "vm_block_ids.h"
ENUMS = GENERATED / "common" / "enums.json"
PLACEHOLDER = re.compile(r"\{([^{}]+)\}")
ASSERT_RE = re.compile(r"_Static_assert\s*\((.*?),\s*\"", re.DOTALL)
SIZEOF_RE = re.compile(r"sizeof\s*\(\s*(\w+)\s*\)\s*==\s*(\d+)")
BLOCK_TYPE_RE = re.compile(r"VM_BLOCK_TYPE_DEFINE\s*\(\s*(VM_BLK_\w+)\s*,(.*?)\);", re.DOTALL)
STATE_LEN_RE = re.compile(r"\.state_len\s*=\s*(?:sizeof\s*\(\s*(\w+)\s*\)|(\d+))")


def load(prefix):
    """{id: (path, document)} of every descriptor file whose name starts with `prefix`."""
    found = {}
    for path in sources(".json"):
        if path.name.startswith(prefix):
            document = json.loads(read(path))
            found[document["id"]] = (path, document)
    return found


def block_numbers():
    """({VM_BLK_<ID>: number}, VM_BLOCK_TYPES_MAX) from vm_block_ids.h."""
    text = read(BLOCK_IDS)
    numbers = {name: int(value) for name, value in re.findall(r"#define\s+(VM_BLK_\w+)\s+(\d+)", text)}
    limit = re.search(r"#define\s+VM_BLOCK_TYPES_MAX\s+(\d+)", text)
    return numbers, int(limit.group(1)) if limit else 64


def struct_sizes():
    """{type name: bytes} of every `sizeof(T) == N` a _Static_assert under runit-esp/components/ pins down."""
    sizes = {}
    for path in sources(".c", ".h"):
        for condition in ASSERT_RE.findall(read(path)):
            sizes.update({name: int(size) for name, size in SIZEOF_RE.findall(condition)})
    return sizes


def state_lengths():
    """({VM_BLK_<ID>: bytes}, problems): the `.state_len` of every VM_BLOCK_TYPE_DEFINE, 0 when it names none."""
    sizes, lengths, problems = struct_sizes(), {}, []
    for path in sources(".c"):
        for macro, body in BLOCK_TYPE_RE.findall(read(path)):
            found = STATE_LEN_RE.search(body)
            if found is None:
                lengths[macro] = 0
            elif found.group(2):
                lengths[macro] = int(found.group(2))
            elif found.group(1) in sizes:
                lengths[macro] = sizes[found.group(1)]
            else:
                problems.append(f"{rel(path)}: {macro}: .state_len is sizeof({found.group(1)}), which no _Static_assert pins to a number")
    return lengths, problems


def pin_ids(pins):
    return {p["id"] for p in pins if "id" in p}


class Checker:
    def __init__(self):
        self.problems = []
        self.contracts = load("contract_")
        self.devices = load("device_")
        self.enums = json.loads(read(ENUMS))["enums"] if ENUMS.exists() else {}

    def error(self, path, where, message):
        self.problems.append(f"{rel(path)}: {where}: {message}")

    def enum_values(self, name):
        return {m["value"] for m in self.enums.get(name, {}).get("members", [])}

    def allowed(self, field):
        """The values a contract argument / config field can take, or None when anything goes."""
        if field.get("type") == "enum" and "enum" in field:
            return self.enum_values(field["enum"])
        if "options" in field:
            return {o["value"] for o in field["options"]}
        return None

    def devices_check(self):
        for path, document in self.devices.values():
            for name in document.get("contracts", []):
                if name not in self.contracts:
                    self.error(path, "contracts", f"'{name}' is not a contract (no contract_*.json with that id)")

    def mirrors_check(self):
        def shape(part):
            return [(f.get("id"), f.get("raw_type"), f.get("size")) for f in (part or {}).get("fields", [])]
        for path, document in list(self.contracts.values()) + list(self.devices.values()):
            for c in document.get("commands", []):
                if "read" in c and "write" in c and shape(c["read"].get("returns")) != shape(c["write"].get("args")):
                    self.error(path, f"commands/{c['id']}", "a command with both sides is a mirror: read.returns must have the fields of write.args")

    def blocks(self):
        """Checks every block descriptor; returns (how many, block types without one)."""
        numbers, limit = block_numbers()
        lengths, problems = state_lengths()
        self.problems.extend(problems)
        blocks = load("block_")
        seen = {}
        for block_id, (path, block) in blocks.items():
            wire, macro = block["wire_id"], f"VM_BLK_{block_id.upper()}"
            if macro not in numbers:
                self.error(path, "wire_id", f"{macro} is not defined in {rel(BLOCK_IDS)}")
            elif numbers[macro] != wire:
                self.error(path, "wire_id", f"{wire} but {macro} is {numbers[macro]} in {rel(BLOCK_IDS)}")
            if wire >= limit:
                self.error(path, "wire_id", f"{wire} is not below VM_BLOCK_TYPES_MAX ({limit})")
            if wire in seen:
                self.error(path, "wire_id", f"{wire} is also the wire_id of '{seen[wire]}'")
            seen[wire] = block_id
            if macro not in lengths:
                self.error(path, "state_len", f"{macro} has no VM_BLOCK_TYPE_DEFINE under runit-esp/components/")
            elif lengths[macro] != block["state_len"]:
                self.error(path, "state_len", f"{block['state_len']} but the .state_len of {macro} is {lengths[macro]} in C")
            self.block(path, block)
        described = {f"VM_BLK_{b.upper()}" for b in blocks}
        missing = sorted(n.removeprefix("VM_BLK_").lower() for n in numbers if n not in described and n != "VM_BLK_NONE")
        return len(blocks), missing

    def block(self, path, block):
        spec = block.get("device", {})
        contract = device = None
        if "contract" in spec:
            entry = self.contracts.get(spec["contract"])
            if entry is None:
                self.error(path, "device.contract", f"'{spec['contract']}' is not a contract")
            else:
                contract = entry[1]
        if "device_type" in spec:
            entry = self.devices.get(spec["device_type"])
            if entry is None:
                self.error(path, "device.device_type", f"'{spec['device_type']}' is not a device")
            else:
                device = entry[1]
        if contract is not None:  # the idle option of a contract block is 0 (hold) or the full op code: (contract_id << 8) | op
            codes = {0} | {(contract["contract_id"] << 8) | c["op"] for c in contract["commands"] if "write" in c}
            for field in block.get("fields", []):
                for option in field.get("options", []) if field["id"] == "idle" else []:
                    if option["value"] not in codes:
                        self.error(path, f"fields/idle/options/{option['alias']}", f"{option['value']} is not 0 or an op code of contract {contract['id']} ((contract_id << 8) | op)")
        if device is not None and contract is None:  # a device's own command is (SYS_DEVICE_CONTRACT_OWN << 8) | op
            own = next((m["value"] for m in self.enums.get("device_contract", {}).get("members", []) if m["name"] == "SYS_DEVICE_CONTRACT_OWN"), None)
            if own is None:
                self.error(path, "device.device_type", "SYS_DEVICE_CONTRACT_OWN is not published in enums.json (device_contract)")
            else:
                codes = {0} | {(own << 8) | c["op"] for c in device.get("commands", []) if "write" in c}
                for field in block.get("fields", []):
                    for option in field.get("options", []) if field["id"] == "idle" else []:
                        if option["value"] not in codes:
                            self.error(path, f"fields/idle/options/{option['alias']}", f"{option['value']} is not 0 or an op code of a command of device {device['id']} ((0xFF << 8) | op)")
        if "opcodes" in block:
            opcodes = block["opcodes"]
            if "editor" not in block:
                self.error(path, "opcodes", "'opcodes' needs an 'editor'")
            if opcodes["enum"] not in self.enums:
                self.error(path, "opcodes/enum", f"'{opcodes['enum']}' is not published in {rel(ENUMS)}")
        configs = {f["id"]: f for f in (device or {}).get("config", {}).get("fields", [])}
        variant_ids = {v["id"] for v in block.get("variants", [])}

        for variant in block.get("variants") or [None]:
            where = f"variants/{variant['id']}" if variant else "block"
            part = variant or {}
            fields = {f["id"]: f for f in block.get("fields", []) + part.get("fields", [])}
            pins = (pin_ids(block.get("inputs", [])) | pin_ids(block.get("outputs", []))
                    | pin_ids(part.get("inputs", [])) | pin_ids(part.get("outputs", [])))
            args = {}
            if "op" in part:
                command = None
                owner = contract or device  # the commands come from the contract, else from the device type
                if owner is None:
                    self.error(path, f"{where}/op", "'op' needs a block with device.contract or device.device_type")
                else:
                    command = next((c for c in owner.get("commands", []) if c["id"] == part["op"]), None)
                    if command is None:
                        self.error(path, f"{where}/op", f"'{part['op']}' is not a command of {owner['id']}")
                side = None
                if command is not None:
                    sides = [d for d in ("read", "write") if d in command]
                    if "dir" in part:
                        side = command.get(part["dir"])
                        if side is None:
                            self.error(path, f"{where}/dir", f"{part['op']} has no {part['dir']} side")
                    elif len(sides) == 1:
                        side = command[sides[0]]
                    else:
                        self.error(path, f"{where}/dir", f"{part['op']} has a read and a write side: say which ('dir')")
                args = {f["id"]: f for f in (side or {}).get("args", {}).get("fields", [])}
                for name, override in part.get("args", {}).items():
                    at = f"{where}/args/{name}"
                    if command is None:
                        break
                    if name not in args:
                        self.error(path, at, f"not an argument of {part['op']} ({', '.join(args) or 'none'})")
                        continue
                    allowed = self.allowed(args[name])
                    if "fixed" in override and allowed is not None and override["fixed"] not in allowed:
                        self.error(path, f"{at}/fixed", f"{override['fixed']} is not a value of '{name}' ({', '.join(map(str, sorted(allowed)))})")
                    self.hints(path, at, override.get("hints", []), fields, configs, variant_ids)
            for field in fields.values():
                self.field(path, f"{where}/fields/{field['id']}", field, fields, configs, variant_ids)
            for pin in block.get("inputs", []) + part.get("inputs", []):
                if pin.get("overrides") and pin["overrides"] not in fields:
                    self.error(path, f"{where}/inputs/{pin['id']}/overrides", f"'{pin['overrides']}' is not a field of this block or variant")
            known = set(fields) | set(args) | pins | set(block.get("editor_values", [])) | {"device", "variant"}
            lines = [part.get("header", block.get("header", {})).get("subtitle", "")] + list(part.get("body", block.get("body", [])))
            for line in lines:
                for name in PLACEHOLDER.findall(line):
                    if name not in known:
                        self.error(path, where, f"{{{name}}} is not a field, argument, pin, 'device' or 'variant' here")

    def field(self, path, at, field, fields, configs, variant_ids):
        if "enum" in field:
            if field["enum"] not in self.enums:
                self.error(path, f"{at}/enum", f"'{field['enum']}' is not published in {rel(ENUMS)}")
            else:
                for key in ("default", "fixed"):
                    if key in field and field[key] not in self.enum_values(field["enum"]):
                        self.error(path, f"{at}/{key}", f"{field[key]} is not a member of {field['enum']}")
        for key in ("default", "fixed"):
            if key in field and "options" in field and field[key] not in {o["value"] for o in field["options"]}:
                self.error(path, f"{at}/{key}", f"{field[key]} is not one of the options")
        if "device_field" in field and fields.get(field["device_field"], {}).get("type") != "device":
            self.error(path, f"{at}/device_field", f"'{field['device_field']}' is not a field of type 'device' of this block or variant")
        self.hints(path, at, field.get("hints", []), fields, configs, variant_ids)

    def hints(self, path, at, hints, fields, configs, variant_ids):
        for number, hint in enumerate(hints):
            if "when" in hint:
                self.condition(path, f"{at}/hints/{number}/when", hint["when"], fields, configs, variant_ids)

    def condition(self, path, at, node, fields, configs, variant_ids):
        for key in ("all", "any"):
            for number, child in enumerate(node.get(key, [])):
                self.condition(path, f"{at}/{key}/{number}", child, fields, configs, variant_ids)
        if "variant" in node:
            names = node["variant"] if isinstance(node["variant"], list) else [node["variant"]]
            for name in names:
                if name not in variant_ids:
                    self.error(path, f"{at}/variant", f"'{name}' is not a variant of the block")
        if "wired" in node and fields.get(node["wired"], {}).get("input", "none") == "none":
            self.error(path, f"{at}/wired", f"'{node['wired']}' is not a field with an input")
        if "field" in node:
            name = node["field"]
            values = [node[k] for k in ("equals", "not") if k in node] + list(node.get("in", []))
            if name.startswith("device."):
                config = configs.get(name.removeprefix("device."))
                if config is None:
                    self.error(path, f"{at}/field", f"'{name}' is not a config field of the block's device")
                    return
                allowed = self.allowed(config)
                for value in values:
                    if allowed is not None and value not in allowed:
                        self.error(path, f"{at}/{name}", f"{value} is not a value of '{config['id']}'")
            elif name not in fields:
                self.error(path, f"{at}/field", f"'{name}' is not a field of this block or variant")


    def controls_check(self):
        """A field's `control` needs what it draws: a slider a number with min and max, a switch a choice of two options."""
        is_contract = False

        def walk(node, path, where):
            if isinstance(node, dict):
                control = node.get("control")
                if control == "slider" and node.get("type") not in ("number", "integer"):
                    self.error(path, where, "control slider needs a number field")
                # A contract's field may leave the range to the device (`ranges`); anywhere else it has its own.
                if control == "slider" and not ("min" in node and "max" in node) and not is_contract:
                    self.error(path, where, "control slider needs min and max (a contract field may take them from the device's ranges)")
                if control == "switch" and not (node.get("type") == "choice" and len(node.get("options", [])) == 2):
                    self.error(path, where, "control switch needs a choice field with exactly two options")
                for key, value in node.items():
                    walk(value, path, f"{where}/{node['id']}" if key == "fields" and "id" in node else where)
            elif isinstance(node, list):
                for item in node:
                    walk(item, path, f"{where}/{item['id']}" if isinstance(item, dict) and "id" in item and "control" in item else where)
        for path, document in list(self.contracts.values()) + list(self.devices.values()):
            is_contract = document.get("kind") != "device-definition"
            walk(document, path, document["id"])
        for path, document in self.devices.values():
            provides = document.get("provides", {})
            for name in document.get("contracts", []):
                needs = [f for c in self.contracts.get(name, (None, {}))[1].get("commands", []) for s in ("read", "write")
                         for part in ("args", "returns") for f in ((c.get(s) or {}).get(part) or {}).get("fields", []) if f.get("type") == "channel"]
                if needs and "channels" not in provides and "pins" not in provides:
                    self.error(path, "provides", f"contract {name} has a channel field: the device must give provides.channels (or provides.pins when its pins are its channels)")
            commands = {}
            for name in document.get("contracts", []):
                for c in self.contracts.get(name, (None, {}))[1].get("commands", []):
                    commands[c["id"]] = c
            for c in document.get("commands", []):
                commands[c["id"]] = c
            for key, r in document.get("ranges", {}).items():
                command, field = key.split(".", 1)
                sides = [s for s in ("read", "write") if s in commands.get(command, {})]
                fields = [f for s in sides for part in ("args", "returns") for f in (commands[command][s].get(part) or {}).get("fields", [])]
                if not any(f["id"] == field for f in fields):
                    self.error(path, f"ranges/{key}", "names no field of a command of the device's contracts or of its own commands")
                if r["min"] >= r["max"]:
                    self.error(path, f"ranges/{key}", "min must be below max")


def main():
    checker = Checker()
    checker.devices_check()
    checker.mirrors_check()
    checker.controls_check()
    count, missing = checker.blocks()
    if checker.problems:
        fail(f"{len(checker.problems)} broken reference(s):\n  " + "\n  ".join(checker.problems))
    print(f"references hold: {len(checker.devices)} devices, {len(checker.contracts)} contracts, {count} block descriptors")
    if missing:
        print(f"no descriptor yet for {len(missing)} block types: {', '.join(missing)}")


if __name__ == "__main__":
    main()
