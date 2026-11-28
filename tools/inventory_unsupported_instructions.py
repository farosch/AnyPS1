#!/usr/bin/env python3

import argparse
import json
import struct
import sys
from collections import Counter
from pathlib import Path


REG_NAMES = (
    "zero", "at", "v0", "v1", "a0", "a1", "a2", "a3",
    "t0", "t1", "t2", "t3", "t4", "t5", "t6", "t7",
    "s0", "s1", "s2", "s3", "s4", "s5", "s6", "s7",
    "t8", "t9", "k0", "k1", "gp", "sp", "fp", "ra",
)


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
    0x14, 0x15, 0x16, 0x17, 0x2F,
    0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26,
    0x28, 0x29, 0x2A, 0x2B, 0x2E,
    0x31, 0x32, 0x39, 0x3A,
    0x30, 0x33, 0x3B, 0x3E,
    0x13, 0x35, 0x36, 0x37, 0x3D, 0x3E, 0x3F,
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
    if opcode == 0x11:
        return True
    if opcode == 0x12:
        rs = (word >> 21) & 0x1F
        if rs in (0x00, 0x02, 0x04, 0x06):
            return True
        function = word & 0x3F
        return rs >= 16 and function in {
            0x01, 0x06, 0x0C, 0x10, 0x11, 0x12,
            0x13, 0x14, 0x16, 0x1B, 0x1C, 0x1E,
            0x20, 0x28, 0x29, 0x2A, 0x2D, 0x2E,
            0x30, 0x3D, 0x3E, 0x3F,
        }
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
    if opcode in (0x3C, 0x3D, 0x3F):
        return "unknown"
    if opcode == 0 or 1 <= opcode <= 15 or 32 <= opcode <= 59:
        return "instruction"
    return "unknown"


def instruction_name(word: int) -> str:
    opcode = word >> 26
    if opcode == 0:
        return SPECIAL_NAMES[word & 0x3F]
    if opcode in OP_NAMES:
        return OP_NAMES[opcode]
    return f"unknown_opcode_{opcode:02x}"


def sign_extend16(value: int) -> int:
    return value - 0x10000 if value & 0x8000 else value


def disassemble_coprocessor(name: str, rs: int, rt: int, rd: int, word: int) -> str:
    if rs == 0:
        return f"mfc{name[-1]} {REG_NAMES[rt]}, {rd}"
    if rs == 4:
        return f"mtc{name[-1]} {REG_NAMES[rt]}, {rd}"
    if rs == 16:
        return f"{name} 0x{word & 0x00FFFFFF:06X}"
    return f"{name} 0x{word & 0x03FFFFFF:X}"


