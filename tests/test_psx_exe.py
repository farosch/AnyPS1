import struct
import sys
import unittest
from pathlib import Path


sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))

from inspect_psx_disc import parse_psx_exe_header


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


if __name__ == "__main__":
    unittest.main()
