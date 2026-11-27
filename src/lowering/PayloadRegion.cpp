#include "PayloadRegion.h"

#include <cstddef>

namespace anyps1 {

namespace {

bool is_ascii_text_word(std::uint32_t word)
{
    std::size_t printable = 0;
    for (std::size_t index = 0; index < 4; ++index) {
        const unsigned char byte =
            static_cast<unsigned char>((word >> (index * 8)) & 0xFFu);
        if (byte >= 0x20u && byte <= 0x7Eu) {
            ++printable;
        }
    }
    return printable >= 3;
}

bool is_repeated_filler_word(std::uint32_t word)
{
    const unsigned char byte0 =
        static_cast<unsigned char>(word & 0xFFu);
    const unsigned char byte1 =
        static_cast<unsigned char>((word >> 8) & 0xFFu);
    const unsigned char byte2 =
        static_cast<unsigned char>((word >> 16) & 0xFFu);
    const unsigned char byte3 =
        static_cast<unsigned char>((word >> 24) & 0xFFu);
    return (byte0 == byte1 && byte1 == byte2 && byte2 == byte3)
        || word == 0x77777777u
        || word == 0x77770700u;
}

}

PayloadRegionKind classify_payload_region(
    std::uint32_t address,
    std::uint32_t word,
    bool reached_by_cfg)
{
    (void)address;
    if (!reached_by_cfg) return PayloadRegionKind::EmbeddedData;
    if (word == 0) return PayloadRegionKind::Instruction;
    if (is_repeated_filler_word(word) || is_ascii_text_word(word)) {
        return PayloadRegionKind::EmbeddedData;
    }

    const std::uint32_t opcode = word >> 26;
    if (opcode == 0x00 || opcode == 0x01 ||
        opcode == 0x02 || opcode == 0x03 ||
        (opcode >= 0x04 && opcode <= 0x0F) ||
        (opcode >= 0x20 && opcode <= 0x2E)) {
        return PayloadRegionKind::Instruction;
    }
    return PayloadRegionKind::Unknown;
}

}
