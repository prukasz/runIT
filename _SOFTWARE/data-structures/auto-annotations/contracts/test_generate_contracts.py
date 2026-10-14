"""Tests of generate-contracts.py: the real tree says what the app's codec relies on; wrongly annotated commands are refused.

Run: python -m unittest data-structures/auto-annotations/contracts/test_generate_contracts.py
Each refusal test copies `components/` and `sdkconfig` to a temporary tree, edits one line and expects a message naming the problem.
"""
import importlib.util
import json
import shutil
import tempfile
import unittest
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[2]
SPEC = importlib.util.spec_from_file_location("generate_contracts", HERE / "generate-contracts.py")


def load(root=None):
    module = importlib.util.module_from_spec(SPEC)
    SPEC.loader.exec_module(module)
    if root is not None:
        module.ROOT, module.COMPONENTS_DIR, module.SDKCONFIG = root, root / "components", root / "sdkconfig"
        module.ENUMS_PATH = REPO / "data-structures" / "auto-annotations" / "enums" / "generate-enums.py"
    return module


class Tree:
    def __init__(self, test):
        self.dir = Path(tempfile.mkdtemp(prefix="contracts-"))
        test.addCleanup(shutil.rmtree, self.dir, True)
        shutil.copytree(REPO / "components", self.dir / "components", ignore=shutil.ignore_patterns("build", "*.o"))
        shutil.copy(REPO / "sdkconfig", self.dir / "sdkconfig")

    def edit(self, relative, old, new, count=1):
        path = self.dir / relative
        text = path.read_text(encoding="utf-8").replace("\r\n", "\n")
        assert old in text, f"{relative}: `{old[:60]}` not found"
        path.write_text(text.replace(old, new, count), encoding="utf-8", newline="\n")

    def render(self):
        return load(self.dir).render()


SYS_C = "components/core/router/router_sys.c"
ACTIONS_C = "components/sys/sys_actions/sys_actions_router.c"
IO_H = "components/sys/sys_io/sys_io.h"
HB_H = "components/sys/sys_hbridge/sys_hbridge.h"


