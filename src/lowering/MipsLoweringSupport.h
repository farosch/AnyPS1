#pragma once

#include <cstdint>
#include <vector>

namespace anyps1 {

enum class MipsInstructionKind {
    Unsupported,
    Nop,
    Shift,
    Arithmetic,
    Logical,
    Compare,
    Multiply,
    Divide,
    HiLo,
    Cop0,
    Cop1,
    Cop2,
    Cop3,
    Immediate,
    Branch,
    Jump,
    Jr,
    Jalr,
    Load,
    Store,
    CoprocessorLoad,
    CoprocessorStore,
    System,
};

struct MipsInstructionIr {
    MipsInstructionKind kind = MipsInstructionKind::Unsupported;
    std::uint32_t address = 0;
    std::uint32_t word = 0;
    std::uint32_t opcode = 0;
    std::uint32_t rs = 0;
    std::uint32_t rt = 0;
    std::uint32_t rd = 0;
    std::uint32_t shamt = 0;
    std::uint32_t immediate = 0;
    std::uint32_t special = 0;
    std::uint32_t target = 0;
    std::uint32_t size = 0;
    bool signed_value = false;
    bool overflow_check = false;
    bool supported = false;
};

MipsInstructionIr build_mips_instruction_ir(
    std::uint32_t address,
    std::uint32_t instruction);

std::uint32_t resolve_static_indirect_target(
    const std::vector<unsigned char>& data,
    std::uint32_t load_address,
    std::uint32_t file_size,
    std::uint32_t instruction_address,
    std::uint32_t target_register);

std::vector<std::uint32_t> discover_indirect_table_targets(
    const std::vector<unsigned char>& data,
    std::uint32_t load_address,
    std::uint32_t file_size,
    std::uint32_t instruction_address,
    std::uint32_t index_register);

std::vector<std::uint32_t> discover_payload_pointer_targets(
    const std::vector<unsigned char>& data,
    std::uint32_t load_address,
    std::uint32_t file_size);

}
