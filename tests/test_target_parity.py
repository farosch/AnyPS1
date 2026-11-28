import json
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


class LoweringInventoryTest(unittest.TestCase):
    def test_linux_windows_lowering_inventory(self):
        fixture_path = (
            ROOT / "tests" / "fixtures" / "json" / "target-parity.json"
        )
        fixture = json.loads(fixture_path.read_text(encoding="utf-8"))

        linux = (
            ROOT / "src" / "lowering" / "MipsLinuxLowerer.cpp"
        ).read_text(encoding="utf-8")
        windows = (
            ROOT / "src" / "lowering" / "MipsWindowsLowerer.cpp"
        ).read_text(encoding="utf-8")

        mips_patterns = {
            "j": ("0x02", "0x02"),
            "jal": ("0x03", "0x03"),
            "beq": ("0x04", "0x04"),
            "bne": ("0x05", "0x05"),
            "blez": ("0x06", "0x06"),
            "bgtz": ("0x07", "0x07"),
            "addiu": ("0x09", "0x09"),
            "slti": ("0x0A", "0x0A"),
            "sltiu": ("0x0B", "0x0B"),
            "andi": ("0x0C", "0x0C"),
            "ori": ("0x0D", "0x0D"),
            "xori": ("0x0E", "0x0E"),
            "lui": ("0x0F", "0x0F"),
            "lb": ("0x20", "0x20"),
            "lh": ("0x21", "0x21"),
            "lw": ("0x23", "0x23"),
            "lbu": ("0x24", "0x24"),
            "lhu": ("0x25", "0x25"),
            "sb": ("0x28", "0x28"),
            "sh": ("0x29", "0x29"),
            "sw": ("0x2B", "0x2B"),
            "addu": ("0x21", "0x21"),
            "subu": ("0x23", "0x23"),
            "and": ("0x24", "0x24"),
            "or": ("0x25", "0x25"),
            "xor": ("0x26", "0x26"),
            "nor": ("0x27", "0x27"),
            "slt": ("0x2A", "0x2A"),
            "sltu": ("0x2B", "0x2B"),
            "sll": ("0x00", "0x00"),
            "srl": ("0x02", "0x02"),
            "sra": ("0x03", "0x03"),
            "jr": ("0x08", "0x08"),
            "jalr": ("0x09", "0x09"),
            "syscall": ("0x0C", "0x0C"),
            "break": ("0x0D", "0x0D"),
        }

        for name in (
            fixture["mips"]["known_special"]
            + fixture["mips"]["known_opcodes"]
        ):
            pattern = mips_patterns[name]
            self.assertIn(
                pattern[0],
                linux,
                f"Linux MIPS lowerer does not contain {name}",
            )
        self.assertIn(
            "NativeLoweringIr",
            windows,
            "Windows MIPS lowering does not use the shared native lowering IR",
        )

        self.assertIn(
            "write_pe_ir",
            windows,
            "Windows lowering must emit through the shared LLD PE backend",
        )
        self.assertIn(
            "build_mips_executable_plan",
            linux,
            "Linux lowering must consume the shared executable plan",
        )
        self.assertIn(
            "build_mips_executable_plan",
            windows,
            "Windows lowering must consume the shared executable plan",
        )
        self.assertNotIn(
            "discover_payload_pointer_targets",
            linux,
            "Linux lowering must not rebuild shared CFG pointer discovery",
        )
        self.assertNotIn(
            "discover_indirect_table_targets",
            linux,
            "Linux lowering must not rebuild shared indirect-table discovery",
        )
        self.assertIn(
            "build_mips_instruction_ir",
            linux,
            "Linux lowering must consume the shared instruction IR",
        )
        shared = (
            ROOT / "src" / "lowering" / "MipsLowerer.cpp"
        ).read_text(encoding="utf-8")
        self.assertIn(
            "leader_dispatch",
            shared,
            "Shared lowering must provide sparse leader dispatch buckets",
        )
        self.assertIn(
            "leader_chunks",
            shared,
            "Shared lowering must provide bounded native emission chunks",
        )
        linux_text = (
            ROOT / "src" / "lowering" / "MipsLinuxLowerer.cpp"
        ).read_text(encoding="utf-8")
        windows_text = (
            ROOT / "src" / "lowering" / "MipsWindowsLowerer.cpp"
        ).read_text(encoding="utf-8")
        self.assertIn(
            ".supported",
            windows_text,
            "Windows lowering must consume canonical IR support state",
        )
        self.assertNotIn(
            "is_supported_mips_instruction",
            windows_text,
            "Windows lowering must not maintain a separate support classifier",
        )
        self.assertNotIn(
            "is_supported_mips_instruction",
            linux_text,
            "Linux lowering must not maintain a separate support classifier",
        )
        self.assertIn(
            "leader_chunks",
            windows_text,
            "Windows lowering must consume bounded leader chunks",
        )
        self.assertIn(
            "indirect_leader_dispatch",
            linux_text,
            "Linux lowering must retain sparse indirect dispatch",
        )
        self.assertNotIn(
            "for (std::uint32_t target : leaders)",
            windows,
            "Windows lowering must not duplicate leader scans per instruction",
        )
        self.assertEqual(
            linux.count('source << "indirect_jump:\\n";'),
            1,
            "Linux indirect dispatch must use one shared leader table",
        )
        self.assertNotIn(
            "for (std::uint32_t leader_target : leaders)",
            linux,
            "Linux lowering must not duplicate leader scans per JR",
        )

        graphics = ROOT / "src" / "gpu" / "Graphics.cpp"
        graphics_text = graphics.read_text(encoding="utf-8")
        for primitive in fixture["gpu"]["primitives"]:
            enum_names = {
                "polygon": "Polygon",
                "textured-polygon": "TexturedPolygon",
                "sprite": "Sprite",
                "rectangle": "Rectangle",
                "line": "Line",
                "framebuffer": "Framebuffer",
            }
            enum_name = enum_names[primitive]
            self.assertIn(
                enum_name,
                graphics_text,
                f"GPU primitive {primitive} is missing from shared graphics lowering",
            )

        for gp0 in fixture["gpu"]["gp0"]:
            self.assertIn(
                gp0,
                graphics_text,
                f"GP0 command {gp0} is missing from shared GPU lowering",
            )
        for gp1 in fixture["gpu"]["gp1"]:
            self.assertIn(
                gp1,
                graphics_text,
                f"GP1 command {gp1} is missing from shared GPU lowering",
            )


if __name__ == "__main__":
    unittest.main()
