#pragma once

#include <cstdint>
#include <optional>
#include <string>
namespace anyps1 {
enum class TargetPlatform { Linux, Windows };
enum class TargetArchitecture { X64, ELF32, ARM64 };
struct RelinkOptions {
    std::string input;
    std::string output;
    std::optional<TargetPlatform> target;
    TargetArchitecture architecture = TargetArchitecture::X64;
};
bool relink(RelinkOptions& options);
}
