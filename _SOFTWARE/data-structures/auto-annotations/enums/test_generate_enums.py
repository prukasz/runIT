"""Tests of generate-enums.py: the real tree publishes what the other generators rely on; wrong enums are refused.

Run: python -m unittest data-structures/auto-annotations/enums/test_generate_enums.py
"""
import importlib.util
import json
import shutil
import tempfile
import unittest
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[2]
SPEC = importlib.util.spec_from_file_location("generate_enums", HERE / "generate-enums.py")


def load(root=None):
    module = importlib.util.module_from_spec(SPEC)
    SPEC.loader.exec_module(module)
    if root is not None:
        module.ROOT, module.COMPONENTS_DIR, module.SDKCONFIG = root, root / "components", root / "sdkconfig"
    return module


class Tree:
    def __init__(self, test):
        self.dir = Path(tempfile.mkdtemp(prefix="enums-"))
        test.addCleanup(shutil.rmtree, self.dir, True)
        (self.dir / "components").mkdir()
        shutil.copy(REPO / "sdkconfig", self.dir / "sdkconfig")

    def write(self, name, text):
        path = self.dir / "components" / name
        path.write_text(text, encoding="utf-8", newline="\n")

    def scan(self):
        return load(self.dir).scan()


class RealTree(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.enums, cls.symbols = load().scan()

    def test_the_wire_vocabulary_is_published_with_aliases(self):
        for name in ("sys_io_mode_e", "sys_hbridge_fault_reason_e", "router_key", "device_contract", "vm_obj_t_e", "servo_type_e"):
            self.assertIn(name, self.enums)
        for enum in self.enums.values():
            self.assertTrue(enum["alias"])
            for member in enum["members"]:
                self.assertTrue(member["alias"], member["name"])

    def test_values_and_symbols(self):
        modes = {m["name"]: m["value"] for m in self.enums["sys_io_mode_e"]["members"]}
        self.assertEqual(modes["SYS_IO_MODE_ADC"], 7)
        self.assertEqual(self.symbols["SYS_IO_MODE_ADC"], "sys_io_mode_e")
        keys = {m["name"]: m["value"] for m in self.enums["router_key"]["members"]}
        self.assertEqual((keys["ROUTER_KEY_VM"], keys["ROUTER_KEY_PROJECT"], keys["ROUTER_KEY_EVENT"]), (0x0B, 0x0C, 0x07))
        contracts = {m["name"]: m["value"] for m in self.enums["device_contract"]["members"]}
        self.assertEqual(contracts["SYS_DEVICE_CONTRACT_HBRIDGE"], 9)

    def test_element_sizes_and_the_stream_and_flag_groups_are_published(self):
        types = {m["name"]: m for m in self.enums["vm_obj_t_e"]["members"]}
        self.assertEqual((types["VM_OBJ_PTR"]["wire_size"], types["VM_OBJ_PTR"]["mem_size"]), (2, 4))
        self.assertEqual((types["VM_OBJ_F"]["wire_size"], types["VM_OBJ_B"]["wire_size"]), (4, 1))
        streams = {m["name"]: m["value"] for m in self.enums["stream_byte"]["members"]}
        self.assertEqual((streams["SYS_CONNECTOR_STREAM_TELEMETRY"], streams["SYS_CONNECTOR_STREAM_RESPONSE"], streams["SYS_CONNECTOR_STREAM_EVENTS"]), (2, 5, 6))
        flags = {m["name"]: m["value"] for m in self.enums["vm_wire_head_flag"]["members"]}
        self.assertEqual(flags["VM_WIRE_HEAD_F_UPD_RESETABLE"], 4)
        self.assertEqual(flags["VM_WIRE_HEAD_F_USR_PROTECTED"], 0x40)
        self.assertEqual({m["name"]: m["value"] for m in self.enums["vm_blk_on_error_e"]["members"]}["VM_BLK_ERR_CONTINUE"], 1)

    def test_count_sentinels_are_left_out(self):
        self.assertNotIn("SYS_I2C_BUS_COUNT", self.symbols)
        self.assertNotIn("VM_TIMER_UNIT_CNT", self.symbols)

    def test_a_kconfig_value_comes_from_sdkconfig(self):
        members = {m["name"]: m["value"] for m in self.enums["sys_connector_id_e"]["members"]}
        self.assertEqual(members["SYS_CONNECTOR_TELEMETRY"], 2)
        document = json.loads(load().render())
        self.assertEqual(document["kind"], "enums")


class Refusals(unittest.TestCase):
    ENUM = "//#ref-enum @alias Mode\ntypedef enum mode_e {\n  MODE_A = 0,  //@alias A\n  MODE_B = MODE_A + 3,  //@alias B @description second\n  MODE_CNT,\n} mode_e;\n"

    def refused(self, tree, *words):
        with self.assertRaises(SystemExit) as raised:
            tree.scan()
        for word in words:
            self.assertIn(word, str(raised.exception))

    def test_a_good_enum(self):
        tree = Tree(self)
        tree.write("a.h", self.ENUM)
        enums, symbols = tree.scan()
        self.assertEqual([(m["name"], m["value"]) for m in enums["mode_e"]["members"]], [("MODE_A", 0), ("MODE_B", 3)])

    def test_a_member_without_an_alias(self):
        tree = Tree(self)
        tree.write("a.h", self.ENUM.replace("//@alias A", "// plain"))
        self.refused(tree, "MODE_A", "@alias")

    def test_a_marker_without_a_title(self):
        tree = Tree(self)
        tree.write("a.h", self.ENUM.replace("//#ref-enum @alias Mode", "//#ref-enum"))
        self.refused(tree, "@alias")

    def test_a_marker_not_above_a_typedef(self):
        tree = Tree(self)
        tree.write("a.h", "//#ref-enum @alias Mode\nint x;\n" + self.ENUM)
        self.refused(tree, "not directly above")

    def test_one_symbol_in_two_enums(self):
        tree = Tree(self)
        tree.write("a.h", self.ENUM)
        tree.write("b.h", self.ENUM.replace("mode_e", "other_e"))
        self.refused(tree, "must name one member")

    def test_an_unknown_name_in_a_value(self):
        tree = Tree(self)
        tree.write("a.h", self.ENUM.replace("MODE_A + 3", "NOT_DEFINED + 3"))
        self.refused(tree, "NOT_DEFINED")

    def test_the_same_value_twice(self):
        tree = Tree(self)
        tree.write("a.h", self.ENUM.replace("MODE_A + 3", "0"))
        self.refused(tree, "same value")

    def test_a_defines_group(self):
        tree = Tree(self)
        tree.write("a.h", "//#ref-defines key @alias Keys @prefix K_\n#define K_ONE 0x01u  //@alias One\n#define K_TWO (K_ONE + 1)  //@alias Two @description second\n")
        enums, symbols = tree.scan()
        self.assertEqual([(m["name"], m["value"]) for m in enums["key"]["members"]], [("K_ONE", 1), ("K_TWO", 2)])

    def test_a_define_outside_the_prefix(self):
        tree = Tree(self)
        tree.write("a.h", "//#ref-defines key @alias Keys @prefix K_\n#define OTHER_ONE 1  //@alias One\n")
        self.refused(tree, "prefix")

    def test_an_element_size_must_be_a_number(self):
        tree = Tree(self)
        tree.write("a.h", self.ENUM.replace("MODE_A = 0,  //@alias A", "MODE_A = 0,  //@alias A @wire-size wide"))
        self.refused(tree, "number of bytes")

if __name__ == "__main__":
    unittest.main()
