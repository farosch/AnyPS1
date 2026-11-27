#include "MipsLoweringSupport.h"
#include "format/PsExe.h"

#include <cstdint>
#include <vector>

namespace anyps1 {

std::uint32_t resolve_static_indirect_target(
    const std::vector<unsigned char>& data,
    std::uint32_t load_address,
    std::uint32_t file_size,
    std::uint32_t instruction_address,
    std::uint32_t target_register)
{
    std::uint32_t value = 0;
    bool initialized = false;
    for (std::int32_t distance = -16; distance <= 0; distance += 4) {
        const std::int64_t word_address =
            static_cast<std::int64_t>(instruction_address) + distance;
        if (word_address < load_address ||
            word_address >=
                static_cast<std::int64_t>(load_address) + file_size) {
            continue;
        }
        const std::uint32_t instruction = read_psx32(
            data,
            PSX_HEADER_SIZE +
                static_cast<std::uint32_t>(word_address - load_address));
        const std::uint32_t opcode = instruction >> 26;
        const std::uint32_t rt = (instruction >> 16) & 31u;
        const std::uint32_t immediate = instruction & 0xFFFFu;
        if (rt != target_register) continue;
        if (opcode == 0x0F) {
            value = immediate << 16;
            initialized = true;
        } else if (opcode == 0x09) {
            value = static_cast<std::uint32_t>(
                static_cast<std::int64_t>(value) +
                static_cast<std::int16_t>(immediate));
            initialized = true;
        } else if (opcode == 0x0D) {
            value |= immediate;
            initialized = true;
        }
    }
    return initialized ? value : 0;
}

}
