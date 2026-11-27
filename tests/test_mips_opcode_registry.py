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

    def test_inventory_disassembly_for_reserved_special(self):
        inventory = load_inventory_module()
        word = (0x08 << 21) | (0x09 << 16) | (0x0A << 11) | (5 << 6) | 0x05
        self.assertEqual(
            inventory.disassemble_mips(0x80010000, word),
            "reserved_special_05 rs=t0, rt=t1, rd=t2, sa=5",
        )

    def test_inventory_disassembly_for_branch_and_load_store(self):
        inventory = load_inventory_module()
        branch = (0x04 << 26) | (8 << 21) | (9 << 16) | 1
        load = (0x23 << 26) | (8 << 21) | (9 << 16) | 0xFFFC
        self.assertIn("beq t0, t1", inventory.disassemble_mips(0x80010000, branch))
        self.assertEqual(
            inventory.disassemble_mips(0x80010000, load),
            "lw t1, -4(t0)",
        )

    def test_inventory_disassembly_for_coprocessor_store(self):
        inventory = load_inventory_module()
        store = (0x3E << 26) | (8 << 21) | (9 << 16) | 0x20
        self.assertEqual(
            inventory.disassemble_mips(0x80010000, store),
            "sdc2 t1, 32(t0)",
        )

    def test_inventory_reports_unreachable_payload_words(self):
        inventory = load_inventory_module()
        fixture = ROOT / "tests" / "fixtures" / "json" / "mips" / "native-unsupported.json"
        import tempfile
        import importlib
        loader = ROOT / "tests" / "fixture_loader.py"
        spec = importlib.util.spec_from_file_location("fixture_loader", loader)
        fixture_loader = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(fixture_loader)
        description = __import__("json").loads(fixture.read_text(encoding="utf-8"))
        executable = fixture_loader.build_psx_exe(description)
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / "native-unsupported.ps-exe"
            path.write_bytes(executable)
            report = inventory.inventory(path)
        self.assertIn("payload_word_count", report)
        self.assertIn("counted_reachable_words", report)
        self.assertIn("excluded_unreachable_words", report)
        self.assertGreaterEqual(report["excluded_unreachable_words"], 0)

    def test_inventory_tracks_resolved_families(self):
        inventory = load_inventory_module()
        fixture = ROOT / "tests" / "fixtures" / "json" / "mips" / "native-unsupported.json"
        import tempfile
        import importlib
        import json
        loader = ROOT / "tests" / "fixture_loader.py"
        spec = importlib.util.spec_from_file_location("resolved_fixture_loader", loader)
        fixture_loader = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(fixture_loader)
        description = json.loads(fixture.read_text(encoding="utf-8"))
        executable = fixture_loader.build_psx_exe(description)
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / "native-unsupported.ps-exe"
            path.write_bytes(executable)
            previous = {
                "unsupported_families": {
                    "sdc2": 1,
                    "cop1": 1,
                }
            }
            report = inventory.inventory(path, previous)
        self.assertEqual(report["resolved_families"], ["cop1", "sdc2"])
        self.assertEqual(report["remaining_unsupported_families"], {})


if __name__ == "__main__":
    unittest.main()
