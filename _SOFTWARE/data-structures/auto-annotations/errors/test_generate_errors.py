"""Tests of generate-errors.py: the real tree's catalog decodes the tags the app meets; a wrong map is refused.

Run: python -m unittest data-structures/auto-annotations/errors/test_generate_errors.py
"""
import importlib.util
import json
import shutil
import tempfile
import unittest
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[2]
SPEC = importlib.util.spec_from_file_location("generate_errors", HERE / "generate-errors.py")


def load(root=None):
    module = importlib.util.module_from_spec(SPEC)
    SPEC.loader.exec_module(module)
    if root is not None:
        module.ROOT, module.COMPONENTS_DIR, module.ENUMS_JSON = root, root / "components", root / "data-structures" / "enums.json"
    return module


class Copy:
    """A temp copy of components/ and enums.json, one line broken."""

    def __init__(self, test, path, old, new):
        self.dir = Path(tempfile.mkdtemp(prefix="errors-"))
        test.addCleanup(shutil.rmtree, self.dir, True)
        shutil.copytree(REPO / "components", self.dir / "components", ignore=shutil.ignore_patterns("build"))
        (self.dir / "data-structures").mkdir()
        shutil.copy(REPO / "data-structures" / "enums.json", self.dir / "data-structures" / "enums.json")
        file = self.dir / "components" / path
        text = file.read_text(encoding="utf-8")
        assert old in text, old
        file.write_text(text.replace(old, new, 1), encoding="utf-8", newline="\n")

    def scan(self):
        return load(self.dir).scan()


class RealTree(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.modules = load().scan()
        cls.tags = {t["name"]: t for m in cls.modules for t in m["tags"]}

    def test_a_tag_is_module_shifted_with_its_code(self):
        tag = self.tags["ERR_ROUTER_UNKNOWN_KEY"]
        self.assertEqual((tag["tag"], tag["code"], tag["severity"]), (0x00020003, 3, 3))
        self.assertEqual([(f["name"], f["type"], f["offset"]) for f in tag["fields"]], [("key", "u8", 0)])

    def test_payload_offsets_are_packed(self):
        fields = {f["name"]: (f["type"], f["offset"]) for f in self.tags["ERR_DEV_RANGE"]["fields"]}
        self.assertEqual(fields, {"id": ("u16", 0), "value": ("i32", 2), "min": ("i32", 6), "max": ("i32", 10)})
        self.assertEqual(self.tags["ERR_DEV_RANGE"]["size"], 14)

    def test_field_annotations_reach_the_catalog(self):
        fields = {f["name"]: f for f in self.tags["ERR_VM_OBJ_BAD_TYPE"]["fields"]}
        self.assertEqual(fields["type"]["enum_ref"], "vm_obj_t_e")
        self.assertEqual(self.tags["ERR_VM_BLOCK_FAILED"]["fields"][1]["id_kind"], "vm-block-type")
        self.assertEqual(self.tags["ERR_VM_ACCESSOR_NAME_NOT_FOUND"]["fields"][2], {"name": "name", "type": "chars", "offset": 3, "size": 16})

    def test_every_module_is_there_and_tag_ids_are_unique(self):
        self.assertEqual({m["name"] for m in self.modules} >= {"router", "vm", "cmd", "sys_ble", "project"}, True)
        tags = [t["tag"] for m in self.modules for t in m["tags"]]
        self.assertEqual(len(tags), len(set(tags)))

    def test_output_is_json(self):
        self.assertEqual(json.loads(load().render())["kind"], "errors")


class Refusals(unittest.TestCase):
    def refused(self, path, old, new, message):
        with self.assertRaises(SystemExit) as raised:
            Copy(self, path, old, new).scan()
        self.assertIn(message, str(raised.exception))

    def test_a_tag_of_another_module(self):
        self.refused("core/router/router.h", "ERR_TAG(ERR_MOD_ROUTER, 3)", "ERR_TAG(ERR_MOD_VM, 3)", "belongs to ERR_MOD_VM")

    def test_a_duplicate_id(self):
        self.refused("core/router/router.h", "ERR_TAG(ERR_MOD_ROUTER, 3)", "ERR_TAG(ERR_MOD_ROUTER, 1)", "is also")

    def test_an_unknown_field_type(self):
        self.refused("core/router/router.h", "{ uint8_t key; }", "{ long key; }", "type the catalog does not know")

    def test_an_unpublished_enum(self):
        self.refused("vm/vm_core/errors/vm_err.h", "/*@enum-ref vm_obj_t_e*/", "/*@enum-ref no_such_e*/", "not a published enum")

    def test_an_unknown_severity(self):
        self.refused("core/router/router.h", "ERR_SEV_HIGH,   { uint8_t key; }", "ERR_SEV_WILD,   { uint8_t key; }", "unknown severity")


if __name__ == "__main__":
    unittest.main()
