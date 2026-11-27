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
                true) != anyps1::PayloadRegionKind::Unknown) {
            throw std::runtime_error("reserved word was not classified as unknown");
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Payload region test failed: " << error.what() << "\n";
        return 1;
    }
}
