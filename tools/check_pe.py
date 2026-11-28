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

    runtime_root = args.relinker.resolve().parent.parent
    with tempfile.TemporaryDirectory(prefix="anyps1-pe-", dir=runtime_root) as temp:
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
        coff_size = struct.unpack_from("<H", pe, pe_offset + 20)[0]
        optional_magic = struct.unpack_from("<H", pe, pe_offset + 24)[0]
        if machine != 0x8664:
            raise ValueError("PE machine is not AMD64")
        if section_count < 2 or section_count > 32:
            raise ValueError("PE section count is outside supported bounds")
        if coff_size != 240:
            raise ValueError("PE COFF optional header size is invalid")
        if optional_magic != 0x20B:
            raise ValueError("PE optional header is not PE32+")

        optional_offset = pe_offset + 24
        entry_rva = struct.unpack_from("<I", pe, optional_offset + 16)[0]
        base_code = struct.unpack_from("<I", pe, optional_offset + 20)[0]
        image_base = struct.unpack_from("<Q", pe, optional_offset + 24)[0]
        section_alignment = struct.unpack_from("<I", pe, optional_offset + 32)[0]
        file_alignment = struct.unpack_from("<I", pe, optional_offset + 36)[0]
        image_size = struct.unpack_from("<I", pe, optional_offset + 56)[0]
        headers_size = struct.unpack_from("<I", pe, optional_offset + 60)[0]
        subsystem = struct.unpack_from("<H", pe, optional_offset + 68)[0]
        directory_count = struct.unpack_from("<I", pe, optional_offset + 108)[0]
        import_rva = struct.unpack_from("<I", pe, optional_offset + 120)[0]
        import_size = struct.unpack_from("<I", pe, optional_offset + 124)[0]
        if entry_rva < 0x1000:
            raise ValueError("PE entry point or code base is invalid")
        if image_base != 0x140000000:
            raise ValueError("PE image base is invalid")
        if section_alignment != 0x1000 or file_alignment != 0x200:
            raise ValueError("PE alignment fields are invalid")
        if image_size < 0x1000 or headers_size == 0:
            raise ValueError("PE size fields are invalid")
        if subsystem != 3:
            raise ValueError("PE subsystem is not Windows CUI")
        if directory_count != 16:
            raise ValueError("PE data-directory count is invalid")

        section_offset = optional_offset + coff_size
        section_names = set()
        previous_rva = 0
        for index in range(section_count):
            offset = section_offset + index * 40
            name = pe[offset:offset + 8].rstrip(b"\0")
            if not name:
                raise ValueError("PE section has no name")
            section_names.add(name)
            virtual_size = struct.unpack_from("<I", pe, offset + 8)[0]
            raw_size = struct.unpack_from("<I", pe, offset + 16)[0]
            if virtual_size == 0 or raw_size == 0:
                raise ValueError("unexpected PE section size")
            actual_rva = struct.unpack_from("<I", pe, offset + 12)[0]
            if actual_rva <= previous_rva or actual_rva % section_alignment != 0:
                raise ValueError("PE section RVAs are not ordered and aligned")
            previous_rva = actual_rva
        if b".text" not in section_names:
            raise ValueError("PE executable lacks .text")

        def rva_to_offset(rva: int) -> int:
            for index in range(section_count):
                offset = section_offset + index * 40
                rva_start = struct.unpack_from("<I", pe, offset + 12)[0]
                virtual_size = struct.unpack_from("<I", pe, offset + 8)[0]
                raw_offset = struct.unpack_from("<I", pe, offset + 20)[0]
                if rva_start <= rva < rva_start + virtual_size:
                    return raw_offset + (rva - rva_start)
            raise ValueError(f"RVA is outside PE sections: 0x{rva:X}")

        if import_rva != 0:
            descriptor = rva_to_offset(import_rva)
            lookup_rva = struct.unpack_from("<I", pe, descriptor + 0)[0]
            name_rva = struct.unpack_from("<I", pe, descriptor + 12)[0]
            iat_rva = struct.unpack_from("<I", pe, descriptor + 16)[0]
            if lookup_rva == 0 or name_rva == 0 or iat_rva == 0:
                raise ValueError("KERNEL32 import descriptor is invalid")

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
        if hasattr(__import__("os"), "name") and __import__("os").name == "nt":
            try:
                completed = subprocess.run(
                    [str(pe_path)],
                    capture_output=True,
                    check=False,
                )
                if b"AnyPS1 native executable diagnostics" not in completed.stdout:
                    raise RuntimeError("PE executable did not emit AnyPS1 diagnostics")
                if b"target=windows" not in completed.stdout:
                    raise RuntimeError("PE executable did not report the Windows target")
                marker = b"unsupported_instructions="
                marker_offset = completed.stdout.find(marker)
                if marker_offset < 0:
                    raise RuntimeError(
                        "PE executable did not report unsupported-instruction status"
                    )
                count_text = completed.stdout[
                    marker_offset + len(marker):
                ].split(b"\n", 1)[0].strip(b"\0\r ")
                unsupported_count = int(count_text)
                expected_status = 16 if unsupported_count else 17
                if completed.returncode != expected_status:
                    raise RuntimeError(
                        f"PE executable returned unexpected status: "
                        f"{completed.returncode} (unsupported={unsupported_count}, "
                        f"stdout={completed.stdout!r})"
                    )
            except PermissionError:
                pass
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
