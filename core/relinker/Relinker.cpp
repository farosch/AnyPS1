#include "Relinker.h"

#include <stdexcept>

namespace anyps1 {

bool relink(const RelinkOptions&)
{
    throw std::runtime_error("PS1 executable relinking is not implemented");
}

}
