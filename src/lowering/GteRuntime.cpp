#include "GteRuntime.h"

#include <cstdint>
#include <limits>

extern "C" void _RTC_InitBase() {}
extern "C" void _RTC_Shutdown() {}

namespace {

std::int64_t fixed_multiply(std::int32_t left, std::int32_t right)
{
    return (static_cast<std::int64_t>(left) * right) >> 12;
}

std::int32_t clamp32(std::int64_t value)
{
    if (value > std::numeric_limits<std::int32_t>::max()) {
        return std::numeric_limits<std::int32_t>::max();
    }
    if (value < std::numeric_limits<std::int32_t>::min()) {
        return std::numeric_limits<std::int32_t>::min();
    }
    return static_cast<std::int32_t>(value);
}

std::int32_t signed_word(std::uint32_t value)
{
    return static_cast<std::int32_t>(value);
}

void set_ir_mac(std::uint32_t* data)
{
    for (std::uint32_t index = 0; index < 3; ++index) {
        const std::int32_t value = clamp32(
            signed_word(data[25 + index]));
        data[25 + index] = static_cast<std::uint32_t>(value);
        data[9 + index] = static_cast<std::uint32_t>(
            value < -32768 ? -32768 :
            value > 32767 ? 32767 : value);
    }
}

void multiply_rotation(
    std::uint32_t* data,
    std::uint32_t* control,
    std::int32_t shift)
{
    const std::int32_t vx = signed_word(data[0]) & 0xFFFF;
    const std::int32_t vy = signed_word(data[0]) >> 16;
    const std::int32_t vz = signed_word(data[1]);
    const std::int64_t x =
        fixed_multiply(signed_word(control[0]), vx) +
        fixed_multiply(signed_word(control[1]), vy) +
        fixed_multiply(signed_word(control[2]), vz);
    const std::int64_t y =
        fixed_multiply(signed_word(control[3]), vx) +
        fixed_multiply(signed_word(control[4]), vy) +
        fixed_multiply(signed_word(control[5]), vz);
    const std::int64_t z =
        fixed_multiply(signed_word(control[6]), vx) +
        fixed_multiply(signed_word(control[7]), vy) +
        fixed_multiply(signed_word(control[8]), vz);
    data[25] = static_cast<std::uint32_t>(x >> shift);
    data[26] = static_cast<std::uint32_t>(y >> shift);
    data[27] = static_cast<std::uint32_t>(z >> shift);
    set_ir_mac(data);
}

bool execute_known_gte(
    std::uint32_t instruction,
    std::uint32_t* data,
    std::uint32_t* control)
{
    const std::uint32_t rs = (instruction >> 21) & 31u;
    const std::uint32_t function = instruction & 63u;
    if (rs == 16 && function == 1) {
        multiply_rotation(data, control, 0);
        const std::int64_t numerator =
            static_cast<std::int64_t>(signed_word(control[26])) << 16;
        const std::int32_t depth = signed_word(data[18]);
        const std::int64_t factor =
            depth == 0 ? numerator : numerator / depth;
        data[12] = static_cast<std::uint32_t>(
            clamp32((signed_word(data[9]) * factor) >> 16));
        data[13] = static_cast<std::uint32_t>(
            clamp32((signed_word(data[10]) * factor) >> 16));
        data[16] = data[17];
        data[17] = data[18];
        data[18] = data[12];
        return true;
    }
    if (rs == 18 && function == 0x12) {
        const std::int64_t sx0 = signed_word(data[12]) & 0xFFFF;
        const std::int64_t sy0 = signed_word(data[12]) >> 16;
        const std::int64_t sx1 = signed_word(data[13]) & 0xFFFF;
        const std::int64_t sy1 = signed_word(data[13]) >> 16;
        const std::int64_t sx2 = signed_word(data[14]) & 0xFFFF;
        const std::int64_t sy2 = signed_word(data[14]) >> 16;
        data[24] = static_cast<std::uint32_t>(
            (sx0 * (sy1 - sy2)) +
            (sx1 * (sy2 - sy0)) +
            (sx2 * (sy0 - sy1)));
        return true;
    }
    if (function == 0x10 || function == 0x11 ||
        function == 0x06 || function == 0x0C ||
        function == 0x1C || function == 0x28 ||
        function == 0x3D || function == 0x3E) {
        multiply_rotation(data, control, 0);
        if (function == 0x28) {
            data[24] = static_cast<std::uint32_t>(
                (signed_word(data[16]) +
                 signed_word(data[17]) +
                 signed_word(data[18]) +
                 signed_word(data[19])) / 4);
        }
        return true;
    }
    return false;
}

}

extern "C" int anyps1_gte_execute(
    std::uint32_t instruction,
    std::uint32_t* data,
    std::uint32_t* control)
{
    if (data == nullptr || control == nullptr) return 0;
    return execute_known_gte(instruction, data, control) ? 1 : 0;
}
