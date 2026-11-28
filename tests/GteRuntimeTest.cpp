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
    data[0] = 0x00010001u;
    data[1] = 0x00000001u;
    data[2] = 0x00020002u;
    data[3] = 0x00000002u;
    data[4] = 0x00030003u;
    data[5] = 0x00000003u;
    control[0] = 0x1000u;
    control[4] = 0x1000u;
    control[8] = 0x1000u;
    control[26] = 100u;
    if (anyps1_gte_execute(0x4A280030u, data, control) != 1) {
        throw std::runtime_error("RTPT operation failed");
    }
    data[0] = 0x00010001u;
    data[1] = 0x00000001u;
    control[24] = 0x100u;
    control[25] = 0x100u;
    control[26] = 0x100u;
    if (anyps1_gte_execute(0x4B08041Bu, data, control) != 1 ||
        data[9] == 0u || data[10] == 0u || data[11] == 0u) {
        throw std::runtime_error("NCCS operation failed");
    }
    data[16] = 100u;
    data[17] = 200u;
    data[18] = 300u;
    control[29] = 0x1000u;
    if (anyps1_gte_execute(0x4B58002Du, data, control) != 1 ||
        data[7] != 600u) {
        throw std::runtime_error("AVSZ3 operation failed");
    }
    data[16] = 100u;
    data[17] = 200u;
    data[18] = 300u;
    data[19] = 400u;
    control[30] = 0x1000u;
    if (anyps1_gte_execute(0x4B68002Eu, data, control) != 1 ||
        data[7] != 1000u) {
        throw std::runtime_error("AVSZ4 operation failed");
    }
    if (anyps1_gte_execute(0x4B18043Fu, data, control) != 1) {
        throw std::runtime_error("NCCT operation failed");
    }
    if (anyps1_gte_execute(0xFFFFFFFFu, data, control) != 0) {
        throw std::runtime_error("unknown GTE operation was accepted");
    }
    return 0;
}
