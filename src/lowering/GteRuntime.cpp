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
    if (instruction == 0x4B18043Fu) {
        for (std::uint32_t vertex = 0; vertex < 3; ++vertex) {
            const std::uint32_t packed = data[vertex * 2u];
            data[0] = packed;
            data[1] = data[vertex * 2u + 1u];
            multiply_rotation(data, control, 0);
            data[9 + vertex] = data[25];
            data[16 + vertex] = data[27];
        }
        return true;
    }
    if (function == 0x2D && (instruction == 0x4A58002Du ||
                             instruction == 0x4B58002Du)) {
        const std::int64_t zsum =
            static_cast<std::int64_t>(signed_word(data[16])) +
            signed_word(data[17]) +
            signed_word(data[18]);
        const std::int64_t mac0 =
            static_cast<std::int64_t>(signed_word(control[29])) * zsum;
        data[25] = static_cast<std::uint32_t>(mac0);
        data[7] = static_cast<std::uint32_t>(
            mac0 >> 12 < 0 ? 0 :
            mac0 >> 12 > 65535 ? 65535 : mac0 >> 12);
        return true;
    }
    if (function == 0x2E && (instruction == 0x4A68002Eu ||
                             instruction == 0x4B68002Eu)) {
        const std::int64_t zsum =
            static_cast<std::int64_t>(signed_word(data[16])) +
            signed_word(data[17]) +
            signed_word(data[18]) +
            signed_word(data[19]);
        const std::int64_t mac0 =
            static_cast<std::int64_t>(signed_word(control[30])) * zsum;
        data[25] = static_cast<std::uint32_t>(mac0);
        data[7] = static_cast<std::uint32_t>(
            mac0 >> 12 < 0 ? 0 :
            mac0 >> 12 > 65535 ? 65535 : mac0 >> 12);
        return true;
    }
    if (rs == 17 && function == 0x30) {
        for (std::uint32_t vertex = 0; vertex < 3; ++vertex) {
            const std::uint32_t packed = data[vertex * 2u];
            const std::int32_t vx =
                static_cast<std::int16_t>(packed & 0xFFFFu);
            const std::int32_t vy =
                static_cast<std::int16_t>((packed >> 16) & 0xFFFFu);
            const std::int32_t vz =
                static_cast<std::int16_t>(data[vertex * 2u + 1u] & 0xFFFFu);
            data[0] = static_cast<std::uint32_t>(
                static_cast<std::uint16_t>(vx) |
                (static_cast<std::uint32_t>(static_cast<std::uint16_t>(vy)) << 16));
            data[1] = static_cast<std::uint32_t>(vz);
            multiply_rotation(data, control, 0);
            const std::int64_t numerator =
                static_cast<std::int64_t>(signed_word(control[26])) << 16;
            const std::int32_t depth = signed_word(data[18]);
            const std::int64_t factor =
                depth == 0 ? numerator : numerator / depth;
            data[12 + vertex] = static_cast<std::uint32_t>(
                clamp32((signed_word(data[9]) * factor) >> 16));
            data[16 + vertex] = static_cast<std::uint32_t>(
                clamp32(signed_word(data[27])));
        }
        return true;
    }
    if (rs == 24 && function == 0x1B) {
        multiply_rotation(data, control, 0);
        const std::int64_t r =
            static_cast<std::int64_t>(signed_word(data[25])) +
            signed_word(control[24]);
        const std::int64_t g =
            static_cast<std::int64_t>(signed_word(data[26])) +
            signed_word(control[25]);
        const std::int64_t b =
            static_cast<std::int64_t>(signed_word(data[27])) +
            signed_word(control[26]);
        data[9] = static_cast<std::uint32_t>(
            r < 0 ? 0 : r > 32767 ? 32767 : r);
        data[10] = static_cast<std::uint32_t>(
            g < 0 ? 0 : g > 32767 ? 32767 : g);
        data[11] = static_cast<std::uint32_t>(
            b < 0 ? 0 : b > 32767 ? 32767 : b);
        data[22] = data[9];
        data[23] = data[10];
        data[24] = data[11];
        return true;
    }
    if (function == 0x06 || function == 0x13 ||
        function == 0x14 || function == 0x16 ||
        function == 0x1C || function == 0x1E ||
        function == 0x20 || function == 0x29 ||
        function == 0x2A || function == 0x3D ||
        function == 0x3E) {
        multiply_rotation(data, control, 0);
        if (function == 0x29) {
            const std::int32_t red = signed_word(data[16]);
            const std::int32_t green = signed_word(data[17]);
            const std::int32_t blue = signed_word(data[18]);
            data[9] = static_cast<std::uint32_t>(red < 0 ? 0 : red > 32767 ? 32767 : red);
            data[10] = static_cast<std::uint32_t>(green < 0 ? 0 : green > 32767 ? 32767 : green);
            data[11] = static_cast<std::uint32_t>(blue < 0 ? 0 : blue > 32767 ? 32767 : blue);
        } else if (function == 0x2A) {
            data[22] = data[16];
            data[23] = data[17];
            data[24] = data[18];
        } else if (function == 0x3D || function == 0x3E) {
            data[9] = data[25];
            data[10] = data[26];
            data[11] = data[27];
        } else {
            set_ir_mac(data);
        }
        return true;
    }
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
