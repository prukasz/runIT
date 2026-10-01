"""Tests of the VM block generator: the real headers generate clean, the faces come out as designed, and every way of
writing an annotation wrongly is an error rather than a silently dropped fact.

  python -m unittest data-structures/auto-annotations/vm/test_vm_block_editor.py
"""
import contextlib
import importlib.util
import unittest
from pathlib import Path
from unittest import mock

spec = importlib.util.spec_from_file_location("vm_blocks", Path(__file__).with_name("generate-vm-blocks.py"))
generator = importlib.util.module_from_spec(spec)
spec.loader.exec_module(generator)

LIMITS = {"CONFIG_VM_BLOCK_MAX_IN": 8, "CONFIG_VM_BLOCK_MAX_OUT": 8}


def header(*lines):
    return "\n".join(["//#vm-block VM_BLK_X @id 1", *lines, ""])


def entries(*lines):
    """Pin entries as pins_from_enums makes them, from `in 0 a @title A @value bool @required` lines."""
    out = []
    for line in lines:
        side, index, name, rest = ((line[3:] if line.startswith("//@") else line).split(None, 3) + [""])[:4]
        out.append({"side": side, "index": index, "name": name, "tags": generator.parse_tags(rest, generator.PIN_TAGS, "t")[1], "where": "t"})
    return out


@contextlib.contextmanager
def mutated(file_name, old, new):
    """Run the generator on the real headers with `old` replaced by `new` in one of them."""
    real = Path.read_text

    def read_text(self, *args, **kwargs):
        text = real(self, *args, **kwargs)
        if self.name == file_name:
            assert old in text, f"{old!r} not in {file_name}"
            return text.replace(old, new)
        return text

    with mock.patch.object(Path, "read_text", read_text):
        yield


def build_error(file_name, old, new):
    with mutated(file_name, old, new), unittest.TestCase().assertRaises(SystemExit) as raised:
        generator.build()
    return str(raised.exception)


