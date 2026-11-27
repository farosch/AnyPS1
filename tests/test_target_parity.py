import json
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


class TargetParityTest(unittest.TestCase):
    def test_linux_windows_mips_and_gpu_support(self):
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
                pattern[1],
                windows,
                f"Windows MIPS lowerer does not contain {name}",
            )

        for pattern in (
            "resolve_static_indirect_target",
            "call_edges=",
            "unresolved_indirect_transfers=",
            "sdl3_library",
            "SDL_Vulkan_LoadLibrary",
            "LoadLibraryA",
        ):
            self.assertIn(
                pattern,
                windows,
                f"Windows lowerer is missing host/CFG parity feature {pattern}",
            )

        for pattern in (
            "anyps1_host_loop_controller_state",
            "anyps1_host_loop_spu_read",
            "anyps1_host_loop_gpu_write",
            "anyps1_host_loop_vulkan_present",
        ):
            self.assertIn(
                pattern,
                linux,
                f"Linux lowerer is missing host-loop integration {pattern}",
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
