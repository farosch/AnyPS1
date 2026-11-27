#!/usr/bin/env python3

import argparse
import shutil
import struct
import subprocess
import tempfile
from pathlib import Path


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Validate a generated PE32+ artifact and optionally inspect it")
    parser.add_argument("--relinker", type=Path, required=True)
    parser.add_argument("--fixture", type=Path, required=True)
    args = parser.parse_args()

    with tempfile.TemporaryDirectory(prefix="anyps1-pe-") as temp:
        root = Path(temp)
        executable = root / "SCUS_941.63"
        executable.write_bytes(args.fixture.read_bytes())
        (root / "SYSTEM.CNF").write_text(
            "BOOT=cdrom:\\SCUS_941.63;1\n",
            encoding="utf-8",
        )
        completed = subprocess.run(
            [str(args.relinker), "--target", "windows", str(root)],
            capture_output=True,
            check=False,
        )
        if completed.returncode != 0:
            return completed.returncode

        pe_path = root / "game.exe"
        pe = pe_path.read_bytes()
        if pe[:2] != b"MZ":
            raise ValueError("missing DOS signature")
        pe_offset = struct.unpack_from("<I", pe, 0x3C)[0]
        if pe[pe_offset:pe_offset + 4] != b"PE\0\0":
            raise ValueError("missing PE signature")
        machine = struct.unpack_from("<H", pe, pe_offset + 4)[0]
        section_count = struct.unpack_from("<H", pe, pe_offset + 6)[0]
        optional_magic = struct.unpack_from("<H", pe, pe_offset + 24)[0]
        if machine != 0x8664:
            raise ValueError("PE machine is not AMD64")
        if section_count != 4:
            raise ValueError("PE section count is not 4")
        if optional_magic != 0x20B:
            raise ValueError("PE optional header is not PE32+")

        inspector = shutil.which("llvm-readobj") or shutil.which("objdump")
        if inspector:
            if Path(inspector).name.startswith("llvm"):
                result = subprocess.run(
                    [inspector, "-h", str(pe_path)],
                    capture_output=True,
                    check=False,
                )
            else:
                result = subprocess.run(
                    [inspector, "-f", str(pe_path)],
                    capture_output=True,
                    check=False,
                )
            if result.returncode != 0:
                raise RuntimeError("PE format inspector rejected generated output")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
