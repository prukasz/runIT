"""Tests of generate-vm-blocks.py: the real tree builds and says what the app relies on; deliberately wrong headers are refused.

Run: python -m unittest data-structures/auto-annotations/vm/test_vm_blocks.py
Each refusal test copies `components/` and `sdkconfig` to a temporary tree, edits one line and expects the generator to stop with
a message that names the problem.
"""
import importlib.util
import json
import shutil
import tempfile
import unittest
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[2]
SPEC = importlib.util.spec_from_file_location("generate_vm_blocks", HERE / "generate-vm-blocks.py")


def load_generator(root=None):
    module = importlib.util.module_from_spec(SPEC)
    SPEC.loader.exec_module(module)
    if root is not None:
        module.ROOT = root
        module.VM_DIR = root / "components" / "vm"
        module.BLOCKS_DIR = module.VM_DIR / "vm_blocks"
        module.IDS_HEADER = module.VM_DIR / "vm_core" / "block" / "vm_block_ids.h"
        module.COMPONENTS_DIR = root / "components"
        module.OUT_DIR = root / "data-structures" / "vm" / "blocks"
        module.SDKCONFIG = root / "sdkconfig"
    return module


class Tree:
    """A temporary copy of the sources the generator reads, to break one thing at a time."""

    def __init__(self, test):
        self.dir = Path(tempfile.mkdtemp(prefix="vmblocks-"))
        test.addCleanup(shutil.rmtree, self.dir, True)
        shutil.copytree(REPO / "components", self.dir / "components", ignore=shutil.ignore_patterns("build", "*.o"))
        shutil.copy(REPO / "sdkconfig", self.dir / "sdkconfig")

    def edit(self, relative, old, new, count=1):
        path = self.dir / relative
        text = path.read_text(encoding="utf-8").replace("\r\n", "\n")
        assert old in text, f"{relative}: `{old[:60]}` not found"
        path.write_text(text.replace(old, new, count), encoding="utf-8", newline="\n")

    def build(self):
        return load_generator(self.dir).outputs()


SET_H = "components/vm/vm_blocks/core/set/vm_block_set.h"
SET_C = "components/vm/vm_blocks/core/set/vm_block_set.c"
EXPR_CORE = "components/vm/vm_blocks/core/expr_core/vm_expr_core.h"
IO_C = "components/vm/vm_blocks/device-contract/io/vm_block_io.c"
IDS = "components/vm/vm_core/block/vm_block_ids.h"


