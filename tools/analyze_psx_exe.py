import argparse
import struct
from pathlib import Path


REG_NAMES = (
    "zero", "at", "v0", "v1", "a0", "a1", "a2", "a3",
    "t0", "t1", "t2", "t3", "t4", "t5", "t6", "t7",
    "s0", "s1", "s2", "s3", "s4", "s5", "s6", "s7",
    "t8", "t9", "k0", "k1", "gp", "sp", "fp", "ra",
)

SPECIAL_NAMES = {
    0x00: "sll", 0x02: "srl", 0x03: "sra", 0x08: "jr",
    0x09: "jalr", 0x0C: "syscall", 0x0D: "break",
    0x10: "mfhi", 0x11: "mthi", 0x12: "mflo", 0x13: "mtlo",
    0x18: "mult", 0x19: "multu", 0x1A: "div", 0x1B: "divu",
    0x20: "add", 0x21: "addu", 0x22: "sub", 0x23: "subu",
    0x24: "and", 0x25: "or", 0x26: "xor", 0x27: "nor",
    0x2A: "slt", 0x2B: "sltu",
}

BRANCH_NAMES = {
    0x00: "bltz", 0x01: "bgez", 0x10: "bltzal", 0x11: "bgezal",
}

OP_NAMES = {
    0x02: "j", 0x03: "jal",
    0x04: "beq", 0x05: "bne", 0x06: "blez", 0x07: "bgtz",
    0x08: "addi", 0x09: "addiu", 0x0A: "slti", 0x0B: "sltiu",
    0x0C: "andi", 0x0D: "ori", 0x0E: "xori", 0x0F: "lui",
    0x20: "lb", 0x21: "lh", 0x22: "lwl", 0x23: "lw",
    0x24: "lbu", 0x25: "lhu", 0x26: "lwr", 0x28: "sb",
    0x29: "sh", 0x2A: "swl", 0x2B: "sw", 0x2E: "swr",
    0x32: "lwc2", 0x3A: "swc2",
}


def sign_extend16(value: int) -> int:
    return value - 0x10000 if value & 0x8000 else value


def disassemble_mips(address: int, word: int) -> str:
    opcode = (word >> 26) & 0x3F
    rs = (word >> 21) & 0x1F
    rt = (word >> 16) & 0x1F
    rd = (word >> 11) & 0x1F
    shamt = (word >> 6) & 0x1F
    immediate = word & 0xFFFF
    index = word & 0x03FFFFFF

    if opcode == 0:
        name = SPECIAL_NAMES.get((word & 0x3F))
        if name is None:
            return "unknown"
        if name in ("sll", "srl", "sra"):
            if word == 0:
                return "nop"
            return f"{name} {REG_NAMES[rd]}, {REG_NAMES[rt]}, {shamt}"
        if name in ("jr", "jalr"):
            return f"{name} {REG_NAMES[rs]}" if name == "jr" else f"{name} {REG_NAMES[rd]}, {REG_NAMES[rs]}"
        if name in ("mfhi", "mflo"):
            return f"{name} {REG_NAMES[rd]}"
        if name in ("mthi", "mtlo"):
            return f"{name} {REG_NAMES[rs]}"
        if name in ("mult", "multu", "div", "divu"):
            return f"{name} {REG_NAMES[rs]}, {REG_NAMES[rt]}"
        if name in ("syscall", "break"):
            return name
        return f"{name} {REG_NAMES[rd]}, {REG_NAMES[rs]}, {REG_NAMES[rt]}"

    if opcode == 0x01:
        name = BRANCH_NAMES.get(rt)
        if name is None:
            return "unknown"
        target = address + 4 + (sign_extend16(immediate) << 2)
        return f"{name} {REG_NAMES[rs]}, 0x{target & 0xFFFFFFFF:08X}"

    if opcode in (0x02, 0x03):
        target = ((address + 4) & 0xF0000000) | (index << 2)
        return f"{OP_NAMES[opcode]} 0x{target & 0xFFFFFFFF:08X}"

    if opcode in (0x04, 0x05):
        target = address + 4 + (sign_extend16(immediate) << 2)
        return f"{OP_NAMES[opcode]} {REG_NAMES[rs]}, {REG_NAMES[rt]}, 0x{target & 0xFFFFFFFF:08X}"

    if opcode in (0x06, 0x07):
        target = address + 4 + (sign_extend16(immediate) << 2)
        return f"{OP_NAMES[opcode]} {REG_NAMES[rs]}, 0x{target & 0xFFFFFFFF:08X}"

    if opcode == 0x10:
        return disassemble_coprocessor("cop0", rs, rt, rd, word)

    if opcode == 0x12:
        return disassemble_coprocessor("cop2", rs, rt, rd, word)

    if opcode == 0x11 or opcode == 0x13:
        return f"cop{opcode - 0x10} 0x{word & 0x03FFFFFF:X}"

    name = OP_NAMES.get(opcode)
    if name is None:
        return "unknown"
    if opcode == 0x0F:
        return f"{name} {REG_NAMES[rt]}, 0x{immediate:04X}"
    if opcode in range(0x20, 0x2F):
        return f"{name} {REG_NAMES[rt]}, {sign_extend16(immediate)}({REG_NAMES[rs]})"
    if opcode in (0x08, 0x09, 0x0A, 0x0B):
        return f"{name} {REG_NAMES[rt]}, {REG_NAMES[rs]}, {sign_extend16(immediate)}"
    return f"{name} {REG_NAMES[rt]}, {REG_NAMES[rs]}, 0x{immediate:04X}"


def disassemble_coprocessor(name: str, rs: int, rt: int, rd: int, word: int) -> str:
    if rs == 0:
        return f"mfc{name[-1]} {REG_NAMES[rt]}, {rd}"
    if rs == 4:
        return f"mtc{name[-1]} {REG_NAMES[rt]}, {rd}"
    if rs == 16:
        return f"{name} 0x{word & 0x00FFFFFF:06X}"
    return f"{name} 0x{word & 0x03FFFFFF:X}"


def disassemble_range(code: bytes, base_address: int, start: int = 0, count: int = 64):
    for offset in range(start, min(start + count * 4, len(code)), 4):
        if offset + 4 > len(code):
            break
        word = struct.unpack_from("<I", code, offset)[0]
        address = base_address + offset
        yield address, word, disassemble_mips(address, word)


def parse_psx_header(path: Path) -> tuple[int, bytes]:
    data = path.read_bytes()
    if len(data) >= 64 and data[:8] == b"PS-X EXE":
        base_address = struct.unpack_from("<I", data, 0x18)[0]
        file_size = struct.unpack_from("<I", data, 0x1C)[0]
        code = data[0x800:0x800 + file_size]
        if len(code) != file_size:
            raise ValueError("declared executable size exceeds available file data")
        return base_address, code
    return 0x80010000, data


def parse_integer(value: str) -> int:
    return int(value, 0)


def main() -> int:
    parser = argparse.ArgumentParser(description="Disassemble a PS-X EXE code section")
    parser.add_argument("executable", type=Path)
    parser.add_argument("--count", type=int, default=64)
    parser.add_argument("--start", type=parse_integer, default=0)
    args = parser.parse_args()

    try:
        base_address, code = parse_psx_header(args.executable)
        print(f"Base: 0x{base_address:08X}")
        print(f"Code: {len(code)} bytes")
        for address, word, text in disassemble_range(code, base_address, args.start, args.count):
            print(f"0x{address:08X}: {word:08X}  {text}")
    except (OSError, ValueError) as error:
        print(f"Error: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    import sys
    raise SystemExit(main())
