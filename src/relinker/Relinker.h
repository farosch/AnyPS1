#pragma once

#include <cstdint>
#include <optional>
#include <string>
namespace anyps1 {
enum class TargetPlatform { Linux, Windows };
struct RelinkOptions {
    std::string input;
    std::string output;
    std::optional<TargetPlatform> target;
};
bool relink(RelinkOptions& options);
}