class RealTree(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.generator = load_generator()
        cls.files = cls.generator.outputs()
        cls.blocks = {p.stem[:-len(".block")]: json.loads(c) for p, c in cls.files.items() if p.name.endswith(".block.json")}

    def test_every_block_is_published_with_its_guide(self):
        self.assertGreaterEqual(len(self.blocks), 23)
        index = json.loads(self.files[self.generator.OUT_DIR / "index.generated.json"])
        self.assertEqual(sorted(b["name"] for b in index["blocks"]), sorted(b["name"] for b in self.blocks.values()))
        for block in self.blocks.values():
            self.assertTrue(block["alias"], block["name"])
            self.assertIn(self.generator.OUT_DIR / block["guide"], self.files)

    def test_ids_are_unique(self):
        ids = [b["id"] for b in self.blocks.values()]
        self.assertEqual(len(ids), len(set(ids)))

    def test_every_pin_and_user_setting_has_an_alias(self):
        for block in self.blocks.values():
            for side in ("inputs", "outputs"):
                for pin in block[side]["pins"]:
                    self.assertTrue(pin.get("alias"), f"{block['name']} {pin['name']}")
            for field in block.get("state", {}).get("fields", []):
                if field["source"] == "user" and "overridden_by" not in field and field.get("role") not in ("const", "idle-const"):
                    self.assertTrue(field.get("alias"), f"{block['name']}.{field['name']}")
            for enum in block.get("enums", {}).values():
                for member in enum["members"]:
                    self.assertTrue(member.get("alias"), f"{block['name']} {member['name']}")

    def test_firmware_shape_comes_from_the_palette_entry(self):
        shape = self.blocks["set"]["shape"]
        self.assertEqual((shape["min_in"], shape["max_in"], shape["required_in"], shape["state_len"]), (2, 2, 2, 8))
        self.assertEqual(self.blocks["if"]["shape"]["min_q"], 2)

    def test_face_templates_are_parsed_into_parts(self):
        face = self.blocks["timer"]["face"]
        self.assertEqual(face["lead"], [{"text": "Timer"}])
        refs = [p["ref"] for p in face["value"] if "ref" in p]
        self.assertEqual(refs, ["mode", "pt", "time_base"])
        self.assertEqual([p for p in face["value"] if p.get("ref") == "pt"][0]["kind"], "pin")

    def test_expression_formats_and_golden_vector(self):
        encoding = self.blocks["expr"]["encoding"]
        by_symbol = {o["symbol"]: o for o in encoding["opcodes"]}
        self.assertEqual(by_symbol["VM_EXPR_ADD"]["format"], "{a} + {b}")
        self.assertEqual(by_symbol["VM_EXPR_SQRT"]["format"], "sqrt({a})")
        self.assertEqual(by_symbol["VM_EXPR_SEL"]["format"], "{a} ? {b} : {c}")
        self.assertEqual(by_symbol["VM_EXPR_IN"]["format"], "{input}")
        self.assertTrue(by_symbol["VM_EXPR_DUP"]["stack_op"])
        self.assertEqual(encoding["examples"][0]["custom_data"], "0100090000000040010002000601010800")
        self.assertEqual([p.get("ref") or p.get("text") for p in encoding["formula"]], ["result", " = ", "expr"])

    def test_scaled_op_and_ids_of_device_blocks(self):
        drive = self.blocks["hbridge"]["ops"][0]
        self.assertEqual(drive["symbol"], "HBRIDGE_OP_SET_DRIVE")
        self.assertEqual(drive["value"], 0x0900)
        self.assertEqual(drive["inputs"][1]["scale"], 40.95)
        self.assertEqual(drive["inputs"][1]["unit"], "%")
        io = {o["symbol"]: o for o in self.blocks["io"]["ops"]}
        self.assertEqual(io["IO_OP_SET_LEVEL"]["value"], 0x0100)
        self.assertEqual(io["IO_OP_GET_LEVEL"]["kind"], "read")
        self.assertEqual(io["IO_OP_SET_LEVEL"]["inputs"][1]["choices"][1], {"value": 1, "alias": "High"})
        self.assertEqual({o["symbol"]: o["value"] for o in self.blocks["servo"]["ops"]}["SERVO_OP_SET"], 0xFF02)
        self.assertEqual({o["symbol"]: o["value"] for o in self.blocks["lifecycle"]["ops"]}["SYS_DEVICE_OP_SUSPEND"], 0x0002)

    def test_device_field_offers_the_blocks_devices(self):
        field = {f["name"]: f for f in self.blocks["hbridge"]["state"]["fields"]}["device_id"]
        self.assertEqual((field["id_kind"], field["contract"]), ("device", "HBRIDGE"))
        field = {f["name"]: f for f in self.blocks["servo"]["state"]["fields"]}["device_id"]
        self.assertEqual(field["device_type"], "device_servo")

    def test_a_reading_with_several_values_has_several_outputs(self):
        fault = {o["symbol"]: o for o in self.blocks["drv8962"]["ops"]}["DRV8962_OP_GET_FAULT"]
        self.assertEqual([p["name"] for p in fault["outputs"]], ["fault", "reason"])
        self.assertEqual([p["name"] for p in self.blocks["tps55289"]["ops"][1]["outputs"]], ["ocp_delay", "hiccup", "fault_mask", "discharge"])


class Refusals(unittest.TestCase):
    def refused(self, tree, *words):
        with self.assertRaises(SystemExit) as raised:
            tree.build()
        message = str(raised.exception)
        for word in words:
            self.assertIn(word, message)

    def test_a_pin_without_an_alias(self):
        tree = Tree(self)
        tree.edit(SET_H, "//@alias Source @description", "//@description")
        self.refused(tree, "VM_IN_SET_SOURCE", "@alias")

    def test_a_block_without_an_alias(self):
        tree = Tree(self)
        tree.edit(SET_H, "//@alias Set\n", "")
        self.refused(tree, "@alias")

    def test_a_setting_without_an_alias(self):
        tree = Tree(self)
        tree.edit("components/vm/vm_blocks/core/timer/vm_block_timer.h", "//@alias Mode @description", "//@description")
        self.refused(tree, "mode", "@alias")

    def test_an_unknown_tag(self):
        tree = Tree(self)
        tree.edit(SET_H, "@value object @overrides value", "@value object @overides value")
        self.refused(tree, "overides")

    def test_a_directive_line_outside_the_directive(self):
        tree = Tree(self)
        tree.edit(SET_H, "#define VM_SET_F_DONE", "//@alias stray\n#define VM_SET_F_DONE")
        self.refused(tree, "outside the block directive")

    def test_the_palette_entry_must_match_the_pins(self):
        tree = Tree(self)
        tree.edit(SET_C, ".max_in = 2", ".max_in = 3")
        self.refused(tree, "max_in")

    def test_the_state_size_must_match(self):
        tree = Tree(self)
        tree.edit(SET_C, ".state_len = sizeof(vm_block_set_data_t)", ".state_len = 4")
        self.refused(tree, "state_len")

    def test_a_face_reference_that_names_nothing(self):
        tree = Tree(self)
        tree.edit("components/vm/vm_blocks/core/timer/vm_block_timer.h", "//@face Timer | {mode} {pt} {time_base}", "//@face Timer | {mode} {nothing}")
        self.refused(tree, "{nothing}")

    def test_a_stray_brace_in_a_face(self):
        tree = Tree(self)
        tree.edit("components/vm/vm_blocks/core/timer/vm_block_timer.h", "//@face Timer | {mode} {pt} {time_base}", "//@face Timer | {mode} }")
        self.refused(tree, "stray brace")

    def test_an_opcode_without_a_format(self):
        tree = Tree(self)
        tree.edit(EXPR_CORE, " @format {a} + {b}", "")
        self.refused(tree, "VM_EXPR_ADD", "@format")

    def test_a_format_using_an_operand_the_opcode_does_not_pop(self):
        tree = Tree(self)
        tree.edit(EXPR_CORE, "@format {a} + {b}", "@format {a} + {c}")
        self.refused(tree, "VM_EXPR_ADD")

    def test_an_enum_member_without_an_alias(self):
        tree = Tree(self)
        tree.edit("components/vm/vm_blocks/core/edge/vm_block_edge.h", "//@alias Rising @description The enable changes from false to true", "//@description rising")
        self.refused(tree, "VM_EDGE_RISING", "@alias")

    def test_a_missing_user_guide(self):
        tree = Tree(self)
        (tree.dir / "components/vm/vm_blocks/core/set/set.md").unlink()
        self.refused(tree, "guide")

    def test_two_blocks_with_one_number(self):
        tree = Tree(self)
        tree.edit(IDS, "#define VM_BLK_IF 3 ", "#define VM_BLK_IF 6 ")
        self.refused(tree, "share id")

    def test_a_block_id_without_a_block(self):
        tree = Tree(self)
        tree.edit(IDS, "#define VM_BLK_SERVO 39", "#define VM_BLK_EXTRA 60\n#define VM_BLK_SERVO 39")
        self.refused(tree, "VM_BLK_EXTRA")

    def test_an_op_row_without_its_annotation(self):
        tree = Tree(self)
        tree.edit(IO_C, "    //@op IO_OP_TOGGLE @alias Toggle", "    //@opx IO_OP_TOGGLE @alias Toggle")
        self.refused(tree)

    def test_an_op_with_the_wrong_number_of_input_lines(self):
        tree = Tree(self)
        tree.edit(IO_C, "    //@op-in pin @alias Pin @id pin\n    VM_DEV_OP(IO_OP_TOGGLE", "    VM_DEV_OP(IO_OP_TOGGLE")
        self.refused(tree, "IO_OP_TOGGLE", "input")

    def test_a_rule_naming_an_unknown_error(self):
        tree = Tree(self)
        tree.edit("components/vm/vm_blocks/core/set/vm_block_set.h", "@error ERR_VM_BLK_BAD_SHAPE", "@error ERR_NO_SUCH_TAG")
        self.refused(tree, "ERR_NO_SUCH_TAG")


if __name__ == "__main__":
    unittest.main()