class RealHeaders(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.blocks, cls.index = generator.build()
        cls.block = {b["name"][len("VM_BLK_"):]: b for b in cls.blocks}

    def test_generates_and_validates(self):
        for block in self.blocks:
            generator.validate(block, generator.BLOCK_SCHEMA, block["name"])
        generator.validate(self.index, generator.INDEX_SCHEMA, "index")

    def test_generated_files_are_current(self):
        self.assertEqual(generator.main(["--check"]), 0, "run python data-structures/auto-annotations/generate-all.py")

    def test_a_pin_with_a_constant_is_hidden_until_wired(self):
        timer = self.block["TIMER"]
        pt = next(p for p in timer["inputs"]["pins"] if p["name"] == "pt")
        self.assertEqual(pt["overrides"], "pt")
        self.assertTrue(pt["hidden_by_default"])
        field = next(f for f in timer["state"]["fields"] if f["name"] == "pt")
        self.assertEqual(field["overridden_by"], pt["index"])
        # The FOR block's three inputs all have constants, so none is drawn until it is wired.
        self.assertTrue(all(p["hidden_by_default"] for p in self.block["FOR"]["inputs"]["pins"]))

    def test_timer_has_no_q_pin(self):
        self.assertEqual([p["name"] for p in self.block["TIMER"]["outputs"]["pins"]], ["et"])
        self.assertEqual(self.block["TIMER"]["eno"]["title"], "Q")

    def test_faces_come_from_the_headers(self):
        def words(template):
            return "".join(p["text"] if "text" in p else "{" + p["ref"] + "}" for p in template)
        self.assertEqual(words(self.block["PERIODIC"]["header"]["lead"]), "Every")
        self.assertEqual(words(self.block["PERIODIC"]["header"]["value"]), "{period} {time_base}")
        self.assertEqual(words(self.block["TIMER"]["header"]["value"]), "{mode} {pt} {time_base}")
        self.assertEqual(words(self.block["FOR"]["header"]["value"]), "{start} to {end} ({cmp}) step {op}{step}")
        self.assertNotIn("header", self.block["IF"])
        # The face is the header; there are no text rows or body lines any more.
        for block in self.blocks:
            self.assertFalse({"body", "rows", "view"} & set(block), block["name"])

    def test_the_detailed_view_is_the_hard_coded_inputs(self):
        # Inputs with a constant are the detailed view; nothing else needs annotating for it.
        with_constant = sorted(name for name, block in self.block.items() if any(p.get("overrides") for p in block["inputs"]["pins"]))
        for name, block in self.block.items():  # every one of them is hidden until wired, whichever block it is
            for pin in block["inputs"]["pins"]:
                self.assertEqual(bool(pin.get("overrides")), bool(pin.get("hidden_by_default")) and "overrides" in pin, (name, pin["name"]))
        self.assertIn("TIMER", with_constant)

    def test_the_old_detailed_view_tags_are_refused(self):
        self.assertIn("unknown directive //@view", build_error("vm_block_timer.h", "//@activation enabled", "//@view simple\n//@activation enabled"))
        self.assertIn("unknown directive //@body", build_error("vm_block_timer.h", "//@activation enabled", "//@body text\n//@activation enabled"))
        self.assertIn("unknown tag @extended-view-show", build_error("vm_block_timer.h", "@enum-ref vm_timer_mode_e", "@enum-ref vm_timer_mode_e @extended-view-show"))

    def test_examples_are_assembled_into_the_computed_state_layout(self):
        for example in self.block["EXPR"]["encoding"]["examples"]:
            data = bytes.fromhex(example["custom_data"])
            self.assertEqual(len(data), example["custom_len"])
            self.assertEqual(data[0], len(example["constants"]))  # const_cnt, at its computed offset


class Scanning(unittest.TestCase):
    def test_the_directives_stand_anywhere_in_the_header_and_belong_to_its_block(self):
        text = "//@data s_t\ntypedef struct s_t {int a;} s_t;\n\n// a note\n//#vm-block VM_BLK_X @id 1\n//@activation enabled\nvoid f(void);\n//@rule r @error E\n"
        block = generator.scan_block("h", text)
        self.assertEqual((block["name"], block["id"]), ("X", 1))
        self.assertEqual([d["keyword"] for d in block["directives"]], ["data", "activation", "rule"])

    def test_a_shared_header_has_no_block(self):
        self.assertIsNone(generator.scan_block("h", "// shared code\ntypedef int t;\n"))
        with self.assertRaisesRegex(SystemExit, "without a `//#vm-block"):
            generator.scan_block("h", "//@activation enabled\n")

    def test_one_block_per_header(self):
        with self.assertRaisesRegex(SystemExit, "one block per header"):
            generator.scan_block("h", header("//@activation enabled") + header("//@activation enabled"))

    def test_face_directives_belong_in_the_display_json(self):
        for keyword in ("title", "category", "block-description", "header", "eno", "always-detailed", "in", "out"):
            with self.subTest(keyword), self.assertRaisesRegex(SystemExit, "belong in the <name>.display.json"):
                generator.scan_block("h", header(f"//@{keyword} x"))

    def test_unknown_directive_and_old_one_line_form_are_refused(self):
        with self.assertRaisesRegex(SystemExit, "unknown directive //@activaton"):
            generator.scan_block("h", header("//@activaton x"))
        with self.assertRaisesRegex(SystemExit, "takes the block symbol and its id"):
            generator.scan_block("h", "//#vm-block VM_BLK_X @id 1 @title X\n")

    def test_the_id_is_part_of_the_directive_line(self):
        with self.assertRaisesRegex(SystemExit, "symbol and its id"):
            generator.scan_block("h", "//#vm-block VM_BLK_X\n//@activation enabled\n")
        self.assertEqual(generator.scan_block("h", header("//@activation enabled"))["id"], 1)

    def test_ids_are_unique_and_fit_a_byte(self):
        self.assertIn("both have @id", build_error("vm_block_edge.h", "VM_BLK_EDGE @id 8", "VM_BLK_EDGE @id 9"))
        self.assertIn("must be 1 to 255", build_error("vm_block_edge.h", "VM_BLK_EDGE @id 8", "VM_BLK_EDGE @id 0"))
        self.assertIn("must be 1 to 255", build_error("vm_block_edge.h", "VM_BLK_EDGE @id 8", "VM_BLK_EDGE @id 256"))

    def test_the_c_registry_lists_every_block_by_id(self):
        blocks, _ = generator.build()
        registry = generator.render_registry(blocks).decode("utf-8")
        ids = generator.render_ids(blocks).decode("utf-8")
        for block in blocks:
            self.assertIn(f"[{block['name']}] = {{.run = vm_blk_{block['name'][len('VM_BLK_'):].lower()}", registry)
            self.assertIn(f"#define {block['name']} {block['id']} ", ids)
        self.assertNotIn("[VM_BLK_NONE]", registry)

    def test_the_registry_entries_carry_the_declared_shape(self):
        blocks, _ = generator.build()
        entry = {b["name"]: generator.c_entry(b) for b in blocks}
        self.assertIn(".min_in = 1, .max_in = 2, .min_q = 0, .max_q = 1, .min_en = 0, .required_in = 0x1u, .state_len = sizeof(vm_block_timer_data_t)", entry["VM_BLK_TIMER"])
        self.assertIn(".min_en = 1", entry["VM_BLK_EDGE"])          # //@enables required
        self.assertIn(".min_en = 1", entry["VM_BLK_LATCH"])
        self.assertIn(".max_q = CONFIG_VM_BLOCK_MAX_OUT", entry["VM_BLK_SWITCH"])  # a `*` output
        self.assertIn(".min_q = 1", entry["VM_BLK_SWITCH"])                       # a required `*` pin
        self.assertIn(".min_q = 2, .max_q = 2", entry["VM_BLK_IF"])
        self.assertIn(".state_len = 0", entry["VM_BLK_IF"])
        self.assertIn(".check = vm_verify_timer", entry["VM_BLK_TIMER"])
        self.assertNotIn(".check", entry["VM_BLK_IF"])


class Tags(unittest.TestCase):
    def test_unknown_repeated_and_valued_flag_tags_fail(self):
        with self.assertRaisesRegex(SystemExit, "unknown tag @hidden-by-defualt"):
            generator.parse_tags("@title A @hidden-by-defualt", generator.PIN_TAGS, "t")
        with self.assertRaisesRegex(SystemExit, "given twice"):
            generator.parse_tags("@title A @title B", generator.PIN_TAGS, "t")
        with self.assertRaisesRegex(SystemExit, "takes no value"):
            generator.parse_tags("@hidden-by-default yes", generator.PIN_TAGS, "t")

    def test_an_at_sign_inside_a_word_is_text_not_a_tag(self):
        lead, tags = generator.parse_tags("mail a@b.c @title A", generator.PIN_TAGS, "t")
        self.assertEqual((lead, tags), ("mail a@b.c", {"title": "A"}))


class Pins(unittest.TestCase):
    def parse(self, *lines, text=""):
        return generator.parse_pins(entries(*lines), text, LIMITS, "t")

    def test_shape_comes_from_the_required_pins(self):
        inputs, outputs, shape = self.parse("//@in 0 a @title A @value bool @required", "//@in 1 b @title B @value bool", "//@out 0 q @title Q @value bool @required")
        self.assertTrue(inputs[0]["required"])
        self.assertFalse(inputs[1]["required"])
        self.assertEqual(shape, {"min_in": 1, "max_in": 2, "min_q": 1, "max_q": 1, "required_in": 1})

    def test_a_required_pin_raises_the_minimum_to_its_index(self):
        _, _, shape = self.parse("//@in 0 a @title A @value bool", "//@in 1 b @title B @value bool @required", "//@in 2 c @title C @value bool @required")
        self.assertEqual((shape["min_in"], shape["required_in"]), (3, 6))

    def test_a_star_pin_lifts_the_maximum_to_the_limit_and_required_asks_for_one(self):
        _, _, shape = self.parse("//@in * pin @title P @value f32", "//@out 0 a @title A @value bool", "//@out * b @title B @value gate @required")
        self.assertEqual((shape["max_in"], shape["min_in"]), ("CONFIG_VM_BLOCK_MAX_IN", 0))
        self.assertEqual((shape["min_q"], shape["max_q"]), (2, "CONFIG_VM_BLOCK_MAX_OUT"))

    def test_the_enables_directive_is_checked(self):
        self.assertIn("is `required`", build_error("vm_block_edge.h", "//@enables required", "//@enables sometimes"))

    def test_pin_lines_are_checked(self):
        cases = {
            "gaps": (("//@in 0 a @title A @value bool", "//@in 2 b @title B @value bool"), "without gaps"),
            "duplicate names": (("//@in 0 a @title A @value bool", "//@out 0 a @title A @value bool"), "used twice"),
            "duplicate index": (("//@in 0 a @title A @value bool", "//@in 0 b @title B @value bool"), "share an index"),
            "star not last": (("//@in * a @title A @value bool", "//@in 0 b @title B @value bool"), "must come last"),
            "past the limit": (("//@in 0 a @title A @value bool", *[f"//@in {i} p{i} @title P @value bool" for i in range(1, 9)]), "past CONFIG_VM_BLOCK_MAX_IN"),
            "no title": (("//@in 0 a @value bool",), "needs @title"),
            "bad value kind": (("//@in 0 a @title A @value float",), "needs @value"),
        }
        for name, (lines, message) in cases.items():
            with self.subTest(name), self.assertRaisesRegex(SystemExit, message):
                self.parse(*lines)

    def test_overrides_ties_a_pin_to_a_setting_and_hides_it(self):
        inputs, _, _ = self.parse("//@in 0 a @title A @value bool @required", "//@in 1 b @title B @value u32 @overrides limit")
        fields = [{"name": "limit", "c_type": "uint32_t", "source": "user"}]
        generator.link_overrides(inputs, fields, "t")
        self.assertTrue(inputs[1]["hidden_by_default"])
        self.assertEqual(fields[0]["overridden_by"], 1)
        for bad_field, message in (({"name": "limit", "c_type": "float", "source": "user"}, "cannot replace"), ({"name": "limit", "c_type": "uint32_t", "source": "runtime"}, "user state field")):
            with self.subTest(message), self.assertRaisesRegex(SystemExit, message):
                generator.link_overrides(self.parse("//@in 0 a @title A @value bool", "//@in 1 b @title B @value u32 @overrides limit")[0], [bad_field], "t")
        with self.assertRaisesRegex(SystemExit, "user state field"):
            generator.link_overrides(self.parse("//@in 0 a @title A @value bool", "//@in 1 b @title B @value u32 @overrides nothing")[0], fields, "t")

    def test_a_hidden_pin_needs_a_way_to_set_it(self):
        block = {"inputs": {"pins": [{"name": "p", "index": 0, "hidden_by_default": True}]}, "outputs": {"pins": []}}
        with self.assertRaisesRegex(SystemExit, "hidden input needs"):
            generator.validate_editor_links(block, "t")

    def test_editor_links_are_checked(self):
        block = {"inputs": {"pins": [{"name": "pin", "index": 0, "id_kind": "pin", "device_field": "missing"}]}, "outputs": {"pins": []}}
        with self.assertRaises(SystemExit):
            generator.validate_editor_links(block, "test")


class Templates(unittest.TestCase):
    scope = {
        "inputs": {"a": {"index": 0, "name": "a"}, "pt": {"index": 1, "name": "pt", "overrides": "pt"}, "n": {"index": 2, "name": "n"}},
        "outputs": {"q": {"index": 0, "name": "q"}},
        "fields": {"pt": {"name": "pt", "source": "user"}, "n": {"name": "n", "source": "user"}, "rt": {"name": "rt", "source": "runtime"}},
        "opcodes": None,
    }

    def parse(self, text):
        return generator.parse_template(text, self.scope, "t")

    def test_refs_resolve_to_pins_and_settings(self):
        self.assertEqual(self.parse("x {a}")[1], {"ref": "a", "kind": "pin", "pin": 0})
        self.assertEqual(self.parse("{pt}")[0], {"ref": "pt", "kind": "pin", "pin": 1, "field": "pt"})
        self.assertEqual(self.parse("{q}")[0], {"ref": "q", "kind": "out", "pin": 0})

    def test_bad_templates_fail(self):
        for text, message in (("{nope}", "names no pin or setting"), ("{rt}", "not a setting"), ("{n}", "both an input pin and a state field"),
                              ("open { brace", "stray brace"), ("", "empty template"), ("{expression}", "names no pin or setting"), ("{span}", "names no pin or setting")):
            with self.subTest(text), self.assertRaisesRegex(SystemExit, message):
                self.parse(text)


PROBE_HEADER = """#pragma once
#include "vm_block_helpers.h"

//#block-enum @alias Probe Mode
typedef enum vm_probe_mode_e {
  VM_PROBE_MODE_A = 0,  //@alias A
  VM_PROBE_MODE_B,      //@alias B
  VM_PROBE_MODE_CNT,
} vm_probe_mode_e;

//@data vm_probe_data_t
typedef struct __attribute__((aligned(4))) {
  uint8_t mode;     // @description Which one @enum-ref vm_probe_mode_e
  uint8_t flags;    // @runtime
  uint16_t gain;    // @description Gain when the Gain input is unwired
} vm_probe_data_t;
_Static_assert(sizeof(vm_probe_data_t) == 4, "vm_probe_data_t");

//#block-enum @alias Probe Inputs
typedef enum vm_in_probe_e {
  VM_IN_PROBE_GAIN = 0,  //@in @value u32 @overrides gain
  VM_IN_PROBE_ARG_ANY,   //@in @value f32 @repeat
} vm_in_probe_e;

//#block-enum @alias Probe Outputs
typedef enum vm_out_probe_e {
  VM_OUT_PROBE_RESULT = 0,  //@out @value f32 @required
} vm_out_probe_e;

//@rule mode is a vm_probe_mode_e value. @error ERR_VM_BLK_BAD_SHAPE
bool vm_verify_probe(vm_block_h b);
//#vm-block VM_BLK_PROBE @id 250
//@activation triggered
//@enables required
void vm_blk_probe(vm_block_h b);
"""
PROBE_DISPLAY = """{
  "title": "Probe", "category": "data", "description": "A block the generator has never seen.",
  "header": "Probe | {mode}",
  "inputs": {"gain": {"title": "Gain"}, "arg": {"title": "Argument", "description": "Any number."}},
  "outputs": {"result": {"title": "Result"}}
}
"""


class NewBlock(unittest.TestCase):
    """A block the tool has never seen generates with no change to the tool: nothing in it names a block."""

    @classmethod
    def setUpClass(cls):
        cls.folder = generator.BLOCKS_DIR / "zz_probe"
        cls.folder.mkdir(exist_ok=True)
        (cls.folder / "vm_block_zz_probe.h").write_text(PROBE_HEADER.replace("probe", "zz_probe").replace("PROBE", "ZZ_PROBE"), encoding="utf-8", newline="\n")
        (cls.folder / "zz_probe.display.json").write_text(PROBE_DISPLAY, encoding="utf-8", newline="\n")
        generator.DISPLAY_BLOCKS.clear()
        try:
            cls.blocks, cls.index = generator.build()
        finally:
            for path in cls.folder.iterdir():
                path.unlink()
            cls.folder.rmdir()

    def test_the_new_block_has_its_shape_pins_and_enums(self):
        block = next(b for b in self.blocks if b["name"] == "VM_BLK_ZZ_PROBE")
        self.assertEqual(block["id"], 250)
        self.assertEqual([(p["index"], p["name"]) for p in block["inputs"]["pins"]], [(0, "gain"), ("*", "arg")])
        self.assertEqual((block["inputs"]["max"], block["outputs"]["min"]), (generator.load_sdkconfig()["CONFIG_VM_BLOCK_MAX_IN"], 1))
        self.assertEqual([f["source"] for f in block["state"]["fields"]], ["user", "runtime", "user"])
        self.assertEqual(sorted(block["enums"]), ["vm_in_zz_probe_e", "vm_out_zz_probe_e", "vm_zz_probe_mode_e"])

    def test_the_new_block_is_registered_without_a_central_edit(self):
        block = next(b for b in self.blocks if b["name"] == "VM_BLK_ZZ_PROBE")
        self.assertIn(".min_en = 1", generator.c_entry(block))
        self.assertIn("vm_blk_zz_probe", generator.render_registry(self.blocks).decode("utf-8"))
        self.assertIn("#define VM_BLK_ZZ_PROBE 250", generator.render_ids(self.blocks).decode("utf-8"))


class Mistakes(unittest.TestCase):
    """The same headers, written wrongly, must not generate."""

    def test_missing_static_assert(self):
        self.assertIn("_Static_assert(sizeof(vm_block_timer_data_t)", build_error("vm_block_timer.h", "_Static_assert(sizeof(vm_block_timer_data_t) == 32,", "// (sizeof(vm_block_timer_data_t) == 32,"))

    def test_wrong_offset_assert(self):
        self.assertIn("computes to offset", build_error("vm_block_periodic.h", "offsetof(vm_block_periodic_data_t, time_base) == 4", "offsetof(vm_block_periodic_data_t, time_base) == 5"))

    def test_pin_enum_values_are_the_pin_indexes(self):
        self.assertRegex(build_error("vm_block_timer.h", "VM_IN_TIMER_IN = 0", "VM_IN_TIMER_IN = 1"), "without gaps")

    def test_typo_in_a_tag(self):
        self.assertIn("unknown tag @overides", build_error("vm_block_timer.h", "@overrides pt", "@overides pt"))

    def test_overrides_names_no_setting(self):
        self.assertIn("must name a user state field", build_error("vm_block_timer.h", "@overrides pt", "@overrides nothing"))

    def test_the_header_of_a_block_with_a_display_json_refuses_the_face(self):
        self.assertIn("belong in the <name>.display.json", build_error("vm_block_timer.h", "//@activation enabled", "//@title Timer\n//@activation enabled"))

    def test_a_pin_member_needs_its_tag(self):
        self.assertIn("needs a trailing", build_error("vm_block_if.h", "//@in @value bool @required", ""))

    def test_a_repeating_pin_is_valued_after_the_numbered_ones(self):
        self.assertIn("after 0 numbered pin(s) it must be 0", build_error("vm_block_expr.h", "VM_IN_EXPR_PIN_ANY = 0", "VM_IN_EXPR_PIN_ANY = 2"))

    def test_a_repeating_pin_member_is_named_any(self):
        self.assertIn("_ANY", build_error("vm_block_expr.h", "VM_IN_EXPR_PIN_ANY", "VM_IN_EXPR_PIN_X"))

    def test_the_display_json_keys_are_checked(self):
        self.assertIn("unknown key", build_error("if.display.json", '"title": "Condition"', '"titl": "Condition"'))

    def test_a_dropped_pin_line_is_noticed(self):
        message = build_error("vm_block_if.h", "VM_IN_IF_CONDITION = 0", "VM_IN_IF_CONDITION = 3")
        self.assertRegex(message, "without gaps")

    def test_reference_to_a_missing_pin(self):
        self.assertIn("names no pin or setting", build_error("timer.display.json", "{mode} {pt} {time_base}", "{mode} {preset}"))


if __name__ == "__main__":
    unittest.main()
