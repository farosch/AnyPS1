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
        return {UnsupportedInstructionKind::Cop2, "cop2"};
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

}
