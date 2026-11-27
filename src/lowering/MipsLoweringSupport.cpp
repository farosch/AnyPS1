#include "MipsLoweringSupport.h"
#include "format/PsExe.h"
#include "UnsupportedInstructions.h"

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
        const std::uint32_t rs = (instruction >> 21) & 31u;
        const std::uint32_t rt = (instruction >> 16) & 31u;
        const std::uint32_t rd = (instruction >> 11) & 31u;
        const std::uint32_t immediate = instruction & 0xFFFFu;
        if (opcode == 0x0F) {
            if (rt == target_register) {
                value = immediate << 16;
                initialized = true;
            }
        } else if (opcode == 0x09 || opcode == 0x08) {
            if (rt == target_register) {
                if (rs == 0) {
                    value = static_cast<std::uint32_t>(
                        static_cast<std::int32_t>(
                            static_cast<std::int16_t>(immediate)));
                    initialized = true;
                } else if (rs == target_register && initialized) {
                    value = static_cast<std::uint32_t>(
                        static_cast<std::int64_t>(value) +
                        static_cast<std::int16_t>(immediate));
                } else {
                    initialized = false;
                }
            }
        } else if (opcode == 0x0D) {
            if (rt == target_register) {
                if (rs == 0) {
                    value = immediate;
                    initialized = true;
                } else if (rs != target_register || !initialized) {
                    initialized = false;
                } else {
                    value |= immediate;
                }
            }
        } else if (opcode == 0x23 && rt == target_register) {
            if (rs != target_register || !initialized) {
                initialized = false;
                continue;
            }
            const std::uint32_t load_target_address =
                value + static_cast<std::int32_t>(
                    static_cast<std::int16_t>(immediate));
            if (load_target_address < load_address ||
                load_target_address >=
                    load_address + file_size ||
                (load_target_address & 3u) != 0) {
                initialized = false;
                continue;
            }
            value = read_psx32(
                data,
                PSX_HEADER_SIZE + load_target_address - load_address);
            initialized = value >= load_address &&
                value < load_address + file_size &&
                (value & 3u) == 0;
        } else {
            const bool writes_rt =
                (opcode >= 0x08 && opcode <= 0x0E) ||
                (opcode >= 0x10 && opcode <= 0x13 && rs <= 0x02) ||
                (opcode >= 0x20 && opcode <= 0x27);
            const std::uint32_t special = instruction & 63u;
            const bool writes_rd =
                opcode == 0x00 &&
                (special == 0x00 || special == 0x02 ||
                 special == 0x03 || special == 0x04 ||
                 special == 0x06 || special == 0x07 ||
                 special == 0x09 || special == 0x10 ||
                 special == 0x12 || special == 0x20 ||
                 special == 0x21 || special == 0x22 ||
                 special == 0x23 || special == 0x24 ||
                 special == 0x25 || special == 0x26 ||
                 special == 0x27 || special == 0x2A ||
                 special == 0x2B);
            const bool writes_link =
                opcode == 0x03 ||
                (opcode == 0x01 && (rt == 0x10 || rt == 0x11));
            if ((writes_rt && rt == target_register) ||
                (writes_rd && rd == target_register) ||
                (writes_link && target_register == 31u)) {
                initialized = false;
            }
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

std::vector<std::uint32_t> discover_payload_pointer_targets(
    const std::vector<unsigned char>& data,
    std::uint32_t load_address,
    std::uint32_t file_size)
{
    const std::uint32_t word_count = file_size / 4u;
    std::vector<bool> pointer_candidate(word_count, false);
    for (std::uint32_t index = 0; index < word_count; ++index) {
        const std::uint32_t target = read_psx32(
            data,
            PSX_HEADER_SIZE + index * 4u);
        if (target < load_address ||
            target >= load_address + file_size ||
            (target & 3u) != 0) {
            continue;
        }
        const std::uint32_t target_index =
            (target - load_address) / 4u;
        if (target_index + 4u > word_count) continue;
        std::size_t nonzero = 0;
        bool supported = true;
        bool entry_like = false;
        for (std::uint32_t offset = 0; offset < 4u; ++offset) {
            const std::uint32_t instruction = read_psx32(
                data,
                PSX_HEADER_SIZE + (target_index + offset) * 4u);
            if (offset == 0) {
                const std::uint32_t opcode = instruction >> 26;
                const std::uint32_t rt = (instruction >> 16) & 31u;
                entry_like =
                    opcode == 0x0F ||
                    ((opcode == 0x08 || opcode == 0x09) && rt == 29u);
            }
            nonzero += instruction != 0;
            if (!is_supported_mips_instruction(instruction)) {
                supported = false;
                break;
            }
        }
        pointer_candidate[index] =
            entry_like && supported && nonzero >= 3u;
    }

    std::set<std::uint32_t> targets;
    for (std::uint32_t index = 0; index < word_count;) {
        if (!pointer_candidate[index]) {
            ++index;
            continue;
        }
        const std::uint32_t run_start = index;
        while (index < word_count && pointer_candidate[index]) ++index;
        if (index - run_start < 2u) continue;
        for (std::uint32_t table_index = run_start;
             table_index < index;
             ++table_index) {
            targets.insert(read_psx32(
                data,
                PSX_HEADER_SIZE + table_index * 4u));
        }
    }
    return {targets.begin(), targets.end()};
}

}
