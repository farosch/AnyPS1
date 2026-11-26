import argparse
import struct
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Iterator


SECTOR_SIZE = 2352
PAYLOAD_OFFSET = 24
PAYLOAD_SIZE = 2048


@dataclass(frozen=True)
class IsoEntry:
    path: str
    lba: int
    size: int
    is_directory: bool


def read_sector(image: Path, lba: int) -> bytes:
    with image.open("rb") as stream:
        stream.seek(lba * SECTOR_SIZE + PAYLOAD_OFFSET)
        data = stream.read(PAYLOAD_SIZE)
    if len(data) != PAYLOAD_SIZE:
        raise ValueError(f"incomplete disc sector {lba}")
    return data


def read_both_endian32(data: bytes, offset: int) -> int:
    little = struct.unpack_from("<I", data, offset)[0]
    big = struct.unpack_from(">I", data, offset + 4)[0]
    if little != big:
        raise ValueError("invalid ISO 9660 directory record")
    return little


def read_directory_records(image: Path, lba: int, size: int) -> Iterator[tuple[str, IsoEntry | None]]:
    first = lba
    end = lba + ((size + PAYLOAD_SIZE - 1) // PAYLOAD_SIZE)
    records = bytearray()
    for sector_lba in range(first, end):
        records.extend(read_sector(image, sector_lba))

    offset = 0
    while offset < len(records):
        record_length = records[offset]
        if record_length == 0:
            offset = ((offset // PAYLOAD_SIZE) + 1) * PAYLOAD_SIZE
            continue
        if record_length < 33:
            raise ValueError("invalid ISO 9660 directory record")

        flags = records[offset + 25]
        name_length = records[offset + 32]
        name = bytes(records[offset + 33:offset + 33 + name_length])

        if name in (b"\x00", b"\x01"):
            yield ("", None)
        else:
            decoded = name.decode("ascii", errors="replace")
            if decoded.endswith(";1"):
                decoded = decoded[:-2]
            entry_lba = read_both_endian32(records, offset + 2)
            entry_size = read_both_endian32(records, offset + 10)
            yield (
                decoded,
                IsoEntry(
                    path=decoded,
                    lba=entry_lba,
                    size=entry_size,
                    is_directory=bool(flags & 2),
                ),
            )

        offset += record_length


def walk_directory(image: Path, lba: int, size: int, prefix: str = "") -> Iterator[IsoEntry]:
    for name, entry in read_directory_records(image, lba, size):
        if entry is None:
            continue
        path = f"{prefix}/{name}" if prefix else name
        yield IsoEntry(path, entry.lba, entry.size, entry.is_directory)
        if entry.is_directory:
            yield from walk_directory(image, entry.lba, entry.size, path)


def inspect_disc(image: Path) -> tuple[str, list[IsoEntry]]:
    primary = read_sector(image, 16)
    if primary[0] != 1 or primary[1:6] != b"CD001":
        raise ValueError("disc has no ISO 9660 primary volume descriptor")

    volume_id = primary[40:72].decode("ascii", errors="replace").rstrip()
    root = primary[156:190]
    root_lba = read_both_endian32(primary, 158)
    root_size = read_both_endian32(primary, 166)
    entries = list(walk_directory(image, root_lba, root_size))
    return volume_id, entries


def read_entry(image: Path, entry: IsoEntry) -> bytes:
    if entry.is_directory:
        raise ValueError(f"{entry.path} is a directory")
    output = bytearray()
    remaining = entry.size
    lba = entry.lba
    while remaining:
        sector = read_sector(image, lba)
        chunk = min(remaining, PAYLOAD_SIZE)
        output.extend(sector[:chunk])
        remaining -= chunk
        lba += 1
    return bytes(output)


def parse_psx_exe_header(data: bytes) -> dict[str, int | str]:
    if len(data) < 64 or data[:8] != b"PS-X EXE":
        raise ValueError("not a PS-X EXE")
    return {
        "initial_pc": struct.unpack_from("<I", data, 0x10)[0],
        "initial_gp": struct.unpack_from("<I", data, 0x14)[0],
        "load_address": struct.unpack_from("<I", data, 0x18)[0],
        "file_size": struct.unpack_from("<I", data, 0x1C)[0],
        "initial_sp_base": struct.unpack_from("<I", data, 0x30)[0],
        "initial_sp_offset": struct.unpack_from("<I", data, 0x34)[0],
    }


def find_boot_entry(image: Path) -> tuple[IsoEntry, bytes]:
    _, entries = inspect_disc(image)
    system_entry = next(
        (entry for entry in entries if entry.path.upper().endswith("SYSTEM.CNF")),
        None,
    )
    if system_entry is None:
        raise ValueError("SYSTEM.CNF not found")

    system_cnf = read_entry(image, system_entry).decode("ascii", errors="replace")
    for line in system_cnf.splitlines():
        if line.upper().strip().startswith("BOOT"):
            boot_value = line.split("=", 1)[1].strip()
            boot_name = boot_value.replace("\\", "/").split(":", 1)[-1].strip("/").upper()
            if boot_name.endswith(";1"):
                boot_name = boot_name[:-2]
            boot_entry = next(
                (entry for entry in entries if entry.path.upper() == boot_name),
                None,
            )
            if boot_entry is None:
                raise ValueError(f"boot executable not found: {boot_value}")
            return boot_entry, read_entry(image, boot_entry)

    raise ValueError("BOOT declaration not found")


def extract_psx_code(exe: bytes) -> tuple[dict[str, int | str], bytes]:
    header = parse_psx_exe_header(exe)
    file_size = int(header["file_size"])
    code_offset = 0x800
    code_end = code_offset + file_size
    if code_end > len(exe):
        raise ValueError(
            f"declared executable size 0x{file_size:X} exceeds file size 0x{len(exe):X}"
        )
    return header, exe[code_offset:code_end]


def main() -> int:
    parser = argparse.ArgumentParser(description="Inspect a PS1 MODE2/2352 disc image")
    parser.add_argument("image", type=Path)
    parser.add_argument("--boot", action="store_true", help="read SYSTEM.CNF and parse the boot executable")
    parser.add_argument("--extract", type=Path, help="write the boot PS-X executable code section")
    args = parser.parse_args()

    try:
        volume_id, entries = inspect_disc(args.image)
        print(f"Volume ID: {volume_id}")
        print(f"Entries: {len(entries)}")
        for entry in entries:
            if entry.path.upper() in ("SYSTEM.CNF",) or entry.path.upper().endswith(".EXE"):
                print(f"{entry.path}\t{entry.size}\tLBA {entry.lba}")

        if args.boot:
            boot_entry, exe = find_boot_entry(args.image)
            _, entries = inspect_disc(args.image)
            system_entry = next(
                entry for entry in entries if entry.path.upper().endswith("SYSTEM.CNF")
            )
            system_cnf = read_entry(args.image, system_entry).decode("ascii", errors="replace")
            header, code = extract_psx_code(exe)
            print("SYSTEM.CNF:")
            print(system_cnf.strip())
            print(f"Boot executable: {boot_entry.path}")
            print("PS-X EXE header:")
            for key, value in header.items():
                if isinstance(value, int):
                    print(f"  {key}: 0x{value:08X}")
                else:
                    print(f"  {key}: {value}")
            if args.extract:
                args.extract.write_bytes(code)
                print(f"Extracted code: {args.extract} ({len(code)} bytes)")
            return 0
    except (OSError, ValueError) as error:
        print(f"Error: {error}", file=sys.stderr)
        return 1

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
