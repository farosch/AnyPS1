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

UnsupportedInstruction classify_unsupported_instruction(
    std::uint32_t instruction);
bool is_supported_mips_instruction(std::uint32_t instruction);

}
