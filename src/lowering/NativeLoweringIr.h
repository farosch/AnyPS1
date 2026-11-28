#pragma once

#include <cstdint>
#include <string>

#include "relinker/Relinker.h"

namespace anyps1 {

struct NativeLoweringIr {
    TargetPlatform target = TargetPlatform::Linux;
    std::string entry_symbol = "main";
    std::string source;
    std::string diagnostics;
    std::uint32_t exit_status = 0;
};

}
