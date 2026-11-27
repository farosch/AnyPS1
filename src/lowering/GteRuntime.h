#pragma once

#include <cstdint>

extern "C" int anyps1_gte_execute(
    std::uint32_t instruction,
    std::uint32_t* data,
    std::uint32_t* control);