class RealTree(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.doc = json.loads(load().render())
        cls.routers = {r["symbol"]: r for r in cls.doc["routers"]}
        cls.contracts = {c["name"]: c for c in cls.doc["device_contracts"]}

    def op(self, key, symbol):
        return next(o for o in self.routers[key]["ops"] if o["symbol"] == symbol)

    def test_every_router_key_is_listed_with_its_ops(self):
        self.assertEqual(self.routers["ROUTER_KEY_VM"]["key"], 0x0B)
        self.assertEqual(len(self.routers["ROUTER_KEY_SYS"]["ops"]), 5)
        self.assertEqual(self.routers["ROUTER_KEY_DEVICE"]["dispatch"]["kind"], "device")
        self.assertNotIn("ops", self.routers["ROUTER_KEY_EVENT"])

    def test_everything_a_user_sees_has_an_alias(self):
        def walk(fields, where):
            for f in fields:
                if not f.get("padding"):
                    self.assertTrue(f.get("alias"), f"{where}.{f['name']}")
                walk(f.get("fields", []), f"{where}.{f['name']}")
        for router in self.doc["routers"]:
            for op in router.get("ops", []):
                self.assertTrue(op["alias"] and op["description"], op["symbol"])
                walk(op.get("args", {}).get("fields", []), op["symbol"])
        for contract in self.doc["device_contracts"]:
            for op in contract["ops"]:
                self.assertTrue(op["alias"] and op["description"], op["symbol"])
                walk(op.get("args", {}).get("fields", []), op["symbol"])
            for event in contract.get("events", []):
                self.assertTrue(event["alias"] and event["description"], event["symbol"])

    def test_a_reading_and_its_tail(self):
        ping = self.op("ROUTER_KEY_SYS", "OP_PING")
        self.assertEqual((ping["id"], ping["returns"]["type"], ping["returns"]["unit"]), (1, "u32", "ms"))
        listing = self.op("ROUTER_KEY_ACTIONS", "SYS_ACTIONS_OP_LIST")
        self.assertEqual(listing["returns"]["type"], "u16")
        self.assertEqual((listing["returns_tail"]["type"], listing["returns_tail"]["array"]), ("u32", True))
        self.assertTrue(self.op("ROUTER_KEY_SYS", "OP_STOP")["urgent"])

    def test_args_come_from_the_handler_and_are_laid_out_packed(self):
        record = self.op("ROUTER_KEY_ACTIONS", "SYS_ACTIONS_OP_RECORD_START")["args"]
        self.assertEqual([(f["name"], f["offset"], f["size"]) for f in record["fields"]], [("id", 0, 4), ("execute", 4, 1)])
        self.assertEqual(record["size"], 5)
        write = self.op("ROUTER_KEY_ACTIONS", "SYS_ACTIONS_OP_WRITE")
        self.assertEqual(write["tail"]["type"], "bytes")
        # two routers have a private `begin_t`: each op got its own
        self.assertNotEqual(self.op("ROUTER_KEY_ACTIONS", "SYS_ACTIONS_OP_BEGIN")["args"]["size"], self.op("ROUTER_KEY_PROJECT", "SYS_PROJECT_OP_BEGIN")["args"]["size"])

    def test_device_contract_ids_and_events(self):
        drive = next(o for o in self.contracts["HBRIDGE"]["ops"] if o["name"] == "SET_DRIVE")
        self.assertEqual((drive["id"], drive["op"]), (0x0900, 0))
        self.assertEqual(self.contracts["IO"]["id"], 1)
        rising = self.contracts["IO"]["events"][0]
        self.assertEqual((rising["id"], rising["notify"]), (0x0100, False))
        self.assertTrue(self.contracts["HBRIDGE"]["events"][0]["notify"])
        create = self.contracts["DEVICE"]["ops"][0]
        self.assertEqual((create["name"], create["id"]), ("CREATE", 0))

    def test_nested_structs_and_enum_references(self):
        intr = next(o for o in self.contracts["IO"]["ops"] if o["name"] == "CONFIGURE_INTR")["args"]
        self.assertEqual(intr["size"], 11)
        cfg = intr["fields"][1]
        self.assertEqual(cfg["struct"], "sys_io_intr_cfg_t")
        self.assertEqual(cfg["fields"][0]["enum_ref"], "sys_io_intr_mode_e")
        mode = next(o for o in self.contracts["IO"]["ops"] if o["name"] == "SET_MODE")["args"]["fields"][1]
        self.assertEqual(mode["enum_ref"], "sys_io_mode_e")

    def test_the_frame(self):
        self.assertEqual(self.doc["frame"]["response"]["status_enum"], "router_status_e")
        self.assertEqual(self.doc["frame"]["status"], {"ROUTER_STATUS_OK": 0, "ROUTER_STATUS_ERROR": 1})


class Refusals(unittest.TestCase):
    def refused(self, tree, *words):
        with self.assertRaises(SystemExit) as raised:
            tree.render()
        for word in words:
            self.assertIn(word, str(raised.exception))

    def test_an_op_without_its_annotation(self):
        tree = Tree(self)
        tree.edit(SYS_C, "    //@op OP_STOP @alias Stop", "    //@opx OP_STOP @alias Stop")
        self.refused(tree, "OP_STOP", "//@op")

    def test_an_op_naming_the_wrong_row(self):
        tree = Tree(self)
        tree.edit(SYS_C, "//@op OP_STOP @alias", "//@op OP_RESTART @alias")
        self.refused(tree, "names `OP_RESTART`")

    def test_an_op_without_a_description(self):
        tree = Tree(self)
        tree.edit(SYS_C, "@description Checks the link and returns the board's uptime", "")
        self.refused(tree, "OP_PING", "@description")

    def test_a_field_without_an_alias(self):
        tree = Tree(self)
        tree.edit(SYS_C, "//@alias Timeout @description", "//@description")
        self.refused(tree, "timeout_ms", "@alias")

    def test_an_unknown_tag(self):
        tree = Tree(self)
        tree.edit(SYS_C, "@alias Timeout @description Time", "@alias Timeout @descripton Time")
        self.refused(tree, "descripton")

    def test_a_scalar_result_without_a_name(self):
        tree = Tree(self)
        tree.edit(SYS_C, "@returns u32 @returns-alias Uptime", "@returns u32")
        self.refused(tree, "@returns-alias")

    def test_a_handler_with_a_tail_that_is_not_described(self):
        tree = Tree(self)
        tree.edit(ACTIONS_C, "@tail bytes @tail-alias Data", "")
        self.refused(tree, "tail")

    def test_args_that_disagree_with_the_handler(self):
        tree = Tree(self)
        tree.edit(ACTIONS_C, "SYS_ACTIONS_OP_RUN @alias Run @description", "SYS_ACTIONS_OP_RUN @alias Run @args begin_t @description")
        self.refused(tree, "SYS_ACTIONS_OP_RUN", "takes")

    def test_a_contract_row_without_its_comment(self):
        tree = Tree(self)
        tree.edit(HB_H, "/* @alias Brake @description Shorts the motor terminals. */", "")
        self.refused(tree, "BRAKE", "@alias")

    def test_an_event_without_an_alias(self):
        tree = Tree(self)
        tree.edit(IO_H, "@alias Rising edge ", "")
        self.refused(tree, "RISING")

    def test_an_unknown_enum_reference(self):
        tree = Tree(self)
        tree.edit(IO_H, "@enum-ref sys_io_mode_e", "@enum-ref no_such_enum_e")
        self.refused(tree, "no_such_enum_e")

    def test_a_wrong_choices_item(self):
        tree = Tree(self)
        tree.edit(IO_H, "@choices 0=Low,1=High", "@choices 0=Low,High", 1)
        self.refused(tree, "@choices")

    def test_two_rows_with_one_number(self):
        tree = Tree(self)
        tree.edit(HB_H, "X(BRAKE, 0x01,", "X(BRAKE, 0x00,")
        self.refused(tree, "used twice")

    def test_a_router_without_a_table_or_dispatch(self):
        tree = Tree(self)
        tree.edit("components/sys/sys_device/sys_device_router.c", "//@dispatch device", "// dispatch device")
        self.refused(tree, "@dispatch")


if __name__ == "__main__":
    unittest.main()
