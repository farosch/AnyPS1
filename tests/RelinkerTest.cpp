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
        std::cerr << "Usage: anyps1_relinker_test <fixture.ps-exe>\n";
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
            throw std::runtime_error("relink failed");
        }
        anyps1::RunOptions run_options;
        run_options.input = input_path;
        run_options.max_instructions = 10;
        if (!anyps1::run_psx(run_options)) {
            throw std::runtime_error("runtime stopped");
        }

        anyps1::RelinkOptions windows_options;
        windows_options.input = game_root;
        windows_options.target = anyps1::TargetPlatform::Windows;
        if (!anyps1::relink(windows_options)) {
            throw std::runtime_error("Windows relink failed");
        }
        std::ifstream windows_artifact(windows_output_path, std::ios::binary);
        std::array<unsigned char, 224> windows_header{};
        windows_artifact.read(reinterpret_cast<char*>(windows_header.data()), windows_header.size());
        if (windows_header[0] != 'M' || windows_header[1] != 'Z') {
            throw std::runtime_error("invalid PE DOS magic");
        }
        std::uint32_t pe_offset = static_cast<std::uint32_t>(windows_header[60])
            | (static_cast<std::uint32_t>(windows_header[61]) << 8)
            | (static_cast<std::uint32_t>(windows_header[62]) << 16)
            | (static_cast<std::uint32_t>(windows_header[63]) << 24);
        if (pe_offset != 128) {
            throw std::runtime_error("invalid PE offset");
        }
        if (windows_header[128] != 'P' || windows_header[129] != 'E') {
            throw std::runtime_error("invalid PE signature");
        }
        if (windows_header[132] != static_cast<unsigned char>(0x64)
            || windows_header[133] != static_cast<unsigned char>(0x86)) {
            throw std::runtime_error("invalid PE x86-64 machine");
        }
        if (windows_header[220] != 3 || windows_header[221] != 0) {
            throw std::runtime_error("invalid PE CUI subsystem");
        }

        std::ifstream artifact(output_path, std::ios::binary);
        std::array<unsigned char, 52> header{};
        artifact.read(reinterpret_cast<char*>(header.data()), header.size());
        if (header[0] != 0x7F || header[1] != 'E' || header[2] != 'L' || header[3] != 'F') {
            throw std::runtime_error("invalid ELF magic");
        }
        artifact.seekg(0, std::ios::end);
        const std::uint64_t artifact_size =
            static_cast<std::uint64_t>(artifact.tellg());
        constexpr std::uint64_t trailer_size = 24;
        if (artifact_size < trailer_size) {
            throw std::runtime_error("Linux native launcher trailer is missing");
        }
        artifact.seekg(static_cast<std::streamoff>(artifact_size - trailer_size));
        std::array<unsigned char, trailer_size> trailer{};
        artifact.read(reinterpret_cast<char*>(trailer.data()), trailer.size());
        const char magic[] = "ANYPS1-PSX-IMAGE";
        if (std::memcmp(trailer.data(), magic, 16) != 0) {
            throw std::runtime_error("invalid Linux native launcher trailer");
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
