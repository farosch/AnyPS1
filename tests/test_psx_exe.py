import struct
import sys
import unittest
from pathlib import Path


sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))

from inspect_psx_disc import extract_psx_code, parse_psx_exe_header
from analyze_psx_exe import (
    classify_bios_calls,
    classify_hardware_references,
    control_flow_map,
    disassemble_mips,
    disassemble_range,
    identify_calls,
)
from extract_psx_game import parse_cue, safe_output_path


class PsxExeHeaderTest(unittest.TestCase):
    def test_parse_valid_header(self):
        data = bytearray(64)
        data[:8] = b"PS-X EXE"
        struct.pack_into("<I", data, 0x10, 0x80010008)
        struct.pack_into("<I", data, 0x14, 0x80020000)
        struct.pack_into("<I", data, 0x18, 0x80010000)
        struct.pack_into("<I", data, 0x1C, 0x1000)
        struct.pack_into("<I", data, 0x30, 0x801FFFF0)
        struct.pack_into("<I", data, 0x34, 0xFFFFFF00)

        header = parse_psx_exe_header(bytes(data))

        self.assertEqual(header["initial_pc"], 0x80010008)
        self.assertEqual(header["load_address"], 0x80010000)
        self.assertEqual(header["file_size"], 0x1000)
        self.assertEqual(header["initial_sp_base"], 0x801FFFF0)
        self.assertEqual(header["initial_sp_offset"], 0xFFFFFF00)

    def test_reject_invalid_header(self):
        with self.assertRaises(ValueError):
            parse_psx_exe_header(b"NOT A PSX EXECUTABLE")

    def test_extract_declared_code_section(self):
        data = bytearray(0x804)
        data[:8] = b"PS-X EXE"
        struct.pack_into("<I", data, 0x18, 0x80010000)
        struct.pack_into("<I", data, 0x1C, 4)
        data[0x800:0x804] = b"\x00\x00\x00\x24"

        header, code = extract_psx_code(bytes(data))

        self.assertEqual(header["load_address"], 0x80010000)
        self.assertEqual(code, b"\x00\x00\x00\x24")

    def test_extract_rejects_truncated_code(self):
        data = bytearray(0x800)
        data[:8] = b"PS-X EXE"
        struct.pack_into("<I", data, 0x1C, 4)

        with self.assertRaises(ValueError):
            extract_psx_code(bytes(data))


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
        code = struct.pack("<II", 0, (0x09 << 26) | (9 << 16) | 1)
        instructions = list(disassemble_range(code, 0x80010000, count=2))
        self.assertEqual(instructions[0][0], 0x80010000)
        self.assertEqual(instructions[1][0], 0x80010004)

    def test_control_flow_adds_branch_edges(self):
        code = struct.pack("<IIII", 0, (0x04 << 26) | 2, 0, 0)
        cfg = control_flow_map(code, 0x80010000)
        self.assertGreaterEqual(len(cfg["blocks"]), 2)

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
    def test_parse_multitrack_cue(self):
        cue = b'FILE "Game.bin" BINARY\r\n  TRACK 01 MODE2/2352\r\nFILE "Audio.bin" BINARY\r\n  TRACK 02 AUDIO\r\n'
        with __import__("tempfile").TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "Game.bin").write_bytes(b"")
            (root / "Audio.bin").write_bytes(b"")
            (root / "Game.cue").write_bytes(cue)

            tracks = parse_cue(root / "Game.cue")

            self.assertEqual(len(tracks), 2)
            self.assertEqual(tracks[0].number, 1)
            self.assertEqual(tracks[0].mode, "MODE2/2352")
            self.assertEqual(tracks[1].number, 2)
            self.assertEqual(tracks[1].mode, "AUDIO")

    def test_rejects_parent_traversal_path(self):
        with __import__("tempfile").TemporaryDirectory() as directory:
            root = Path(directory)
            with self.assertRaises(ValueError):
                safe_output_path(root, "../escape.bin")


if __name__ == "__main__":
    unittest.main()
