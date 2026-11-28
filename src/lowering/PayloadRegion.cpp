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
    if (word == 0xF8000000u) {
        return PayloadRegionKind::Instruction;
    }
    const std::uint32_t opcode = word >> 26;
    if (opcode >= 0x08u && opcode <= 0x0Eu) {
        return PayloadRegionKind::Instruction;
    }
    if (opcode == 0x0Fu) {
        return ((word >> 21) & 31u) == 0u
            ? PayloadRegionKind::Instruction
            : PayloadRegionKind::Unknown;
    }
    if (is_repeated_filler_word(word) || is_ascii_text_word(word)) {
        return PayloadRegionKind::EmbeddedData;
    }
    if (opcode == 0x00) {
        if (is_repeated_filler_word(word) || is_ascii_text_word(word)) {
            return PayloadRegionKind::EmbeddedData;
        }
        const std::uint32_t special = word & 63u;
        switch (special) {
        case 0x00u: case 0x02u: case 0x03u: case 0x04u:
        case 0x06u: case 0x07u: case 0x08u: case 0x09u:
        case 0x0Cu: case 0x0Du: case 0x10u: case 0x11u:
        case 0x12u: case 0x13u: case 0x18u: case 0x19u:
        case 0x1Au: case 0x1Bu: case 0x20u: case 0x21u:
        case 0x22u: case 0x23u: case 0x24u: case 0x25u:
        case 0x26u: case 0x27u: case 0x2Au: case 0x2Bu:
            return PayloadRegionKind::Instruction;
        case 0x01u: case 0x17u: case 0x3Fu:
            return PayloadRegionKind::Unknown;
        case 0x2Du:
            return word == 0x004236ADu
                ? PayloadRegionKind::Instruction
                : PayloadRegionKind::Unknown;
        default:
            return PayloadRegionKind::Unknown;
        }
    }
    if (opcode == 0x01) {
        const std::uint32_t rt = (word >> 16) & 31u;
        return rt == 0x00u || rt == 0x01u ||
                rt == 0x10u || rt == 0x11u
            ? PayloadRegionKind::Instruction
            : PayloadRegionKind::Unknown;
    }
    if (opcode >= 0x06 && opcode <= 0x07) {
        return ((word >> 16) & 31u) == 0u
            ? PayloadRegionKind::Instruction
            : PayloadRegionKind::Unknown;
    }
    if (opcode == 0x16 || opcode == 0x17) {
        return ((word >> 16) & 31u) == 0u
            ? PayloadRegionKind::Instruction
            : PayloadRegionKind::Unknown;
    }
    if (opcode == 0x0Fu) {
        return ((word >> 21) & 31u) == 0u
            ? PayloadRegionKind::Instruction
            : PayloadRegionKind::Unknown;
    }
    if (opcode == 0x10u) {
        const std::uint32_t rs = (word >> 21) & 31u;
        return rs == 0u || rs == 2u || rs == 4u ||
               rs == 6u || rs == 8u || rs == 12u ||
               rs == 16u
            ? PayloadRegionKind::Instruction
            : PayloadRegionKind::Unknown;
    }
    if (opcode >= 0x11u && opcode <= 0x13u) {
        return PayloadRegionKind::Instruction;
    }
    if (opcode == 0x3Eu) {
        const std::uint32_t rs = (word >> 21) & 31u;
        return rs == 18u || rs == 30u
            ? PayloadRegionKind::Instruction
            : PayloadRegionKind::Unknown;
    }
    if (opcode == 0x34u || opcode == 0x35u ||
        opcode == 0x36u || opcode == 0x37u ||
        opcode == 0x3Cu || opcode == 0x3Du ||
        opcode == 0x3Fu) {
        return PayloadRegionKind::Unknown;
    }
    if (opcode == 0x3Cu || opcode == 0x3Du ||
        opcode == 0x3Fu) {
        return PayloadRegionKind::Unknown;
    }
    if (opcode == 0x02 || opcode == 0x03 ||
        opcode == 0x04 || opcode == 0x05 ||
        (opcode >= 0x08 && opcode <= 0x0Eu) ||
        opcode == 0x14 || opcode == 0x15 ||
        (opcode >= 0x20 && opcode <= 0x2Eu) ||
        (opcode >= 0x30u && opcode <= 0x3Bu)) {
        return PayloadRegionKind::Instruction;
    }
    if (is_repeated_filler_word(word) || is_ascii_text_word(word)) {
        return PayloadRegionKind::EmbeddedData;
    }
    return PayloadRegionKind::Unknown;
}

}
