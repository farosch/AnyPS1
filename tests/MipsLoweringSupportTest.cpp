#include "lowering/MipsLoweringSupport.h"
#include "lowering/MipsLowerer.h"
#include "format/PsExe.h"
#include "relinker/Relinker.h"

#include <cstdint>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {

void write_word(
    std::vector<unsigned char>& data,
    std::uint32_t index,
    std::uint32_t word)
{
    const std::uint32_t offset =
        anyps1::PSX_HEADER_SIZE + index * 4u;
    data[offset] = static_cast<unsigned char>(word);
    data[offset + 1] = static_cast<unsigned char>(word >> 8);
    data[offset + 2] = static_cast<unsigned char>(word >> 16);
    data[offset + 3] = static_cast<unsigned char>(word >> 24);
}

void write_psx_field(
    std::vector<unsigned char>& data,
    std::uint32_t offset,
    std::uint32_t value)
{
    data[offset] = static_cast<unsigned char>(value);
    data[offset + 1] = static_cast<unsigned char>(value >> 8);
    data[offset + 2] = static_cast<unsigned char>(value >> 16);
    data[offset + 3] = static_cast<unsigned char>(value >> 24);
}

}

int main()
{
    try {
        std::vector<unsigned char> sparse_data(
            anyps1::PSX_HEADER_SIZE + 520u);
        write_psx_field(sparse_data, 0x10u, 0x80010000u);
        write_psx_field(sparse_data, 0x18u, 0x80010000u);
        write_psx_field(sparse_data, 0x1Cu, 520u);
        write_word(sparse_data, 0u, 0u);
        write_word(sparse_data, 128u, 0x10000000u);
        anyps1::RelinkOptions sparse_options;
        const anyps1::MipsExecutablePlan sparse_plan =
            anyps1::build_mips_executable_plan(
                sparse_options,
                sparse_data);
        if (sparse_plan.leader_dispatch.size() < 2u) {
            throw std::runtime_error(
                "sparse leader dispatch did not span address buckets");
        }
        if (sparse_plan.leader_chunks.empty()) {
            throw std::runtime_error(
                "native lowering chunks were not created");
        }
        for (const std::vector<std::uint32_t>& chunk :
             sparse_plan.leader_chunks) {
            if (chunk.size() > 512u) {
                throw std::runtime_error(
                    "native lowering chunk exceeds 512 leaders");
            }
        }
        const anyps1::MipsInstructionIr nop =
            anyps1::build_mips_instruction_ir(0x80010000u, 0u);
        if (nop.kind != anyps1::MipsInstructionKind::Nop) {
            throw std::runtime_error("NOP was not decoded into the instruction IR");
        }
        const anyps1::MipsInstructionIr branch =
            anyps1::build_mips_instruction_ir(
                0x80010000u,
                0x10000002u);
        if (branch.kind != anyps1::MipsInstructionKind::Branch ||
            branch.target != 0x8001000Cu) {
            throw std::runtime_error("branch was not decoded into the instruction IR");
        }
        const anyps1::MipsInstructionIr load =
            anyps1::build_mips_instruction_ir(
                0x80010000u,
                0x8D020020u);
        if (load.kind != anyps1::MipsInstructionKind::Load ||
            load.size != 4u ||
            load.rs != 8u ||
            load.rt != 2u) {
            throw std::runtime_error("load was not decoded into the instruction IR");
        }
        constexpr std::uint32_t load_address = 0x80010000u;
        constexpr std::uint32_t file_size = 112u;
        std::vector<unsigned char> data(
            anyps1::PSX_HEADER_SIZE + file_size);
        write_word(data, 0, 0x3C088001u);
        write_word(data, 1, 0x35080020u);
        const std::uint32_t resolved =
            anyps1::resolve_static_indirect_target(
                data,
                load_address,
                file_size,
                load_address + 8u,
                8u);
        if (resolved != load_address + 0x20u) {
            throw std::runtime_error("constant target was not resolved");
        }

        write_word(data, 0, 0x240800A0u);
        const std::uint32_t zero_based =
            anyps1::resolve_static_indirect_target(
                data,
                load_address,
                file_size,
                load_address + 4u,
                8u);
        if (zero_based != 0xA0u) {
            throw std::runtime_error(
                "zero-based immediate target was not resolved");
        }
        write_word(data, 0, 0x3C088001u);

        write_word(data, 2, 0x8D084000u);
        write_word(data, 3, 0x01000008u);
        const std::uint32_t overwritten =
            anyps1::resolve_static_indirect_target(
                data,
                load_address,
                file_size,
                load_address + 12u,
                8u);
        if (overwritten != 0) {
            throw std::runtime_error(
                "constant target survived an intervening register load");
        }

        write_word(data, 1, 0u);
        write_word(data, 2, 0x8D080020u);
        write_word(data, 8, load_address + 28u);
        const std::uint32_t loaded =
            anyps1::resolve_static_indirect_target(
                data,
                load_address,
                file_size,
                load_address + 12u,
                8u);
        if (loaded != load_address + 28u) {
            throw std::runtime_error("payload-loaded target was not resolved");
        }

        write_word(data, 8, 0x34028001u);
        write_word(data, 9, 0x8C420020u);
        write_word(data, 10, 0x24420004u);
        write_word(data, 11, 0u);
        write_word(data, 12, 0x3C048001u);
        write_word(data, 13, 0x24840040u);
        write_word(data, 14, 0x24840004u);
        write_word(data, 15, 0u);
        write_word(data, 16, 0x34048001u);
        write_word(data, 17, 0xAC840000u);
        write_word(data, 18, 0x3C048001u);
        write_word(data, 19, 0u);
        write_word(data, 20, load_address + 32u);
        write_word(data, 21, load_address + 48u);
        write_word(data, 22, load_address + 64u);
        write_word(data, 23, load_address + 32u);
        write_word(data, 24, load_address + 48u);
        write_word(data, 25, load_address + 64u);
        write_word(data, 26, load_address + 32u);
        write_word(data, 27, load_address + 48u);
        const std::vector<std::uint32_t> pointer_targets =
            anyps1::discover_payload_pointer_targets(
                data,
                load_address,
                file_size);
        if (pointer_targets.size() != 3u ||
            pointer_targets[0] != load_address + 32u ||
            pointer_targets[1] != load_address + 48u ||
            pointer_targets[2] != load_address + 64u) {
            throw std::runtime_error(
                "contiguous payload pointer table was not discovered");
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "MIPS lowering support test failed: "
                  << error.what() << "\n";
        return 1;
    }
}
