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

SHAPE = {"min_in": 1, "min_q": 0, "required_in": 1}
LIMITS = {"CONFIG_VM_BLOCK_MAX_IN": 8, "CONFIG_VM_BLOCK_MAX_OUT": 8}


def header(*lines, macro="#define VM_BLOCK_TYPE_X \\\n  {.min_in = 1, .required_in = 0x1u}"):
    return "\n".join(["//#vm-block VM_BLK_X", *lines, macro, ""])


def directives(*lines):
    return [{"keyword": line[3:].split()[0], "rest": line[3:].partition(" ")[2].strip(), "line": i + 1} for i, line in enumerate(lines)]


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
        self.assertEqual(with_constant, ["ACTION", "EDGE", "FOR", "IO_SET_LEVEL", "IO_TOGGLE", "PERIODIC", "TIMER"])

    def test_the_old_detailed_view_tags_are_refused(self):
        self.assertIn("unknown directive //@view", build_error("vm_block_timer.h", "//@header Timer |", "//@view simple\n//@header Timer |"))
        self.assertIn("unknown directive //@body", build_error("vm_block_timer.h", "//@header Timer |", "//@body text\n//@header Timer |"))
        self.assertIn("unknown tag @extended-view-show", build_error("vm_block_timer.h", "@enum-ref vm_timer_mode_e", "@enum-ref vm_timer_mode_e @extended-view-show"))

    def test_examples_are_assembled_into_the_computed_state_layout(self):
        for example in self.block["EXPR"]["encoding"]["examples"]:
            data = bytes.fromhex(example["custom_data"])
            self.assertEqual(len(data), example["custom_len"])
            self.assertEqual(data[0], len(example["constants"]))  # const_cnt, at its computed offset


class Scanning(unittest.TestCase):
    def test_blank_lines_and_plain_comments_inside_the_block_are_fine(self):
        text = header("//@title X", "", "// a note", "//@category flow")
        [block] = generator.scan_blocks("h", text)
        self.assertEqual([d["keyword"] for d in block["directives"]], ["title", "category"])
        self.assertEqual(block["macro"], {"min_in": 1, "min_q": 0, "required_in": 1})

    def test_a_directive_above_the_block_is_refused(self):
        with self.assertRaisesRegex(SystemExit, "not inside a //#vm-block"):
            generator.scan_blocks("h", "//@in 0 a @title A @value bool\n\n" + header("//@title X"))

    def test_unknown_directive_and_old_one_line_form_are_refused(self):
        with self.assertRaisesRegex(SystemExit, "unknown directive //@titel"):
            generator.scan_blocks("h", header("//@titel X"))
        with self.assertRaisesRegex(SystemExit, "one fact per following"):
            generator.scan_blocks("h", "//#vm-block VM_BLK_X @title X\n#define VM_BLOCK_TYPE_X {}\n")

    def test_the_block_must_sit_right_above_its_macro(self):
        with self.assertRaisesRegex(SystemExit, "must end right above"):
            generator.scan_blocks("h", "//#vm-block VM_BLK_X\n//@title X\nint unrelated;\n#define VM_BLOCK_TYPE_X {}\n")

    def test_macro_fields_default_to_zero_and_take_constant_expressions(self):
        [block] = generator.scan_blocks("h", header("//@title X", macro="#define VM_BLOCK_TYPE_X {.run = f, .required_in = (1u << 0) | (1u << 2)}"))
        self.assertEqual(block["macro"], {"min_in": 0, "min_q": 0, "required_in": 5})
        with self.assertRaisesRegex(SystemExit, "cannot read"):
            generator.scan_blocks("h", header("//@title X", macro="#define VM_BLOCK_TYPE_X {.min_in = sizeof(int)}"))


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
    def parse(self, *lines, shape=SHAPE, text=""):
        return generator.parse_pins(directives(*lines), shape, text, LIMITS, "t")

    def test_shape_comes_from_the_macro(self):
        inputs, _ = self.parse("//@in 0 a @title A @value bool")
        self.assertTrue(inputs[0]["required"])

    def test_pin_lines_are_checked(self):
        cases = {
            "gaps": (("//@in 0 a @title A @value bool", "//@in 2 b @title B @value bool"), "without gaps"),
            "duplicate names": (("//@in 0 a @title A @value bool", "//@out 0 a @title A @value bool"), "used twice"),
            "duplicate index": (("//@in 0 a @title A @value bool", "//@in 0 b @title B @value bool"), "share an index"),
            "star not last": (("//@in * a @title A @value bool", "//@in 0 b @title B @value bool"), "must come last"),
            "past the limit": (("//@in 0 a @title A @value bool", *[f"//@in {i} p{i} @title P @value bool" for i in range(1, 9)]), "past CONFIG_VM_BLOCK_MAX_IN"),
            "no title": (("//@in 0 a @value bool",), "needs @title"),
            "bad value kind": (("//@in 0 a @title A @value float",), "needs @value"),
            "text before tags": (("//@in 0 a stray @title A @value bool",), "unexpected text"),
            "required pin missing": (("//@out 0 q @title Q @value bool",), "the macro needs 1 inputs"),
        }
        for name, (lines, message) in cases.items():
            with self.subTest(name), self.assertRaisesRegex(SystemExit, message):
                self.parse(*lines)

    def test_a_pin_index_must_match_the_c_macro(self):
        text = "#define VM_X_IN_A 1u\n"
        with self.assertRaisesRegex(SystemExit, "is 1, the annotation says 0"):
            self.parse("//@in 0 a @title A @value bool @macro VM_X_IN_A", text=text)
        with self.assertRaisesRegex(SystemExit, "is not defined"):
            self.parse("//@in 0 a @title A @value bool @macro VM_X_IN_B", text=text)

    def test_overrides_ties_a_pin_to_a_setting_and_hides_it(self):
        inputs, _ = self.parse("//@in 0 a @title A @value bool", "//@in 1 b @title B @value u32 @overrides limit", shape={"min_in": 1, "min_q": 0, "required_in": 1})
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


