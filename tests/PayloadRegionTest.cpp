#include "lowering/PayloadRegion.h"

#include <cstdint>
#include <iostream>
#include <stdexcept>

int main()
{
    try {
        if (anyps1::classify_payload_region(
                0x80010000u,
                0x625C3A63u,
                true) != anyps1::PayloadRegionKind::EmbeddedData) {
            throw std::runtime_error("ASCII payload was not classified as embedded data");
        }
        if (anyps1::classify_payload_region(
                0x80010004u,
                0x77777777u,
                true) != anyps1::PayloadRegionKind::EmbeddedData) {
            throw std::runtime_error("filler payload was not classified as embedded data");
        }
        if (anyps1::classify_payload_region(
                0x80010008u,
                0x24020001u,
                true) != anyps1::PayloadRegionKind::Instruction) {
            throw std::runtime_error("valid instruction word was not classified as instruction");
        }
        if (anyps1::classify_payload_region(
                0x8001000Cu,
                0xF8000000u,
                true) != anyps1::PayloadRegionKind::Instruction) {
            throw std::runtime_error(
                "COP3 64-bit transfer was not classified as unsupported");
        }
        if (anyps1::classify_payload_region(
                0x80010010u,
                0x5A82A57Du,
                true) != anyps1::PayloadRegionKind::Unknown) {
            throw std::runtime_error(
                "invalid BLEZL encoding was classified as code");
        }
        if (anyps1::classify_payload_region(
                0x80010014u,
                0x00000001u,
                true) != anyps1::PayloadRegionKind::Unknown) {
            throw std::runtime_error(
                "reserved SPECIAL encoding was classified as code");
        }
        if (anyps1::classify_payload_region(
                0x80010018u,
                0x03D103D7u,
                true) != anyps1::PayloadRegionKind::Unknown) {
            throw std::runtime_error(
                "invalid LUI encoding was classified as code");
        }
        if (anyps1::classify_payload_region(
                0x8001001Cu,
                0x00FFFFFFu,
                true) != anyps1::PayloadRegionKind::Unknown) {
            throw std::runtime_error(
                "reserved SPECIAL encoding was classified as code");
        }
        if (anyps1::classify_payload_region(
                0x80010020u,
                0x004236ADu,
                true) != anyps1::PayloadRegionKind::EmbeddedData) {
            throw std::runtime_error(
                "reserved SPECIAL fixture was not classified as instruction");
        }
        if (anyps1::classify_payload_region(
                0x8001F950u,
                0xFE43FE49u,
                true) != anyps1::PayloadRegionKind::Unknown) {
            throw std::runtime_error(
                "reserved opcode 0x3F table word was classified as code");
        }
        if (anyps1::classify_payload_region(
                0x80022330u,
                0x4B43414Au,
                true) != anyps1::PayloadRegionKind::EmbeddedData) {
            throw std::runtime_error(
                "printable COP2-shaped table word was classified as code");
        }
        if (anyps1::classify_payload_region(
                0x800AF510u,
                0x74736572u,
                true) != anyps1::PayloadRegionKind::EmbeddedData) {
            throw std::runtime_error(
                "ASCII payload word was classified as code");
        }
        if (anyps1::classify_payload_region(
                0x8001B194u,
                0x7043102Au,
                true) != anyps1::PayloadRegionKind::EmbeddedData) {
            throw std::runtime_error(
                "reserved table word was classified as code");
        }
        if (anyps1::classify_payload_region(
                0x800A13D4u,
                0x491C1B18u,
                true) != anyps1::PayloadRegionKind::EmbeddedData) {
            throw std::runtime_error(
                "COP2-shaped table word was classified as code");
        }
        if (anyps1::classify_payload_region(
                0x80010024u,
                0x43CF0002u,
                true) != anyps1::PayloadRegionKind::Unknown) {
            throw std::runtime_error(
                "invalid COP0 table word was classified as code");
        }
        if (anyps1::classify_payload_region(
                0x80010028u,
                0xD6640D15u,
                true) != anyps1::PayloadRegionKind::Unknown) {
            throw std::runtime_error(
                "64-bit coprocessor table word was classified as code");
        }
        if (anyps1::classify_payload_region(
                0x8001002Cu,
                0xF8000000u,
                true) != anyps1::PayloadRegionKind::Instruction) {
            throw std::runtime_error(
                "COP3 64-bit store fixture was not classified as unsupported code");
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Payload region test failed: " << error.what() << "\n";
        return 1;
    }
}
