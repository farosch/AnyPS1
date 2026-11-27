#pragma once

#include <cstdint>
#include <string>

namespace anyps1 {

enum class UnsupportedInstructionKind {
    Special,
    Cop0,
    Cop1,
    Cop2,
    Cop3,
    LoadStore,
    ReservedOpcode
};

struct UnsupportedInstruction {
    UnsupportedInstructionKind kind =
        UnsupportedInstructionKind::ReservedOpcode;
    std::string name = "unknown";
};

struct MipsInstructionOperands {
    std::uint32_t rs = 0;
    std::uint32_t rt = 0;
    std::uint32_t rd = 0;
    std::uint32_t shamt = 0;
    std::uint32_t immediate = 0;
    std::uint32_t index = 0;
    std::uint32_t code = 0;
    std::string name = "unknown";
    std::string format = "unknown";
    bool reserved_special = false;
};

MipsInstructionOperands decode_mips_operands(std::uint32_t instruction);
std::string instruction_family_source_name(const std::string& family);

UnsupportedInstruction classify_unsupported_instruction(
    std::uint32_t instruction);
bool is_supported_mips_instruction(std::uint32_t instruction);

}
