#include "relinker/Relinker.h"

#include <iostream>
#include <stdexcept>

int main(int argc, char** argv)
{
    if (argc != 3) {
        std::cerr << "Usage: anyps1 <input.ps-exe> <output>\n";
        return 2;
    }

    anyps1::RelinkOptions options;
    options.input = argv[1];
    options.output = argv[2];

    try {
        anyps1::relink(options);
    } catch (const std::exception& error) {
        std::cerr << "Error: " << error.what() << "\n";
        return 1;
    }

    return 0;
}
