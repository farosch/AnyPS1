#include "PeWriter.h"
#include "relinker/Relinker.h"

#include <stdexcept>
namespace anyps1 {
void write_pe_layout(const RelinkOptions&, const std::vector<unsigned char>&, std::uint32_t, std::uint32_t)
{
    throw std::runtime_error("native Windows MIPS lowering is not implemented");
}
}
