#include "relinker/Relinker.h"
#include "runtime/Runtime.h"

#include <array>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <unistd.h>

namespace {

constexpr char kNativePsxMagic[] = "ANYPS1-PSX-IMAGE";

bool run_embedded_psx()
{
    char executable[4096]{};
    const ssize_t length = readlink("/proc/self/exe", executable, sizeof(executable) - 1);
    if (length <= 0) {
        return false;
    }
    executable[length] = '\0';

    std::ifstream input(executable, std::ios::binary | std::ios::ate);
    if (!input) {
        return false;
    }
    const std::uint64_t file_size = static_cast<std::uint64_t>(input.tellg());
    constexpr std::uint64_t trailer_size = 24;
    if (file_size < trailer_size) {
        return false;
    }
    input.seekg(static_cast<std::streamoff>(file_size - trailer_size), std::ios::beg);
    std::array<char, 24> trailer{};
    input.read(trailer.data(), static_cast<std::streamsize>(trailer.size()));
    if (std::memcmp(trailer.data(), kNativePsxMagic, 16) != 0) {
        return false;
    }
    std::uint64_t psx_size = 0;
    for (std::uint32_t index = 0; index < 8; ++index) {
        psx_size |= static_cast<std::uint64_t>(
            static_cast<unsigned char>(trailer[16 + index])) << (index * 8);
    }
    if (psx_size == 0 || psx_size > file_size - trailer_size) {
        throw std::runtime_error("invalid embedded PS-X payload");
    }
    input.seekg(static_cast<std::streamoff>(file_size - trailer_size - psx_size), std::ios::beg);
    std::string psx(psx_size, '\0');
    input.read(psx.data(), static_cast<std::streamsize>(psx.size()));
    if (!input) {
        throw std::runtime_error("cannot read embedded PS-X payload");
    }

    const std::filesystem::path temp =
        std::filesystem::temp_directory_path()
        / ("anyps1-native-" + std::to_string(static_cast<unsigned long long>(getpid())) + ".ps-exe");
    {
        std::ofstream output(temp, std::ios::binary | std::ios::trunc);
        output.write(psx.data(), static_cast<std::streamsize>(psx.size()));
        if (!output) {
            throw std::runtime_error("cannot create native launcher temporary PS-X image");
        }
    }
    anyps1::RunOptions options;
    options.input = temp.string();
    options.video = true;
    options.max_instructions = 300000000;
    const bool result = anyps1::run_psx(options);
    std::filesystem::remove(temp);
    return result;
}

}

int main(int argc, char** argv)
{
    if (argc == 1) {
        try {
            if (run_embedded_psx()) {
                return 0;
            }
        } catch (const std::exception& error) {
            std::cerr << "Error: " << error.what() << "\n";
            return 1;
        }
    }
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
            std::cout << "Native PS-X launcher written to " << options.output << "\n";
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
