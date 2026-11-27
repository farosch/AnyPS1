#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace anyps1 {

constexpr std::uint32_t PSX_HEADER_SIZE = 2048;
std::uint32_t read_psx32(const std::vector<unsigned char>& data, std::size_t offset);
std::string hex32(std::uint32_t value);
std::string escape_assembly_string(const std::string& value);
std::filesystem::path resolve_relink_input(const std::string& input);

}
