#pragma once

#include <cstdint>
#include <vector>

namespace anyps1 {

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
