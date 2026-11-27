#pragma once

#include <cstdint>

namespace anyps1 {

enum class PayloadRegionKind {
    Instruction,
    EmbeddedData,
    Unknown
};

PayloadRegionKind classify_payload_region(
    std::uint32_t address,
    std::uint32_t word,
    bool reached_by_cfg);

}
