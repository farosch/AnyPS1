#pragma once

#include <cstdint>
#include <string>

namespace anyps1 {

enum class TargetPlatform {
    Linux,
    Windows
};

struct RelinkOptions {
    std::string input;
    std::string output;
    TargetPlatform target = TargetPlatform::Linux;
};

bool relink(const RelinkOptions& options);

}