def disassemble_mips(address: int, word: int) -> str:
    opcode = (word >> 26) & 0x3F
    rs = (word >> 21) & 0x1F
    rt = (word >> 16) & 0x1F
    rd = (word >> 11) & 0x1F
    shamt = (word >> 6) & 0x1F
    immediate = word & 0xFFFF
    index = word & 0x03FFFFFF

    if opcode == 0:
        special = word & 0x3F
        entry = SPECIAL_ENTRIES[special]
        name = entry["name"]
        if entry["kind"] == "reserved":
            return (
                f"{name} rs={REG_NAMES[rs]}, rt={REG_NAMES[rt]}, "
                f"rd={REG_NAMES[rd]}, sa={shamt}"
            )
        if name in ("sll", "srl", "sra"):
            return "nop" if word == 0 else f"{name} {REG_NAMES[rd]}, {REG_NAMES[rt]}, {shamt}"
        if name in ("sllv", "srlv", "srav"):
            return f"{name} {REG_NAMES[rd]}, {REG_NAMES[rt]}, {REG_NAMES[rs]}"
        if name in ("jr", "jalr"):
            return f"{name} {REG_NAMES[rs]}" if name == "jr" else f"{name} {REG_NAMES[rd]}, {REG_NAMES[rs]}"
        if name in ("mfhi", "mflo"):
            return f"{name} {REG_NAMES[rd]}"
        if name in ("mthi", "mtlo"):
            return f"{name} {REG_NAMES[rs]}"
        if name in ("mult", "multu", "div", "divu"):
            return f"{name} {REG_NAMES[rs]}, {REG_NAMES[rt]}"
        if name in ("syscall", "break"):
            code = (word >> 6) & 0xFFFFF
            return f"{name} 0x{code:05X}" if code else name
        return f"{name} {REG_NAMES[rd]}, {REG_NAMES[rs]}, {REG_NAMES[rt]}"

    if opcode == 0x01:
        branch_names = {0x00: "bltz", 0x01: "bgez", 0x10: "bltzal", 0x11: "bgezal"}
        name = branch_names.get(rt)
        if name is None:
            return f"{OP_NAMES[opcode]} rs={REG_NAMES[rs]}, rt={REG_NAMES[rt]}, imm=0x{immediate:04X}"
        target = (address + 4 + sign_extend16(immediate) * 4) & 0xFFFFFFFF
        return f"{name} {REG_NAMES[rs]}, 0x{target:08X}"

    if opcode in (0x02, 0x03):
        target = ((address + 4) & 0xF0000000) | (index << 2)
        return f"{OP_NAMES[opcode]} 0x{target & 0xFFFFFFFF:08X}"

    if opcode in (0x04, 0x05):
        target = (address + 4 + sign_extend16(immediate) * 4) & 0xFFFFFFFF
        return f"{OP_NAMES[opcode]} {REG_NAMES[rs]}, {REG_NAMES[rt]}, 0x{target:08X}"

    if opcode in (0x06, 0x07):
        target = (address + 4 + sign_extend16(immediate) * 4) & 0xFFFFFFFF
        return f"{OP_NAMES[opcode]} {REG_NAMES[rs]}, 0x{target:08X}"

    if opcode == 0x10:
        return disassemble_coprocessor("cop0", rs, rt, rd, word)
    if opcode == 0x11:
        return disassemble_coprocessor("cop1", rs, rt, rd, word)
    if opcode == 0x12:
        return disassemble_coprocessor("cop2", rs, rt, rd, word)
    if opcode == 0x13:
        return disassemble_coprocessor("cop3", rs, rt, rd, word)

    name = OP_NAMES.get(opcode)
    if name is None:
        return f"{instruction_name(word)} rs={REG_NAMES[rs]}, rt={REG_NAMES[rt]}, imm=0x{immediate:04X}"
    if opcode == 0x0F:
        return f"{name} {REG_NAMES[rt]}, 0x{immediate:04X}"
    if opcode in range(0x20, 0x2F):
        return f"{name} {REG_NAMES[rt]}, {sign_extend16(immediate)}({REG_NAMES[rs]})"
    if opcode in (0x08, 0x09, 0x0A, 0x0B):
        return f"{name} {REG_NAMES[rt]}, {REG_NAMES[rs]}, {sign_extend16(immediate)}"
    if opcode in (0x0C, 0x0D, 0x0E):
        return f"{name} {REG_NAMES[rt]}, {REG_NAMES[rs]}, 0x{immediate:04X}"
    if opcode in (0x30, 0x31, 0x32, 0x33, 0x38, 0x39, 0x3A, 0x3B):
        return f"{name} {REG_NAMES[rt]}, {sign_extend16(immediate)}({REG_NAMES[rs]})"
    if opcode in (0x34, 0x35, 0x36, 0x37, 0x3C, 0x3D, 0x3E, 0x3F):
        if name.startswith("reserved_opcode_"):
            return (
                f"{name} rs={REG_NAMES[rs]}, rt={REG_NAMES[rt]}, "
                f"imm=0x{immediate:04X}"
            )
        return f"{name} {REG_NAMES[rt]}, {sign_extend16(immediate)}({REG_NAMES[rs]})"
    return f"{name} rs={REG_NAMES[rs]}, rt={REG_NAMES[rt]}, imm=0x{immediate:04X}"


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


def inventory(path: Path, previous_report: dict | None = None) -> dict:
    data = path.read_bytes()
    if len(data) < 8 or data[:8] != b"PS-X EXE":
        raise ValueError(f"{path}: not a PS-X EXE")
    load, file_size, leaders = reachable_leaders(data)
    unsupported = []
    family_counts = Counter()
    exact_counts = Counter()
    region_counts = Counter()
    reachable_payload_addresses = set()
    for address in sorted(leaders):
        offset = address - load
        if offset + 4 > file_size:
            continue
        reachable_payload_addresses.add(address)
        word = struct.unpack_from("<I", data, 2048 + offset)[0]
        region = classify_payload_region(word)
        region_counts[region] += 1
        if region == "embedded-data":
            continue
        if region == "unknown":
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
            "disassembly": disassemble_mips(address, word),
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
    previous_families = set()
    if previous_report:
        previous = previous_report
        if isinstance(previous, list):
            previous = next(
                (item for item in previous if item.get("executable") == str(path)),
                {},
            )
        previous_families = set(
            previous.get("unsupported_families", {}).keys()
        )
    resolved_families = sorted(previous_families - set(family_counts.keys()))

    return {
        "executable": str(path),
        "load_address": f"0x{load:08X}",
        "payload_word_count": file_size // 4,
        "reachable_leaders": len(leaders),
        "counted_reachable_words": len(reachable_payload_addresses),
        "excluded_unreachable_words":
            max(0, (file_size // 4) - len(reachable_payload_addresses)),
        "payload_regions": dict(sorted(region_counts.items())),
        "unsupported_count": len(unsupported),
        "unsupported_families": dict(sorted(family_counts.items())),
        "unsupported_exact_words": dict(sorted(exact_counts.items())),
        "unsupported_instructions": unsupported,
        "resolved_families": resolved_families,
        "remaining_unsupported_families":
            dict(sorted(family_counts.items())),
    }


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Inventory unsupported MIPS lowering instructions")
    parser.add_argument("paths", nargs="+", type=Path)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--previous", type=Path)
    args = parser.parse_args()

    previous_report = None
    if args.previous:
        previous_report = json.loads(args.previous.read_text(encoding="utf-8"))

    reports = []
    for path in args.paths:
        try:
            reports.append(inventory(path, previous_report))
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
