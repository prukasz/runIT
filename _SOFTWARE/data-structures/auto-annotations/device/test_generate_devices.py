"""Tests of generate-devices.py: the real tree builds the descriptors the app relies on; wrongly annotated devices are refused.

Run: python -m unittest data-structures/auto-annotations/device/test_generate_devices.py
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
SPEC = importlib.util.spec_from_file_location("generate_devices", HERE / "generate-devices.py")


def load(root=None):
    module = importlib.util.module_from_spec(SPEC)
    SPEC.loader.exec_module(module)
    if root is not None:
        module.ROOT, module.COMPONENTS_DIR, module.SDKCONFIG = root, root / "components", root / "sdkconfig"
        module.DEVICES_DIR, module.OUT_DIR = module.COMPONENTS_DIR / "devices", root / "data-structures" / "devices"
        module.CONTRACTS_PATH = REPO / "data-structures" / "auto-annotations" / "contracts" / "generate-contracts.py"
        module.ENUMS_PATH = REPO / "data-structures" / "auto-annotations" / "enums" / "generate-enums.py"
    return module


class Tree:
    def __init__(self, test):
        self.dir = Path(tempfile.mkdtemp(prefix="devices-"))
        test.addCleanup(shutil.rmtree, self.dir, True)
        shutil.copytree(REPO / "components", self.dir / "components", ignore=shutil.ignore_patterns("build", "*.o"))
        shutil.copy(REPO / "sdkconfig", self.dir / "sdkconfig")

    def edit(self, relative, old, new, count=1):
        path = self.dir / relative
        text = path.read_text(encoding="utf-8").replace("\r\n", "\n")
        assert old in text, f"{relative}: `{old[:60]}` not found"
        path.write_text(text.replace(old, new, count), encoding="utf-8", newline="\n")

    def build(self):
        return load(self.dir).outputs()


PCA_H = "components/devices/device_pca9685/device_pca9685.h"
PCA_C = "components/devices/device_pca9685/device_pca9685.c"
SERVO_H = "components/devices/device_servo/device_servo.h"
SERVO_C = "components/devices/device_servo/device_servo.c"
DRV_H = "components/devices/device_drv8962/device_drv8962.h"


class RealTree(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        module = load()
        cls.files = module.outputs()
        cls.devices = {p.name[:-len(".generated.json")]: json.loads(c) for p, c in cls.files.items() if p.name.endswith(".generated.json")}

    def test_every_device_is_described(self):
        self.assertEqual(len(self.devices), 11)
        for device in self.devices.values():
            self.assertTrue(device["alias"] and device["description"] and device["protocols"], device["id"])
            self.assertIn("id", device["type"])
        ids = [d["type"]["id"] for d in self.devices.values()]
        self.assertEqual(len(ids), len(set(ids)))

    def test_every_field_the_user_fills_has_an_alias(self):
        def walk(fields, where):
            for f in fields:
                self.assertTrue(f.get("alias"), f"{where}.{f['name']}")
                walk(f.get("fields", []), f"{where}.{f['name']}")
        for device in self.devices.values():
            walk(device["create"]["fields"], device["id"])
            for op in device.get("own_ops", []):
                self.assertTrue(op["alias"] and op["description"])

    def test_the_create_struct_is_laid_out_and_resolved(self):
        pca = self.devices["device_pca9685"]
        self.assertEqual((pca["type"]["id"], pca["create"]["size"]), (0x41, 5))
        addr = {f["name"]: f for f in pca["create"]["fields"]}["i2c_addr"]
        self.assertEqual(addr["one_of"][0], 0x40)
        self.assertNotIn(0x70, addr["one_of"])
        oe = {f["name"]: f for f in pca["create"]["fields"]}["oe_pin"]
        self.assertTrue(oe["pin_ref"])
        self.assertEqual(oe["default_mode"], 3)
        self.assertEqual(self.devices["device_gpio_esp"]["create"]["fields"], [])

    def test_contract_ops_come_from_the_catalog_with_the_devices_narrowing(self):
        pca = self.devices["device_pca9685"]
        io = next(c for c in pca["contracts"] if c["contract"] == "IO")
        ops = {o["name"]: o for o in io["ops"]}
        self.assertEqual(ops["SET_LEVEL"]["id"], 0x0100)
        pin = ops["SET_LEVEL"]["params"][0]
        self.assertEqual((pin["field"], pin["alias"], len(pin["one_of"])), ("pin", "PWM channel", 16))
        freq = {p["field"]: p for p in ops["SET_PWM_FREQ"]["params"]}
        self.assertTrue(freq["pin"]["device_wide"])
        self.assertEqual((freq["frequency_Hz"]["min"], freq["frequency_Hz"]["max"]), (24, 1526))
        self.assertNotIn("SET_VOLTAGE", ops)

    def test_the_pins_macro_and_internal_ops_are_expanded_or_skipped(self):
        gpio = self.devices["device_gpio_esp"]
        pins = next(c for c in gpio["contracts"] if c["contract"] == "PINS")
        self.assertEqual([o["name"] for o in pins["ops"]], ["AVAILABLE", "LOCKED", "LOCK", "UNLOCK", "SET_LOCKED_LEVEL"])
        names = [o["name"] for c in self.devices["device_tps55289"]["contracts"] for o in c["ops"]]
        self.assertNotIn("ON_EVENT", names)
        self.assertEqual([o["symbol"] for o in self.devices["device_tps55289"]["own_ops"]], ["TPS55289_OP_SET_PROTECTION", "TPS55289_OP_GET_PROTECTION"])

    def test_own_ops_have_ids_arguments_and_answers(self):
        ops = {o["symbol"]: o for o in self.devices["device_servo"]["own_ops"]}
        self.assertEqual(ops["SERVO_OP_SET"]["id"], 0xFF02)
        self.assertEqual(ops["SERVO_OP_SET"]["args"]["fields"][0]["name"], "value_x10")
        self.assertEqual(ops["SERVO_OP_GET_PULSE"]["returns"]["unit"], "us")
        fault = {o["symbol"]: o for o in self.devices["device_drv8962"]["own_ops"]}["DRV8962_OP_GET_FAULT"]
        self.assertEqual([f["name"] for f in fault["returns"]["fields"]], ["fault", "reason"])

    def test_values_by_setting(self):
        position = self.devices["device_servo"]["values"]["POSITION"]
        self.assertEqual([v["title"] for v in position], ["Byte", "Speed", "Angle"])
        self.assertEqual(position[2]["range"], {"min": {"field": "angle_min_deg"}, "max": {"field": "angle_max_deg"}})
        self.assertEqual(position[1]["when"], [{"field": "servo_type", "value": 1}])

    def test_events_with_their_channel(self):
        events = self.devices["device_ina3221"]["events"][0]
        self.assertEqual([e["symbol"] for e in events["events"]], ["PMON_EV_OCP_CRITICAL", "PMON_EV_OCP_WARNING"])
        self.assertEqual((events["channel"]["alias"], events["channel"]["one_of"]), ("Monitor channel", [0, 1, 2]))

    def test_an_array_of_pins_is_named_per_element(self):
        pins = {f["name"]: f for f in self.devices["device_drv8962"]["create"]["fields"]}["pin"]
        self.assertEqual(pins["array_len"], 15)
        self.assertEqual(pins["aliases"][0], "IN1")
        self.assertEqual(pins["aliases"][14], "VREF")
        self.assertEqual(len(pins["modes"]), 15)
        self.assertEqual(pins["modes"][4][0]["symbol"], "SYS_IO_MODE_OUTPUT_PUSH_PULL")

    def test_the_gpio_timer_count(self):
        self.assertEqual(self.devices["device_gpio_esp"]["pwm_frequencies"], 4)


class Refusals(unittest.TestCase):
    def refused(self, tree, *words):
        with self.assertRaises(SystemExit) as raised:
            tree.build()
        for word in words:
            self.assertIn(word, str(raised.exception))

    def test_a_record_without_a_description(self):
        tree = Tree(self)
        tree.edit(SERVO_H, "//@description Hobby servo", "//@descriptions Hobby servo")
        self.refused(tree, "descriptions")

    def test_an_unknown_protocol(self):
        tree = Tree(self)
        tree.edit(PCA_H, "//@protocol i2c", "//@protocol carrier-pigeon")
        self.refused(tree, "protocol")

    def test_a_field_without_an_alias(self):
        tree = Tree(self)
        tree.edit(PCA_H, "//@alias I2C address @description", "//@description")
        self.refused(tree, "i2c_addr", "@alias")

    def test_a_default_outside_one_of(self):
        tree = Tree(self)
        tree.edit(PCA_H, "@one-of [0x40..0x6F, 0x71..0x77]", "@one-of [0x40..0x6F, 0x71..0x77] @default 0x70")
        self.refused(tree, "@default")

    def test_a_default_mode_outside_modes(self):
        tree = Tree(self)
        tree.edit("components/devices/device_ina3221/device_ina3221.h", "@default-mode $SYS_IO_MODE_INPUT_PULLUP", "@default-mode $SYS_IO_MODE_PWM", 1)
        self.refused(tree, "@default-mode")

    def test_an_unknown_symbol(self):
        tree = Tree(self)
        tree.edit(SERVO_H, "@enum-ref servo_type_e @default $SERVO_TYPE_ANGLE", "@enum-ref servo_type_e @default $SERVO_TYPE_NOPE")
        self.refused(tree, "SERVO_TYPE_NOPE")

    def test_an_array_of_pins_without_names(self):
        tree = Tree(self)
        tree.edit(DRV_H, "@aliases IN1|IN2|IN3|IN4|EN1|EN2|EN3|EN4|IPROPI1|IPROPI2|IPROPI3|IPROPI4|nSLEEP|nFAULT|VREF ", "")
        self.refused(tree, "@aliases")

    def test_a_wrong_number_of_array_names(self):
        tree = Tree(self)
        tree.edit(DRV_H, "@aliases IN1|", "@aliases ")
        self.refused(tree, "names")

    def test_an_own_op_without_its_annotation(self):
        tree = Tree(self)
        tree.edit(SERVO_C, "    //@op SERVO_OP_HOME @alias Home", "    //@opx SERVO_OP_HOME @alias Home")
        self.refused(tree, "SERVO_OP_HOME")

    def test_an_op_the_device_does_not_implement_in_a_param(self):
        tree = Tree(self)
        tree.edit(PCA_H, "//@param IO_OP_SET_PWM_FREQ.pin", "//@param IO_OP_SET_VOLTAGE.pin")
        self.refused(tree, "IO_OP_SET_VOLTAGE")

    def test_a_param_naming_no_field(self):
        tree = Tree(self)
        tree.edit(PCA_H, "//@param IO_OP_SET_PWM_FREQ.pin", "//@param IO_OP_SET_PWM_FREQ.nothing")
        self.refused(tree, "nothing")

    def test_an_event_of_a_contract_the_device_lacks(self):
        tree = Tree(self)
        tree.edit("components/devices/device_ads7128/device_ads7128.h", "//@event IO_EV_ADC_OUTSIDE IO_EV_ADC_INSIDE", "//@event VREG_EV_OVP")
        self.refused(tree, "VREG_EV_OVP")

    def test_a_value_condition_on_a_missing_field(self):
        tree = Tree(self)
        tree.edit(SERVO_H, "@when byte_mode=1", "@when no_such_field=1")
        self.refused(tree, "no_such_field")

    def test_a_missing_type_define(self):
        tree = Tree(self)
        tree.edit(PCA_C, ".type = SYS_DEVICE_TYPE_PCA9685", ".type = SYS_DEVICE_TYPE_NOT_A_TYPE")
        self.refused(tree, "NOT_A_TYPE")


if __name__ == "__main__":
    unittest.main()
