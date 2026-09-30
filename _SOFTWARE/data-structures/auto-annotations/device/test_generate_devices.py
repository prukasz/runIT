import importlib.util
import tempfile
import unittest
from pathlib import Path

spec = importlib.util.spec_from_file_location("generate_devices", Path(__file__).with_name("generate-devices.py"))
generator = importlib.util.module_from_spec(spec)
spec.loader.exec_module(generator)

DECODERS = generator.PROJECT_ROOT / "components" / "codecs" / "decoders"


class DeviceRecords(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.context = generator.scan_context()
        headers = sorted(p for p in DECODERS.rglob("dec_*.h") if p.name != "dec_device_common.h")
        own = generator.find_device_headers(generator.PROJECT_ROOT / "components" / "devices")
        cls.devices = {device["id"]: device for device in generator.build_devices(headers, cls.context, own)}

    def parse(self, text: str):
        """parse_device_descriptor on a throwaway header."""
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / "dec_device_test.h"
            path.write_text(text, encoding="utf-8")
            _, symbols, defines, sdkconfig = self.context
            return generator.parse_device_descriptor(path, symbols, defines, sdkconfig)

    def test_every_header_generates_and_validates(self):
        self.assertEqual(len(self.devices), 9)
        for device in self.devices.values():
            generator.validate_document(device)

    def test_pin_ref_expands_to_three_wire_fields_and_one_group(self):
        install = self.devices["device_ads_7128"]["install"]["packet_definition"]
        self.assertEqual(install["field_order"], ["device_id", "i2c_bus", "i2c_addr", "intr_pin_device_id", "intr_pin_pin", "intr_pin_mode", "vref_mV"])
        self.assertEqual(install["groups"]["intr_pin"], {"fields": ["intr_pin_device_id", "intr_pin_pin", "intr_pin_mode"], "sentinel_field": "intr_pin_pin", "alias": "ALERT", "note": "Open-drain, active-low."})
        self.assertEqual(install["fields"]["intr_pin_pin"]["sentinel"], 255)
        mode = install["fields"]["intr_pin_mode"]
        self.assertEqual([choice["symbol"] for choice in mode["one_of"]], ["SYS_IO_MODE_INPUT", "SYS_IO_MODE_INPUT_PULLUP"])
        self.assertEqual(mode["default"], mode["one_of"][1]["value"])
        self.assertEqual(install["fields"]["i2c_addr"]["one_of"], list(range(0x10, 0x18)))

    def test_property_alias_flows_to_parameters(self):
        contracts = {contract["packet"]: contract for contract in self.devices["device_ads_7128"]["contracts"]}
        pin = next(p for p in contracts["packet_sys_io_reset_t"]["parameters"] if p["name"] == "pin")
        self.assertEqual(pin, {"name": "pin", "one_of": list(range(8)), "alias": "ADC Channel"})
        self.assertEqual(contracts["packet_sys_io_get_voltage_t"]["returns"], "voltage_mV")
        self.assertTrue(contracts["packet_sys_io_configure_intr_t"]["description"].endswith("reports events to a board GPIO."))

    def test_ranges(self):
        symbols = self.context[1]
        self.assertEqual(generator.parse_choice_list("[0..2, 5, 0x10..0x11]", symbols, "t"), [0, 1, 2, 5, 16, 17])
        for bad in ["[3..1]", "[0..x]", "[0..99999]"]:
            with self.subTest(bad=bad), self.assertRaises(SystemExit):
                generator.parse_choice_list(bad, symbols, "t")

    def test_record_continuation_and_params(self):
        device = self.parse(
            "//#device device_test\n"
            "//  @title       Test\n"
            "//  @description First line\n"
            "//               second line.\n"
            "//#self-property CH\n"
            "//  @one-of [0..3] @alias Channel @note Four inputs.\n"
            "//#contract packet_sys_io_reset_t\n"
            "//  @alias Reset\n"
            "//  @param pin @arg CH @note Own note.\n"
            "// a developer comment ends the record\n"
        )
        self.assertEqual(device["metadata"]["description"], "First line second line.")
        self.assertEqual(device["contracts"][0]["parameters"], [{"name": "pin", "one_of": [0, 1, 2, 3], "alias": "Channel", "note": "Own note."}])

    def test_rejects_what_nothing_reads(self):
        bad_headers = {
            "old one-line form": "//@id device_test\n",
            "unknown record": "//#device device_test\n//  @title T\n//  @description D\n//#widget X\n",
            "tag nothing reads": "//#device device_test\n//  @title T\n//  @description D\n//  @version 1.0.0\n",
            "unknown param tag": "//#device device_test\n//  @title T\n//  @description D\n//#contract packet_sys_io_reset_t\n//  @param pin @role pin\n",
            "returns with a type": "//#device device_test\n//  @title T\n//  @description D\n//#contract packet_sys_io_get_voltage_t\n//  @returns voltage_mV @type int32_t\n",
            "text before any tag": "//#device device_test\n//  loose text\n",
            "missing title": "//#device device_test\n//  @description D\n",
        }
        for name, text in bad_headers.items():
            with self.subTest(name), self.assertRaises(SystemExit):
                self.parse(text)

    def test_template_generates_every_tag(self):
        """device-template.txt must stay a valid header that uses every tag the grammar has."""
        template = Path(__file__).with_name("device-template.txt").read_text(encoding="utf-8")
        with tempfile.TemporaryDirectory() as folder:
            header = Path(folder) / "device" / "dec_device_example.h"
            header.parent.mkdir()
            header.write_text(template, encoding="utf-8")
            headers = sorted(p for p in DECODERS.rglob("dec_*.h") if p.name != "dec_device_common.h" and p.parent.name != "device") + [header]
            (device,) = generator.build_devices(headers, self.context)
        generator.validate_document(device)
        used: dict = {}
        for record in generator.read_records(Path(__file__).with_name("device-template.txt")):
            _, tags, params = generator.parse_record(record)
            used.setdefault(record["kind"], set()).update(tags)
            used.setdefault("param", set()).update(tag for param in params for tag in param["tags"])
        for kind, allowed in {**generator.RECORD_TAGS, "param": generator.PARAM_TAGS}.items():
            # @count-bits only applies to a mask; the reference in the template explains it.
            self.assertEqual(allowed - used[kind] - {"count_bits"}, set(), f"template misses {kind} tags")
        install = device["install"]["packet_definition"]
        self.assertEqual(install["groups"]["intr_pin"]["alias"], "ALERT")
        self.assertEqual(install["fields"]["sample_rate_Hz"], {"type": "uint16_t", "alias": "Sample Rate", "required": False, "min": 1, "max": 3300, "default": 100, "unit": "Hz", "note": "Higher rates react faster and use more power."})
        self.assertEqual(install["fields"]["alert_event"]["default"], install["fields"]["alert_event"]["one_of"][0]["value"])

    def test_pin_ref_rejects_unknown_tags(self):
        with self.assertRaises(SystemExit):
            generator.parse_struct_fields("pin_ref_wire_t intr_pin; //@role pin\n")
        fields = generator.parse_struct_fields("uint8_t a; //@alias A\n                //  @note Long\n                //  text.\npin_ref_wire_t p; //@note N\n")
        self.assertEqual(fields[0].tags, {"alias": "A", "note": "Long text."})
        self.assertEqual([f.name for f in fields], ["a", "p_device_id", "p_pin", "p_mode"])


if __name__ == "__main__":
    unittest.main()
