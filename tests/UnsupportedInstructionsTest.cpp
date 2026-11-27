#include "lowering/UnsupportedInstructions.h"

#include <cstdint>
#include <iostream>
#include <stdexcept>

int main()
{
    try {
        const anyps1::UnsupportedInstruction cop1 =
            anyps1::classify_unsupported_instruction(0x44400000u);
        if (cop1.kind != anyps1::UnsupportedInstructionKind::Cop1 ||
            cop1.name != "cop1") {
            throw std::runtime_error("COP1 instruction was not classified");
        }

        const anyps1::UnsupportedInstruction cop2 =
            anyps1::classify_unsupported_instruction(0x48C8E800u);
        if (cop2.kind != anyps1::UnsupportedInstructionKind::Cop2 ||
            cop2.name != "cop2") {
            throw std::runtime_error("COP2/GTE instruction was not classified");
        }

        const anyps1::UnsupportedInstruction lwc1 =
            anyps1::classify_unsupported_instruction(0xC4200000u);
        if (lwc1.kind != anyps1::UnsupportedInstructionKind::LoadStore ||
            lwc1.name != "lwc1") {
            throw std::runtime_error("LWC1 instruction was not classified");
        }

        const anyps1::UnsupportedInstruction reserved =
            anyps1::classify_unsupported_instruction(0xFC000000u);
        if (reserved.kind != anyps1::UnsupportedInstructionKind::ReservedOpcode ||
            reserved.name != "unknown_opcode_3f") {
            throw std::runtime_error("reserved opcode was not classified");
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Unsupported instruction test failed: " << error.what() << "\n";
        return 1;
    }
}
