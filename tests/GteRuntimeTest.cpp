#include "lowering/GteRuntime.h"

#include <cstdint>
#include <iostream>
#include <stdexcept>

int main()
{
    std::uint32_t data[32]{};
    std::uint32_t control[32]{};
    control[0] = 0x1000u;
    control[4] = 0x1000u;
    control[8] = 0x1000u;
    data[0] = 0x00020001u;
    data[1] = 3u;
    if (anyps1_gte_execute(0x4A180001u, data, control) != 1 ||
        data[9] != 1u ||
        data[10] != 2u ||
        data[11] != 3u) {
        throw std::runtime_error("RTPS transform failed");
    }
    data[12] = 0x00000000u;
    data[13] = 0x00020001u;
    data[14] = 0x00040003u;
    if (anyps1_gte_execute(0x4A480012u, data, control) != 1) {
        throw std::runtime_error("NCLIP operation failed");
    }
    if (anyps1_gte_execute(0xFFFFFFFFu, data, control) != 0) {
        throw std::runtime_error("unknown GTE operation was accepted");
    }
    return 0;
}
