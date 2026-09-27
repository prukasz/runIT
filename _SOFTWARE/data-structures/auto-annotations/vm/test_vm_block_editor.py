import importlib.util
from pathlib import Path
import unittest

spec = importlib.util.spec_from_file_location("vm_blocks", Path(__file__).with_name("generate-vm-blocks.py"))
generator = importlib.util.module_from_spec(spec)
spec.loader.exec_module(generator)


class BlockEditorMetadata(unittest.TestCase):
    def test_io_metadata_is_generated_from_c_and_validates(self):
        blocks, index = generator.build()
        for block in blocks:
            generator.validate(block, generator.BLOCK_SCHEMA, block["name"])
        generator.validate(index, generator.INDEX_SCHEMA, "index")
        for name, input_index, contract in [("IO_TOGGLE", 0, "packet_sys_io_toggle_t"), ("IO_SET_LEVEL", 1, "packet_sys_io_set_level_t")]:
            block = next(block for block in blocks if block["name"] == "VM_BLK_" + name)
            fields = {field["name"]: field for field in block["state"]["fields"]}
            self.assertEqual(fields["device_id"]["contract"], contract)
            self.assertEqual(fields["default_io_num"]["device_field"], "device_id")
            self.assertTrue(fields["device_id"]["extended_view_show"])
            self.assertTrue(fields["default_io_num"]["extended_view_show"])
            self.assertNotIn("extended_view_show", fields["allowed_mask"])
            self.assertEqual(fields["allowed_mask"]["let_user_select_available"], "default_io_num")
            self.assertEqual(fields["allowed_mask"]["dynamic_input"], input_index)
            self.assertTrue(block["inputs"]["pins"][input_index]["hidden_by_default"])

    def test_invalid_metadata_is_rejected(self):
        for text in ["@hidden-by-default yes", "@extended-view-show yes", "@dynamic-input nope", "@id unknown", "@device-field"]:
            with self.subTest(text=text), self.assertRaises(SystemExit):
                generator.editor_metadata(generator.parse_tags(text), "test")
        block = {"inputs": {"pins": [{"name": "pin", "index": 0, "id_kind": "pin", "device_field": "missing"}]}, "outputs": {"pins": []}}
        with self.assertRaises(SystemExit):
            generator.validate_editor_links(block, "test")


if __name__ == "__main__":
    unittest.main()
