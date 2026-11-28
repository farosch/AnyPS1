#include "UnsupportedInstructions.h"
#include "MipsOpcodeRegistry.h"

#include <array>
#include <string>

namespace anyps1 {

namespace {
}

MipsInstructionOperands decode_mips_operands(std::uint32_t instruction)
{
    MipsInstructionOperands result;
    result.rs = (instruction >> 21) & 31u;
    result.rt = (instruction >> 16) & 31u;
    result.rd = (instruction >> 11) & 31u;
    result.shamt = (instruction >> 6) & 31u;
    result.immediate = instruction & 0xFFFFu;
    result.index = instruction & 0x03FFFFFFu;
    result.code = (instruction >> 6) & 0xFFFFFu;

    const std::uint32_t opcode = instruction >> 26;
    if (opcode == 0) {
        const std::uint32_t special = instruction & 63u;
        const auto& entry = mips_registry::special_entries[special];
        result.name = entry.name;
        result.format = entry.format;
        result.reserved_special = entry.reserved;
    } else {
        result.name = mips_registry::opcode_names[opcode];
        result.format = opcode == 0x01
            ? "rs, rt, immediate"
            : opcode == 0x02 || opcode == 0x03
                ? "index"
                : opcode >= 0x20 && opcode <= 0x2E
                    ? "rt, offset(rs)"
                    : "rs, rt, immediate";
    }
    return result;
}

std::string instruction_family_source_name(const std::string& family)
{
    std::string result = "unsupported_";
    for (char character : family) {
        if ((character >= 'a' && character <= 'z') ||
            (character >= 'A' && character <= 'Z') ||
            (character >= '0' && character <= '9')) {
            result += character;
        } else {
            result += '_';
        }
    }
    result += "_stub";
    return result;
}

UnsupportedInstruction classify_unsupported_instruction(
    std::uint32_t instruction)
{
    const std::uint32_t opcode = instruction >> 26;
    if (opcode == 0) {
        const std::uint32_t special = instruction & 63u;
        const char* name = mips_registry::special_entries[special].name;
        switch (special) {
        case 0x00: case 0x02: case 0x03: case 0x04:
        case 0x06: case 0x07: case 0x08: case 0x09:
        case 0x0C: case 0x0D: case 0x10: case 0x11:
        case 0x12: case 0x13: case 0x18: case 0x19:
        case 0x1A: case 0x1B: case 0x20: case 0x21:
        case 0x22: case 0x23: case 0x24: case 0x25:
        case 0x26: case 0x27: case 0x2A: case 0x2B:
            return {UnsupportedInstructionKind::Special, name};
        default:
            return {UnsupportedInstructionKind::Special, name};
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
                mips_registry::opcode_names[opcode]};
    }
    return {UnsupportedInstructionKind::ReservedOpcode,
            mips_registry::opcode_names[opcode]};
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
    if (opcode == 0x11) {
        if (rs == 16) {
            return special <= 7 || (special >= 12 && special <= 15) ||
                special == 0x20 || special == 0x24 ||
                (special >= 0x30 && special <= 0x37);
        }
        return rs == 0 || rs == 2 || rs == 4 || rs == 6 ||
            rs == 8;
    }
    if (opcode == 0x01) {
        const std::uint32_t rt = (instruction >> 16) & 31u;
        return rt == 0x00u || rt == 0x01u ||
            rt == 0x10u || rt == 0x11u;
    }
    if (opcode == 0x06 || opcode == 0x07 ||
        opcode == 0x16 || opcode == 0x17) {
        return ((instruction >> 16) & 31u) == 0u;
    }
    if (opcode == 0x13) {
        return rs == 0 || rs == 2 || rs == 4 || rs == 6;
    }
    if (opcode == 0x10) {
        return rs == 0x00 || rs == 0x02 || rs == 0x04 ||
               rs == 0x06 || (rs == 0x10 && special == 0x10);
    }
    if (opcode == 0x12) {
        if (rs == 0x00 || rs == 0x02 || rs == 0x04 || rs == 0x06) {
            return true;
        }
        const std::uint32_t function = instruction & 63u;
        return rs >= 16 &&
            (function == 1 || function == 0x06 ||
             function == 0x0C || function == 0x10 ||
             function == 0x11 || function == 0x12 ||
             function == 0x1C || function == 0x28 ||
             function == 0x3D || function == 0x3E);
    }
    switch (opcode) {
    case 0x02: case 0x03: case 0x04:
    case 0x05: case 0x06: case 0x07: case 0x08:
    case 0x09: case 0x0A: case 0x0B: case 0x0C:
    case 0x0D: case 0x0E:     case 0x0F:
    case 0x14: case 0x15:
    case 0x20:
    case 0x21: case 0x22: case 0x23: case 0x24:
    case 0x25: case 0x26: case 0x28: case 0x29:
    case 0x2A: case 0x2B: case 0x2E: case 0x2F:
    case 0x31: case 0x32: case 0x39: case 0x3A:
        return true;
    default:
        return false;
    }
}

}