class Mistakes(unittest.TestCase):
    """The same headers, written wrongly, must not generate."""

    def test_missing_static_assert(self):
        self.assertIn("_Static_assert(sizeof(vm_block_timer_data_t)", build_error("vm_block_timer.h", "_Static_assert(sizeof(vm_block_timer_data_t) == 32,", "// (sizeof(vm_block_timer_data_t) == 32,"))

    def test_wrong_offset_assert(self):
        self.assertIn("computes to offset", build_error("vm_block_periodic.h", "offsetof(vm_block_periodic_data_t, time_base) == 4", "offsetof(vm_block_periodic_data_t, time_base) == 5"))

    def test_annotation_and_c_macro_disagree(self):
        self.assertIn("the annotation says 0", build_error("vm_block_timer.h", "#define VM_TIMER_ET 0u", "#define VM_TIMER_ET 1u"))

    def test_typo_in_a_tag(self):
        self.assertIn("unknown tag @overides", build_error("vm_block_timer.h", "@overrides pt", "@overides pt"))

    def test_overrides_names_no_setting(self):
        self.assertIn("must name a user state field", build_error("vm_block_timer.h", "@overrides pt", "@overrides nothing"))

    def test_annotation_above_the_directive(self):
        self.assertIn("not inside a //#vm-block", build_error("vm_block_timer.h", "//#vm-block VM_BLK_TIMER", "//@in 2 stray @title S @value bool\n//#vm-block VM_BLK_TIMER"))

    def test_a_dropped_pin_line_is_noticed(self):
        message = build_error("vm_block_latch.h", "//@in 0 set @title Set @value bool @macro VM_LATCH_IN_SET\n", "")
        self.assertRegex(message, "without gaps|below min_in")

    def test_reference_to_a_missing_pin(self):
        self.assertIn("names no pin or setting", build_error("vm_block_timer.h", "//@header Timer | {mode} {pt} {time_base}", "//@header Timer | {mode} {preset}"))


if __name__ == "__main__":
    unittest.main()
