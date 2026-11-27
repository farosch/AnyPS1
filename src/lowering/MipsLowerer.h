#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace anyps1 {

struct RelinkOptions;
void write_linux_native_cfg(const RelinkOptions& options, const std::vector<unsigned char>& data);

}
