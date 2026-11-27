import importlib.util
import json
import sys
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
REGISTRY_PATH = ROOT / "tools" / "mips_opcode_registry.json"


def load_inventory_module():
    spec = importlib.util.spec_from_file_location(
        "inventory_unsupported_instructions",
        ROOT / "tools" / "inventory_unsupported_instructions.py",
    )
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module


class MipsOpcodeRegistryTest(unittest.TestCase):
    def test_registry_is_complete(self):
        registry = json.loads(REGISTRY_PATH.read_text(encoding="utf-8"))
        opcodes = [int(entry["value"]) for entry in registry["opcodes"]]
        special = [int(entry["value"]) for entry in registry["special"]]
        self.assertEqual(sorted(opcodes), list(range(64)))
        self.assertEqual(sorted(special), list(range(64)))

    def test_reserved_special_names_and_operands_are_canonical(self):
        registry = json.loads(REGISTRY_PATH.read_text(encoding="utf-8"))
        entries = {
            int(entry["value"]): entry
            for entry in registry["special"]
        }
        for function in range(64):
            entry = entries[function]
            self.assertTrue(entry["name"])
            self.assertTrue(entry["format"])
            self.assertEqual(len(entry["operands"]), len(entry["format"].split(",")))

        reserved = entries[0x05]
        self.assertEqual(reserved["name"], "reserved_special_05")
        self.assertEqual(reserved["kind"], "reserved")
        self.assertEqual(reserved["operands"], ["rs", "rt", "rd", "sa"])

    def test_inventory_uses_canonical_names(self):
        inventory = load_inventory_module()
        word = (0x08 << 21) | (0x09 << 16) | (0x0A << 11) | (5 << 6) | 0x05
        self.assertEqual(inventory.instruction_name(word), "reserved_special_05")


if __name__ == "__main__":
    unittest.main()
