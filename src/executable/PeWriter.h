#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace anyps1 {
struct RelinkOptions;
struct NativeLoweringIr;
void write_pe_layout(
    const RelinkOptions& options,
    const std::vector<unsigned char>& data,
    std::uint32_t load_address,
    std::uint32_t file_size);
void write_pe_ir(
    const RelinkOptions& options,
    const NativeLoweringIr& ir);
}
