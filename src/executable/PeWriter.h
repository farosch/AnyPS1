#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace anyps1 {
struct RelinkOptions;
struct NativeLoweringIr;
void write_pe_ir(
    const RelinkOptions& options,
    const NativeLoweringIr& ir);
}
