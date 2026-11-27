#include "MipsLoweringSupport.h"
#include "format/PsExe.h"

#include <cstdint>
#include <vector>
#include <set>

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

std::vector<std::uint32_t> discover_indirect_table_targets(
    const std::vector<unsigned char>& data,
    std::uint32_t load_address,
    std::uint32_t file_size,
    std::uint32_t instruction_address,
    std::uint32_t index_register)
{
    const std::uint32_t table_base =
        resolve_static_indirect_target(
            data,
            load_address,
            file_size,
            instruction_address,
            index_register);
    if (table_base < load_address ||
        table_base >= load_address + file_size ||
        (table_base & 3u) != 0) {
        return {};
    }
    std::set<std::uint32_t> targets;
    for (std::uint32_t index = 0; index < 1024u; ++index) {
        const std::uint64_t address =
            static_cast<std::uint64_t>(table_base) + index * 4u;
        if (address + 4 >
            static_cast<std::uint64_t>(load_address) + file_size) {
            break;
        }
        const std::uint32_t target = read_psx32(
            data,
            PSX_HEADER_SIZE +
                static_cast<std::uint32_t>(address - load_address));
        if (target < load_address ||
            target >= load_address + file_size ||
            (target & 3u) != 0) {
            break;
        }
        targets.insert(target);
    }
    return {targets.begin(), targets.end()};
}

}
