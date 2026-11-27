#include "lowering/UnsupportedInstructions.h"

#include <cstdint>
#include <iostream>
#include <stdexcept>

int main()
{
    try {
        for (std::uint32_t function = 0; function < 64; ++function) {
            const std::uint32_t instruction =
                (0x1Au << 21) | (0x0Bu << 16) | (0x0Cu << 11) |
                (0x07u << 6) | function;
            const anyps1::MipsInstructionOperands operands =
                anyps1::decode_mips_operands(instruction);
            if (operands.rs != 0x1A || operands.rt != 0x0B ||
                operands.rd != 0x0C || operands.shamt != 7 ||
                operands.name.empty() || operands.format.empty()) {
                throw std::runtime_error(
                    "SPECIAL operand decoding is incomplete");
            }

            if (anyps1::instruction_family_source_name("sdc2") !=
                "unsupported_sdc2_stub") {
                throw std::runtime_error("source-fix stub name is incorrect");
            }
            if (anyps1::instruction_family_source_name("reserved_special_05") !=
                "unsupported_reserved_special_05_stub") {
                throw std::runtime_error("reserved source-fix stub name is incorrect");
            }
        }

        const anyps1::MipsInstructionOperands reserved_operands =
            anyps1::decode_mips_operands(
                (0x08u << 21) | (0x09u << 16) |
                (0x0Au << 11) | (5u << 6) | 0x05u);
        if (!reserved_operands.reserved_special ||
            reserved_operands.name != "reserved_special_05" ||
            reserved_operands.format != "rs, rt, rd, sa" ||
            reserved_operands.rs != 8 || reserved_operands.rt != 9 ||
            reserved_operands.rd != 10 || reserved_operands.shamt != 5) {
            throw std::runtime_error(
                "reserved SPECIAL operand decoding is incorrect");
        }

        const anyps1::UnsupportedInstruction cop1 =
            anyps1::classify_unsupported_instruction(0x44400000u);
        if (cop1.kind != anyps1::UnsupportedInstructionKind::Cop1 ||
            cop1.name != "cop1") {
            throw std::runtime_error("COP1 instruction was not classified");
        }

        const anyps1::UnsupportedInstruction cop2 =
            anyps1::classify_unsupported_instruction(0x48C8E800u);
        if (cop2.kind != anyps1::UnsupportedInstructionKind::Cop2 ||
            cop2.name != "gte_sqr") {
            throw std::runtime_error("COP2/GTE instruction was not classified");
        }

        const anyps1::UnsupportedInstruction lwc1 =
            anyps1::classify_unsupported_instruction(0xC4200000u);
        if (lwc1.kind != anyps1::UnsupportedInstructionKind::LoadStore ||
            lwc1.name != "lwc1") {
            throw std::runtime_error("LWC1 instruction was not classified");
        }

        const anyps1::UnsupportedInstruction reserved_opcode =
            anyps1::classify_unsupported_instruction(0xFC000000u);
        if (reserved_opcode.kind != anyps1::UnsupportedInstructionKind::ReservedOpcode ||
            reserved_opcode.name != "reserved_opcode_3f") {
            throw std::runtime_error("reserved opcode was not classified");
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Unsupported instruction test failed: " << error.what() << "\n";
        return 1;
    }
}
