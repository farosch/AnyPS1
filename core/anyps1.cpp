#include "relinker/Relinker.h"

#include <iostream>
#include <stdexcept>
#include <string>

int main(int argc, char** argv)
{
    if (argc == 5 && std::string(argv[1]) == "--run-card") {
        anyps1::RunOptions options;
        options.input = argv[2];
        options.memory_card = argv[3];
        options.max_instructions = std::stoull(argv[4]);
        try {
            if (!anyps1::run_psx(options)) return 1;
            return 0;
        } catch (const std::exception& error) {
            std::cerr << "Error: " << error.what() << "\n";
            return 1;
        }
    }
    if (argc == 5 && std::string(argv[1]) == "--run-disc") {
        anyps1::RunOptions options;
        options.input = argv[2];
        options.cdrom_bin = argv[3];
        options.max_instructions = std::stoull(argv[4]);
        try {
            if (!anyps1::run_psx(options)) return 1;
            return 0;
        } catch (const std::exception& error) {
            std::cerr << "Error: " << error.what() << "\n";
            return 1;
        }
    }
    if (argc == 4 && std::string(argv[1]) == "--run-video") {
        anyps1::RunOptions options;
        options.input = argv[2];
        options.max_instructions = std::stoull(argv[3]);
        options.video = true;
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
    if (argc == 4 && std::string(argv[1]) == "--target") {
        anyps1::TargetPlatform target;
        const std::string target_name = argv[2];
        if (target_name == "linux") {
            target = anyps1::TargetPlatform::Linux;
        } else if (target_name == "windows") {
            target = anyps1::TargetPlatform::Windows;
        } else {
            std::cerr << "Error: --target must be linux or windows\n";
            return 2;
        }
        anyps1::RelinkOptions options;
        options.input = argv[3];
        options.target = target;
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
    if (argc == 2) {
        anyps1::RelinkOptions options;
        options.input = argv[1];
        try {
            if (!anyps1::relink(options)) {
                return 1;
            }
            return 0;
        } catch (const std::exception& error) {
            std::cerr << "Error: " << error.what() << "\n";
            return 1;
        }
    }
    if (argc != 2) {
        std::cerr
            << "Usage:\n"
            << "  anyps1 <extracted-game-root>\n"
            << "  anyps1 --target linux <extracted-game-root>\n"
            << "  anyps1 --target windows <extracted-game-root>\n"
            << "  anyps1 --run <input.ps-exe> <instruction-limit>\n"
            << "  anyps1 --run-card <input.ps-exe> <memory-card.bin> <instruction-limit>\n"
            << "  anyps1 --run-video <input.ps-exe> <instruction-limit>\n";
        return 2;
    }

    return 2;
}
