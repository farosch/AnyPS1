#pragma once

#include "MipsLowerer.h"

namespace anyps1 {

class MipsLinuxLowerer final : public MipsLowerer {
public:
    void lower(
        const RelinkOptions& options,
        const std::vector<unsigned char>& data) override;
};

}
