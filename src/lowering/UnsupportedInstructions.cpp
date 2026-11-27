#include "UnsupportedInstructions.h"

#include <array>
#include <string>

namespace anyps1 {

namespace {

const std::array<const char*, 64> OPCODE_NAMES = {{
    "special", "regimm", "j", "jal", "beq", "bne", "blez", "bgtz",
    "addi", "addiu", "slti", "sltiu", "andi", "ori", "xori", "lui",
    "cop0", "cop1", "cop2", "cop3", "beql", "bnel", "blezl", "bgtzl",
    "unknown_opcode_18", "unknown_opcode_19", "unknown_opcode_1a", "unknown_opcode_1b",
    "unknown_opcode_1c", "unknown_opcode_1d", "unknown_opcode_1e", "unknown_opcode_1f",
    "lb", "lh", "lwl", "lw", "lbu", "lhu", "lwr", "unknown_opcode_27",
    "sb", "sh", "swl", "sw", "unknown_opcode_2c", "unknown_opcode_2d", "swr",
    "cache", "lwc0", "lwc1", "lwc2", "lwc3", "unknown_opcode_35", "ldc1",
    "ldc2", "unknown_opcode_38", "swc0", "swc1", "swc2", "swc3",
    "unknown_opcode_3d", "sdc1", "sdc2", "unknown_opcode_3f"
}};

}

UnsupportedInstruction classify_unsupported_instruction(
    std::uint32_t instruction)
{
    const std::uint32_t opcode = instruction >> 26;
    if (opcode == 0) {
        const std::uint32_t special = instruction & 63u;
        switch (special) {
        case 0x00: return {UnsupportedInstructionKind::Special, "sll"};
        case 0x02: return {UnsupportedInstructionKind::Special, "srl"};
        case 0x03: return {UnsupportedInstructionKind::Special, "sra"};
        case 0x04: return {UnsupportedInstructionKind::Special, "sllv"};
        case 0x06: return {UnsupportedInstructionKind::Special, "srlv"};
        case 0x07: return {UnsupportedInstructionKind::Special, "srav"};
        case 0x08: return {UnsupportedInstructionKind::Special, "jr"};
        case 0x09: return {UnsupportedInstructionKind::Special, "jalr"};
        case 0x0C: return {UnsupportedInstructionKind::Special, "syscall"};
        case 0x0D: return {UnsupportedInstructionKind::Special, "break"};
        case 0x10: return {UnsupportedInstructionKind::Special, "mfhi"};
        case 0x11: return {UnsupportedInstructionKind::Special, "mthi"};
        case 0x12: return {UnsupportedInstructionKind::Special, "mflo"};
        case 0x13: return {UnsupportedInstructionKind::Special, "mtlo"};
        case 0x18: return {UnsupportedInstructionKind::Special, "mult"};
        case 0x19: return {UnsupportedInstructionKind::Special, "multu"};
        case 0x1A: return {UnsupportedInstructionKind::Special, "div"};
        case 0x1B: return {UnsupportedInstructionKind::Special, "divu"};
        case 0x20: return {UnsupportedInstructionKind::Special, "add"};
        case 0x21: return {UnsupportedInstructionKind::Special, "addu"};
        case 0x22: return {UnsupportedInstructionKind::Special, "sub"};
        case 0x23: return {UnsupportedInstructionKind::Special, "subu"};
        case 0x24: return {UnsupportedInstructionKind::Special, "and"};
        case 0x25: return {UnsupportedInstructionKind::Special, "or"};
        case 0x26: return {UnsupportedInstructionKind::Special, "xor"};
        case 0x27: return {UnsupportedInstructionKind::Special, "nor"};
        case 0x2A: return {UnsupportedInstructionKind::Special, "slt"};
        case 0x2B: return {UnsupportedInstructionKind::Special, "sltu"};
        default:
            return {UnsupportedInstructionKind::Special,
                    "unknown_special_" + std::to_string(special)};
        }
    }
    if (opcode == 0x10) {
        return {UnsupportedInstructionKind::Cop0, "cop0"};
    }
    if (opcode == 0x11) {
        return {UnsupportedInstructionKind::Cop1, "cop1"};
    }
    if (opcode == 0x12) {
        switch (instruction) {
        case 0x48C0C000u: return {UnsupportedInstructionKind::Cop2, "gte_nclip"};
        case 0x48C0C800u: return {UnsupportedInstructionKind::Cop2, "gte_op"};
        case 0x48C8D000u: return {UnsupportedInstructionKind::Cop2, "gte_dpcs"};
        case 0x48C8D800u: return {UnsupportedInstructionKind::Cop2, "gte_intpl"};
        case 0x48C8E000u: return {UnsupportedInstructionKind::Cop2, "gte_mvmva"};
        case 0x48C8E800u: return {UnsupportedInstructionKind::Cop2, "gte_sqr"};
        case 0x48C8F000u: return {UnsupportedInstructionKind::Cop2, "gte_rtv0"};
        default: return {UnsupportedInstructionKind::Cop2, "cop2"};
        }
    }
    if (opcode == 0x13) {
        return {UnsupportedInstructionKind::Cop3, "cop3"};
    }
    if ((opcode >= 0x20 && opcode <= 0x2E) ||
        (opcode >= 0x30 && opcode <= 0x3B)) {
        return {UnsupportedInstructionKind::LoadStore,
                OPCODE_NAMES[opcode]};
    }
    return {UnsupportedInstructionKind::ReservedOpcode,
            OPCODE_NAMES[opcode]};
}

bool is_supported_mips_instruction(std::uint32_t instruction)
{
    const std::uint32_t opcode = instruction >> 26;
    const std::uint32_t rs = (instruction >> 21) & 31u;
    const std::uint32_t special = instruction & 63u;
    if (instruction == 0) return true;
    if (opcode == 0) {
        switch (special) {
        case 0x00: case 0x02: case 0x03: case 0x04:
        case 0x06: case 0x07: case 0x08: case 0x09:
        case 0x0C: case 0x0D: case 0x10: case 0x11:
        case 0x12: case 0x13: case 0x18: case 0x19:
        case 0x1A: case 0x1B: case 0x20: case 0x21:
        case 0x22: case 0x23: case 0x24: case 0x25:
        case 0x26: case 0x27: case 0x2A: case 0x2B:
            return true;
        default:
            return false;
        }
    }
    if (opcode == 0x10) {
        return rs == 0x00 || rs == 0x02 || rs == 0x04 ||
               rs == 0x06 || (rs == 0x10 && special == 0x10);
    }
    switch (opcode) {
    case 0x01: case 0x02: case 0x03: case 0x04:
    case 0x05: case 0x06: case 0x07: case 0x08:
    case 0x09: case 0x0A: case 0x0B: case 0x0C:
    case 0x0D: case 0x0E: case 0x0F: case 0x11:
    case 0x20:
    case 0x21: case 0x22: case 0x23: case 0x24:
    case 0x25: case 0x26: case 0x28: case 0x29:
    case 0x2A: case 0x2B: case 0x2E: case 0x31:
    case 0x39:
        return true;
    default:
        return false;
    }
}

}
