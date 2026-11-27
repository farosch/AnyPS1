#include "relinker/Relinker.h"

#include <iostream>
#include <stdexcept>
#include <string>

int main(int argc, char** argv)
{
    if (argc != 4 || std::string(argv[1]) != "--target") {
        std::cerr
            << "Usage:\n"
            << "  anyps1 --target linux <extracted-game-root>\n"
            << "  anyps1 --target windows <extracted-game-root>\n";
        return 2;
    }
    {
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
            std::cout << "Native executable written to " << options.output << "\n";
            return 0;
        } catch (const std::exception& error) {
            std::cerr << "Error: " << error.what() << "\n";
            return 1;
        }
    }
}
