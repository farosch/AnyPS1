#include "lowering/MipsLoweringSupport.h"
#include "format/PsExe.h"

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

}

int main()
{
    try {
        constexpr std::uint32_t load_address = 0x80010000u;
        constexpr std::uint32_t file_size = 64u;
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
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "MIPS lowering support test failed: "
                  << error.what() << "\n";
        return 1;
    }
}
