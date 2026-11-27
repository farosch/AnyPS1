#!/usr/bin/env python3

import argparse
import json
import struct
import sys
from collections import Counter, defaultdict
from pathlib import Path


REGISTRY_PATH = Path(__file__).with_name("mips_opcode_registry.json")
REGISTRY = json.loads(REGISTRY_PATH.read_text(encoding="utf-8"))
OP_NAMES = {
    int(entry["value"]): entry["name"]
    for entry in REGISTRY["opcodes"]
}
SPECIAL_ENTRIES = {
    int(entry["value"]): entry
    for entry in REGISTRY["special"]
}
SPECIAL_NAMES = {
    value: entry["name"]
    for value, entry in SPECIAL_ENTRIES.items()
}
SUPPORTED_SPECIAL = {
    value
    for value, entry in SPECIAL_ENTRIES.items()
    if entry["kind"] == "known"
}
SUPPORTED_OPS = {
    0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
    0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F,
    0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26,
    0x28, 0x29, 0x2A, 0x2B, 0x2E,
}


def supported(word: int) -> bool:
    opcode = word >> 26
    special = word & 0x3F
    if word == 0:
        return True
    if opcode == 0:
        return special in SUPPORTED_SPECIAL
    if opcode == 0x10:
        rs = (word >> 21) & 0x1F
        return rs in (0x00, 0x02, 0x04, 0x06) or (
            rs == 0x10 and (word & 0x3F) == 0x10)
    return opcode in SUPPORTED_OPS


def classify_payload_region(word: int) -> str:
    if word == 0:
        return "instruction"
    printable = sum(
        1 for index in range(4)
        if 0x20 <= ((word >> (index * 8)) & 0xFF) <= 0x7E
    )
    bytes_ = [(word >> (index * 8)) & 0xFF for index in range(4)]
    if printable >= 3:
        return "embedded-data"
    if len(set(bytes_)) == 1 or word in (0x77777777, 0x77770700):
        return "embedded-data"
    opcode = word >> 26
    if opcode == 0 or 1 <= opcode <= 15 or 32 <= opcode <= 46:
        return "instruction"
    return "unknown"


def instruction_name(word: int) -> str:
    opcode = word >> 26
    if opcode == 0:
        return SPECIAL_NAMES[word & 0x3F]
    if opcode in OP_NAMES:
        return OP_NAMES[opcode]
    return f"unknown_opcode_{opcode:02x}"


def reachable_leaders(psx_data: bytes) -> tuple[int, int, set[int]]:
    if len(psx_data) < 2048:
        raise ValueError("PS-X EXE is shorter than its header")
    entry = struct.unpack_from("<I", psx_data, 0x10)[0]
    load = struct.unpack_from("<I", psx_data, 0x18)[0]
    file_size = struct.unpack_from("<I", psx_data, 0x1C)[0]
    if 2048 + file_size > len(psx_data):
        raise ValueError("PS-X EXE payload is truncated")

    leaders = {entry}
    processed = set()
    work = [entry]
    while work:
        address = work.pop()
        if address in processed:
            continue
        processed.add(address)
        if address < load or address >= load + file_size or address & 3:
            continue
        offset = address - load
        if offset + 4 > file_size:
            continue
        word = struct.unpack_from("<I", psx_data, 2048 + offset)[0]
        opcode = word >> 26
        rs = (word >> 21) & 0x1F
        rt = (word >> 16) & 0x1F
        immediate = word & 0xFFFF
        special = word & 0x3F
        signed_immediate = immediate - 0x10000 if immediate & 0x8000 else immediate

        if opcode in (0x01, 0x04, 0x05, 0x06, 0x07):
            target = (address + 4 + signed_immediate * 4) & 0xFFFFFFFF
            leaders.update((target, address + 8))
            work.extend((target, address + 8))
        elif opcode in (0x02, 0x03):
            target = ((address + 4) & 0xF0000000) | ((word & 0x03FFFFFF) << 2)
            leaders.add(target)
            work.append(target)
            leaders.add(address + 8)
            work.append(address + 8)
        elif not (opcode == 0 and special in (0x08, 0x09, 0x0C, 0x0D)):
            leaders.add(address + 4)
            work.append(address + 4)
        if opcode == 0 and special == 0x09:
            leaders.add(address + 8)
            work.append(address + 8)
        if opcode == 0 and special in (0x08,):
            continue
    return load, file_size, {address for address in leaders
                            if load <= address < load + file_size
                            and (address & 3) == 0}


def inventory(path: Path) -> dict:
    data = path.read_bytes()
    if len(data) < 8 or data[:8] != b"PS-X EXE":
        raise ValueError(f"{path}: not a PS-X EXE")
    load, file_size, leaders = reachable_leaders(data)
    unsupported = []
    family_counts = Counter()
    exact_counts = Counter()
    region_counts = Counter()
    for address in sorted(leaders):
        offset = address - load
        if offset + 4 > file_size:
            continue
        word = struct.unpack_from("<I", data, 2048 + offset)[0]
        region = classify_payload_region(word)
        region_counts[region] += 1
        if region == "embedded-data":
            continue
        if supported(word):
            continue
        name = instruction_name(word)
        family_counts[name] += 1
        exact_counts[f"{word:08X}"] += 1
        rs = (word >> 21) & 0x1F
        rt = (word >> 16) & 0x1F
        rd = (word >> 11) & 0x1F
        shamt = (word >> 6) & 0x1F
        immediate = word & 0xFFFF
        unsupported.append({
            "address": f"0x{address:08X}",
            "instruction": f"0x{word:08X}",
            "name": name,
            "operands": {
                "rs": rs,
                "rt": rt,
                "rd": rd,
                "shamt": shamt,
                "immediate": immediate,
                "index": word & 0x03FFFFFF,
                "code": (word >> 6) & 0xFFFFF,
            },
        })
    return {
        "executable": str(path),
        "load_address": f"0x{load:08X}",
        "reachable_leaders": len(leaders),
        "payload_regions": dict(sorted(region_counts.items())),
        "unsupported_count": len(unsupported),
        "unsupported_families": dict(sorted(family_counts.items())),
        "unsupported_exact_words": dict(sorted(exact_counts.items())),
        "unsupported_instructions": unsupported,
    }


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Inventory unsupported MIPS lowering instructions")
    parser.add_argument("paths", nargs="+", type=Path)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()

    reports = []
    for path in args.paths:
        try:
            reports.append(inventory(path))
        except Exception as error:
            reports.append({
                "executable": str(path),
                "error": str(error),
            })

    text = json.dumps(reports, indent=2)
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(text + "\n", encoding="utf-8")
    sys.stdout.write(text + "\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
