#include "relinker/Relinker.h"

#include <iostream>
#include <stdexcept>
#include <string>

int main(int argc, char** argv)
{
    if (argc == 4 && std::string(argv[1]) == "--run") {
        anyps1::RunOptions options;
        options.input = argv[2];
        options.max_instructions = std::stoull(argv[3]);
        try {
            if (!anyps1::run_psx(options)) {
                return 1;
            }
            return 0;
        } catch (const std::exception& error) {
            std::cerr << "Error: " << error.what() << "\n";
            return 1;
        }
    }
    if (argc != 3) {
        std::cerr
            << "Usage:\n"
            << "  anyps1 <input.ps-exe> <output.elf>\n"
            << "  anyps1 --run <input.ps-exe> <instruction-limit>\n";
        return 2;
    }

    anyps1::RelinkOptions options;
    options.input = argv[1];
    options.output = argv[2];

    try {
        if (!anyps1::relink(options)) {
            return 1;
        }
        std::cout << "PS-X EXE layout artifact written to " << options.output << "\n";
    } catch (const std::exception& error) {
        std::cerr << "Error: " << error.what() << "\n";
        return 1;
    }

    return 0;
}
