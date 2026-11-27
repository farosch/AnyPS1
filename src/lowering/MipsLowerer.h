#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace anyps1 {

struct RelinkOptions;
enum class TargetPlatform;

class MipsLowerer {
public:
    virtual ~MipsLowerer() = default;
    virtual void lower(
        const RelinkOptions& options,
        const std::vector<unsigned char>& data) = 0;
};

std::unique_ptr<MipsLowerer> create_mips_lowerer(TargetPlatform target);

}
