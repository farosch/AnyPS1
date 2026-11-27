import json
import struct
import sys
import unittest
from pathlib import Path


sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
sys.path.insert(0, str(Path(__file__).resolve().parent))

from inspect_psx_disc import extract_psx_code, parse_psx_exe_header
from analyze_psx_exe import (
    classify_bios_calls,
    classify_hardware_references,
    control_flow_map,
    disassemble_mips,
    disassemble_range,
    identify_calls,
)
from extract_psx_game import parse_cue, prepare_extraction_output, safe_output_path


FIXTURES = Path(__file__).resolve().parent / "fixtures"
JSON_FIXTURES = FIXTURES / "json"


def load_json_fixture(relative_path):
    description = json.loads((JSON_FIXTURES / relative_path).read_text(encoding="utf-8"))
    from fixture_loader import build_psx_exe
    return build_psx_exe(description)


class PsxExeHeaderTest(unittest.TestCase):
    def test_parse_valid_header(self):
        data = load_json_fixture("valid.json")

        header = parse_psx_exe_header(data)

        self.assertEqual(header["initial_pc"], 0x80010008)
        self.assertEqual(header["load_address"], 0x80010000)
        self.assertEqual(header["file_size"], 0x20)

    def test_reject_invalid_header(self):
        with self.assertRaises(ValueError):
            parse_psx_exe_header(load_json_fixture("invalid.json"))

    def test_extract_declared_code_section(self):
        header, code = extract_psx_code(load_json_fixture("declared-code.json"))

        self.assertEqual(header["load_address"], 0x80010000)
        self.assertEqual(code, b"\x00\x00\x00\x24")

    def test_extract_rejects_truncated_code(self):
        with self.assertRaises(ValueError):
            extract_psx_code(load_json_fixture("truncated.json"))


class MipsDisassemblerTest(unittest.TestCase):
    def test_nop(self):
        self.assertEqual(disassemble_mips(0x80010000, 0), "nop")

    def test_addiu(self):
        word = (0x09 << 26) | (8 << 21) | (9 << 16) | 0xFFFC
        self.assertEqual(
            disassemble_mips(0x80010000, word),
            "addiu t1, t0, -4",
        )

    def test_branch_target(self):
        word = (0x04 << 26) | (8 << 21) | (9 << 16) | 2
        self.assertEqual(
            disassemble_mips(0x80010000, word),
            "beq t0, t1, 0x8001000C",
        )

    def test_range_preserves_address(self):
        description = json.loads((JSON_FIXTURES / "mips" / "instructions.json").read_text())
        code = b"".join(struct.pack("<I", word) for word in description["words"])[:8]
        instructions = list(disassemble_range(code, 0x80010000, count=2))
        self.assertEqual(instructions[0][0], 0x80010000)
        self.assertEqual(instructions[1][0], 0x80010004)

    def test_control_flow_adds_branch_edges(self):
        description = json.loads((JSON_FIXTURES / "mips" / "control-flow.json").read_text())
        code = b"".join(struct.pack("<I", word) for word in description["words"])
        cfg = control_flow_map(code, 0x80010000)
        self.assertGreaterEqual(len(cfg["blocks"]), 2)

    def test_mips_i_coverage_fixture_contains_instruction_families(self):
        description = json.loads(
            (JSON_FIXTURES / "mips" / "mips-i-coverage.json").read_text())
        words = description["instructions"]
        opcodes = {(word >> 26) & 0x3F for word in words}
        specials = {word & 0x3F for word in words if (word >> 26) == 0}
        expected_opcodes = {
            0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
            0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F,
            0x20, 0x21, 0x23, 0x24, 0x25, 0x28, 0x29, 0x2B,
        }
        self.assertTrue(expected_opcodes.issubset(opcodes))
        self.assertTrue({0x00, 0x02, 0x21, 0x23}.issubset(specials))

    def test_coprocessor_coverage_fixture_contains_family_stubs(self):
        description = json.loads(
            (JSON_FIXTURES / "mips" / "coprocessor-coverage.json").read_text())
        words = description["instructions"]
        opcodes = {(word >> 26) & 0x3F for word in words}
        self.assertTrue({0x10, 0x11, 0x12, 0x13, 0x30, 0x31, 0x32,
                         0x33, 0x38, 0x39, 0x3A, 0x3B}.issubset(opcodes))

    def test_identifies_a0_bios_vector_and_gpu_reference(self):
        lui_t1 = (0x0F << 26) | (9 << 16) | 0xA000
        addiu_t1 = (0x09 << 26) | (9 << 21) | (9 << 16) | 0x40
        jalr_t1 = (9 << 21) | (31 << 11) | 0x09
        lui_t0 = (0x0F << 26) | (8 << 16) | 0x1F80
        lw_gpu = (0x23 << 26) | (8 << 21) | (2 << 16) | 0x1810
        code = struct.pack("<IIIII", lui_t1, addiu_t1, jalr_t1, lui_t0, lw_gpu)

        calls, refs = identify_calls(code, 0x80010000)
        bios = classify_bios_calls(calls)
        hardware = classify_hardware_references(refs)

        self.assertEqual(len(bios["A0"]), 1)
        self.assertEqual(bios["A0"][0].target, 0xA0000040)
        self.assertEqual(len(hardware["GPU"]), 1)
        self.assertEqual(hardware["GPU"][0].resolved_address, 0x1F801810)


class GameExtractionTest(unittest.TestCase):
    def test_prepare_output_removes_legacy_extraction_wrappers(self):
        import tempfile
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "filesystem").mkdir()
            (root / "filesystem" / "SYSTEM.CNF").write_text("old")
            (root / "boot").mkdir()
            (root / "boot" / "SCUS_000.00").write_bytes(b"old")
            (root / "SYSTEM.CNF").write_text("old")
            (root / "game.json").write_text("{}")

            prepare_extraction_output(root)

            self.assertTrue(root.is_dir())
            self.assertFalse((root / "filesystem").exists())
            self.assertFalse((root / "boot").exists())
            self.assertFalse((root / "SYSTEM.CNF").exists())
            self.assertFalse((root / "game.json").exists())

    def test_parse_multitrack_cue(self):
        root = FIXTURES / "cue" / "multitrack"
        tracks = parse_cue(root / "Game.cue")

        self.assertEqual(len(tracks), 2)
        self.assertEqual(tracks[0].number, 1)
        self.assertEqual(tracks[0].mode, "MODE2/2352")
        self.assertEqual(tracks[1].number, 2)
        self.assertEqual(tracks[1].mode, "AUDIO")

    def test_parse_separate_data_and_audio_bins(self):
        root = FIXTURES / "cue" / "separate"
        tracks = parse_cue(root / "Game.cue")

        self.assertEqual([track.number for track in tracks], [1, 2, 3])
        self.assertEqual(tracks[1].file_start_lba, 10)
        self.assertEqual(tracks[2].file_start_lba, 15)

    def test_rejects_parent_traversal_path(self):
        root = FIXTURES / "cue" / "separate"
        with self.assertRaises(ValueError):
            safe_output_path(root, "../escape.bin")

    def test_extracts_two_cues_as_separate_discs(self):
        root = FIXTURES / "cue" / "discs"
        tracks_1 = parse_cue(root / "Disc1.cue")
        tracks_2 = parse_cue(root / "Disc2.cue")

        self.assertEqual(tracks_1[0].bin_file, "Disc1.bin")
        self.assertEqual(tracks_2[0].bin_file, "Disc2.bin")


if __name__ == "__main__":
    unittest.main()
