#include "MipsLowerer.h"

#include "MipsLinuxLowerer.h"
#include "MipsWindowsLowerer.h"
#include "relinker/Relinker.h"

#include <memory>
#include <stdexcept>

namespace anyps1 {

std::unique_ptr<MipsLowerer> create_mips_lowerer(TargetPlatform target)
{
    if (target == TargetPlatform::Linux) {
        return std::make_unique<MipsLinuxLowerer>();
    }
    if (target == TargetPlatform::Windows) {
        return std::make_unique<MipsWindowsLowerer>();
    }
    throw std::invalid_argument("unsupported native MIPS target platform");
}

}
