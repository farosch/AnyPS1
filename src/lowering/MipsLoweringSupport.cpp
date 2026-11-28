#include "MipsLoweringSupport.h"
#include "format/PsExe.h"
#include "UnsupportedInstructions.h"

#include <cstdint>
#include <vector>
#include <set>

namespace anyps1 {

MipsInstructionIr build_mips_instruction_ir(
    std::uint32_t address,
    std::uint32_t instruction)
{
    MipsInstructionIr ir;
    ir.address = address;
    ir.word = instruction;
    ir.opcode = instruction >> 26;
    ir.rs = (instruction >> 21) & 31u;
    ir.rt = (instruction >> 16) & 31u;
    ir.rd = (instruction >> 11) & 31u;
    ir.shamt = (instruction >> 6) & 31u;
    ir.immediate = instruction & 0xFFFFu;
    ir.special = instruction & 63u;

    if (instruction == 0u) {
        ir.kind = MipsInstructionKind::Nop;
        return ir;
    }
    if (ir.opcode == 0u) {
        switch (ir.special) {
        case 0x00u: case 0x02u: case 0x03u:
        case 0x04u: case 0x06u: case 0x07u:
            ir.kind = MipsInstructionKind::Shift;
            break;
        case 0x08u:
            ir.kind = MipsInstructionKind::Jr;
            break;
        case 0x09u:
            ir.kind = MipsInstructionKind::Jalr;
            break;
        case 0x0Cu: case 0x0Du:
            ir.kind = MipsInstructionKind::System;
            break;
        case 0x18u: case 0x19u:
            ir.kind = MipsInstructionKind::Multiply;
            break;
        case 0x1Au: case 0x1Bu:
            ir.kind = MipsInstructionKind::Divide;
            break;
        case 0x10u: case 0x11u: case 0x12u: case 0x13u:
            ir.kind = MipsInstructionKind::HiLo;
            break;
        case 0x20u: case 0x21u: case 0x22u: case 0x23u:
            ir.kind = MipsInstructionKind::Arithmetic;
            ir.overflow_check = ir.special == 0x20u || ir.special == 0x22u;
            ir.signed_value = ir.special == 0x20u || ir.special == 0x22u;
            break;
        case 0x24u: case 0x25u: case 0x26u: case 0x27u:
            ir.kind = MipsInstructionKind::Logical;
            break;
        case 0x2Au: case 0x2Bu:
            ir.kind = MipsInstructionKind::Compare;
            ir.signed_value = ir.special == 0x2Au;
            break;
        default:
            break;
        }
        return ir;
    }

    switch (ir.opcode) {
    case 0x01u: case 0x04u: case 0x05u: case 0x06u:
    case 0x07u: case 0x14u: case 0x15u:
    case 0x16u: case 0x17u:
        ir.kind = MipsInstructionKind::Branch;
        if (ir.opcode == 0x01u || ir.opcode == 0x14u ||
            ir.opcode == 0x15u || ir.opcode == 0x16u ||
            ir.opcode == 0x17u) {
            ir.target = static_cast<std::uint32_t>(
                static_cast<std::int64_t>(address) + 4 +
                static_cast<std::int32_t>(static_cast<std::int16_t>(
                    ir.immediate)) * 4);
        } else if (ir.opcode == 0x04u || ir.opcode == 0x05u ||
                   ir.opcode == 0x06u || ir.opcode == 0x07u) {
            ir.target = static_cast<std::uint32_t>(
                static_cast<std::int64_t>(address) + 4 +
                static_cast<std::int32_t>(static_cast<std::int16_t>(
                    ir.immediate)) * 4);
        }
        break;
    case 0x11u:
        if (ir.rs == 8u || ir.rs == 12u) {
            ir.kind = MipsInstructionKind::Branch;
            ir.target = static_cast<std::uint32_t>(
                static_cast<std::int64_t>(address) + 4 +
                static_cast<std::int32_t>(static_cast<std::int16_t>(
                    ir.immediate)) * 4);
        } else {
            ir.kind = MipsInstructionKind::Cop1;
        }
        break;
    case 0x02u: case 0x03u:
        ir.kind = ir.opcode == 0x02u
            ? MipsInstructionKind::Jump
            : MipsInstructionKind::Jump;
        ir.target = ((address + 4u) & 0xF0000000u) |
            ((instruction & 0x03FFFFFFu) << 2u);
        if (ir.opcode == 0x03u) ir.kind = MipsInstructionKind::Jump;
        break;
    case 0x08u: case 0x09u: case 0x0Au: case 0x0Bu:
    case 0x0Cu: case 0x0Du: case 0x0Eu: case 0x0Fu:
        ir.kind = MipsInstructionKind::Immediate;
        ir.signed_value =
            ir.opcode == 0x08u || ir.opcode == 0x09u ||
            ir.opcode == 0x0Au || ir.opcode == 0x0Bu;
        break;
    case 0x10u:
        ir.kind = MipsInstructionKind::Cop0;
        break;
    case 0x12u:
        ir.kind = MipsInstructionKind::Cop2;
        break;
    case 0x13u:
        ir.kind = MipsInstructionKind::Cop3;
        break;
    case 0x20u: case 0x21u: case 0x23u: case 0x24u: case 0x25u:
        ir.kind = MipsInstructionKind::Load;
        ir.size = ir.opcode == 0x20u || ir.opcode == 0x24u
            ? 1u : ir.opcode == 0x21u || ir.opcode == 0x25u
            ? 2u : 4u;
        ir.signed_value = ir.opcode == 0x20u || ir.opcode == 0x21u ||
            ir.opcode == 0x23u;
        break;
    case 0x28u: case 0x29u: case 0x2Bu:
        ir.kind = MipsInstructionKind::Store;
        ir.size = ir.opcode == 0x28u ? 1u :
            ir.opcode == 0x29u ? 2u : 4u;
        break;
    case 0x22u: case 0x26u: case 0x2Au: case 0x2Eu:
        ir.kind = MipsInstructionKind::Unsupported;
        break;
    case 0x30u: case 0x31u: case 0x32u: case 0x33u:
        ir.kind = MipsInstructionKind::CoprocessorLoad;
        ir.size = 4u;
        break;
    case 0x38u: case 0x39u: case 0x3Au: case 0x3Bu:
        ir.kind = MipsInstructionKind::CoprocessorStore;
        ir.size = 4u;
        break;
    case 0x35u: case 0x36u: case 0x37u:
    case 0x3Du: case 0x3Eu: case 0x3Fu:
        ir.kind = MipsInstructionKind::Unsupported;
        break;
    case 0x2Fu:
        ir.kind = MipsInstructionKind::Unsupported;
        break;
    default:
        break;
    }
    return ir;
}

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
    std::vector<unsigned char> pointer_candidate(word_count, 0u);
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
        for (std::uint32_t offset = 0; offset < 4u; ++offset) {
            const std::uint32_t instruction = read_psx32(
                data,
                PSX_HEADER_SIZE + (target_index + offset) * 4u);
            nonzero += instruction != 0;
            if (!is_supported_mips_instruction(instruction)) {
                supported = false;
                break;
            }
        }
        pointer_candidate[index] =
            supported && nonzero >= 3u ? 1u : 0u;
    }

    std::set<std::uint32_t> targets;
    for (std::uint32_t index = 0; index < word_count;) {
        if (pointer_candidate[index] == 0u) {
            ++index;
            continue;
        }
        const std::uint32_t run_start = index;
        while (index < word_count && pointer_candidate[index] != 0u) ++index;
        if (index - run_start < 8u) continue;
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
