#include "relinker/Relinker.h"
#include "runtime/Runtime.h"

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

int main(int argc, char** argv)
{
    if (argc != 2) {
        std::cerr << "Usage: anyps1_relinker_test <generated-fixture.ps-exe>\n";
        return 1;
    }
    const char* fixture_path = argv[1];
    const char* game_root = "/tmp/anyps1-relinker-test";
    const char* input_path = "/tmp/anyps1-relinker-test/SCUS_941.63";
    const char* output_path = "/tmp/anyps1-relinker-test/game.elf";
    const char* windows_output_path = "/tmp/anyps1-relinker-test/game.exe";
    try {
        if (!std::filesystem::is_regular_file(fixture_path)) {
            throw std::runtime_error("missing relinker fixture");
        }
        std::filesystem::remove_all(game_root);
        std::filesystem::create_directories(game_root);
        std::filesystem::copy_file(fixture_path, input_path);
        std::ofstream system_cnf(std::filesystem::path(game_root) / "SYSTEM.CNF");
        system_cnf << "BOOT=cdrom:\\SCUS_941.63;1\n";
        system_cnf.close();

        anyps1::RelinkOptions options;
        options.input = game_root;
        if (!anyps1::relink(options)) {
            throw std::runtime_error("native relink failed");
        }
        std::ifstream artifact(output_path, std::ios::binary);
        std::array<unsigned char, 4> elf_magic{};
        artifact.read(reinterpret_cast<char*>(elf_magic.data()), elf_magic.size());
        if (elf_magic[0] != 0x7F || elf_magic[1] != 'E'
            || elf_magic[2] != 'L' || elf_magic[3] != 'F') {
            throw std::runtime_error("invalid native ELF output");
        }
        if (std::system(output_path) != 0) {
            throw std::runtime_error("native ELF artifact did not exit successfully");
        }

        anyps1::RelinkOptions windows_options;
        windows_options.input = game_root;
        windows_options.target = anyps1::TargetPlatform::Windows;
        try {
            anyps1::relink(windows_options);
            throw std::runtime_error("Windows native relinker unexpectedly succeeded");
        } catch (const std::exception& error) {
            if (std::string(error.what()).find("native Windows MIPS lowering is not implemented") == std::string::npos) {
                throw;
            }
        }

        std::remove(output_path);
        std::remove(windows_output_path);
        std::filesystem::remove_all("/tmp/anyps1-relinker-test");
        return 0;
    } catch (const std::exception& error) {
        std::remove(output_path);
        std::remove(windows_output_path);
        std::filesystem::remove_all("/tmp/anyps1-relinker-test");
        std::cerr << "Relinker test failed: " << error.what() << "\n";
        return 1;
    }
}
