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

struct RunOptions {
    std::string input;
    std::uint64_t max_instructions = 1000000;
    bool video = false;
};

bool run_psx(const RunOptions& options);

}
