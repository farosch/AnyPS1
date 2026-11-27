#!/usr/bin/env python3

import argparse
import json
import struct
import sys
from collections import Counter, defaultdict
from pathlib import Path


SPECIAL_NAMES = {
    0x00: "sll", 0x02: "srl", 0x03: "sra", 0x04: "sllv",
    0x06: "srlv", 0x07: "srav", 0x08: "jr", 0x09: "jalr",
    0x0C: "syscall", 0x0D: "break", 0x10: "mfhi", 0x11: "mthi",
    0x12: "mflo", 0x13: "mtlo", 0x18: "mult", 0x19: "multu",
    0x1A: "div", 0x1B: "divu", 0x20: "add", 0x21: "addu",
    0x22: "sub", 0x23: "subu", 0x24: "and", 0x25: "or",
    0x26: "xor", 0x27: "nor", 0x2A: "slt", 0x2B: "sltu",
}

OP_NAMES = {
    0x01: "regimm", 0x02: "j", 0x03: "jal",
    0x04: "beq", 0x05: "bne", 0x06: "blez", 0x07: "bgtz",
    0x08: "addi", 0x09: "addiu", 0x0A: "slti", 0x0B: "sltiu",
    0x0C: "andi", 0x0D: "ori", 0x0E: "xori", 0x0F: "lui",
    0x10: "cop0", 0x11: "cop1", 0x12: "cop2", 0x13: "cop3",
    0x20: "lb", 0x21: "lh", 0x22: "lwl", 0x23: "lw",
    0x24: "lbu", 0x25: "lhu", 0x26: "lwr", 0x28: "sb",
    0x29: "sh", 0x2A: "swl", 0x2B: "sw", 0x2E: "swr",
    0x30: "lwc0", 0x31: "lwc1", 0x32: "lwc2", 0x33: "lwc3",
    0x38: "swc0", 0x39: "swc1", 0x3A: "swc2", 0x3B: "swc3",
}

SUPPORTED_SPECIAL = set(SPECIAL_NAMES)
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
        return SPECIAL_NAMES.get(word & 0x3F, f"unknown_special_{word & 0x3f:02x}")
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
        unsupported.append({
            "address": f"0x{address:08X}",
            "instruction": f"0x{word:08X}",
            "name": name,
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
