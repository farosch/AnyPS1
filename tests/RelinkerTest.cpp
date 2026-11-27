#include "relinker/Relinker.h"

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <iterator>
#include <vector>

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

        {
            std::ifstream candidate(input_path, std::ios::binary);
            std::vector<unsigned char> bytes{
                std::istreambuf_iterator<char>(candidate),
                std::istreambuf_iterator<char>()
            };
            bool malformed = bytes.size() < 2048
                || std::memcmp(bytes.data(), "PS-X EXE", 8) != 0;
            if (!malformed && bytes.size() >= 0x20) {
                const std::uint32_t declared =
                    static_cast<std::uint32_t>(bytes[0x1C])
                    | (static_cast<std::uint32_t>(bytes[0x1D]) << 8)
                    | (static_cast<std::uint32_t>(bytes[0x1E]) << 16)
                    | (static_cast<std::uint32_t>(bytes[0x1F]) << 24);
                malformed = 2048ULL + declared > bytes.size();
            }
            if (malformed) {
                anyps1::RelinkOptions malformed_options;
                malformed_options.input = game_root;
                try {
                    anyps1::relink(malformed_options);
                    throw std::runtime_error("malformed fixture unexpectedly relinked");
                } catch (const std::exception& error) {
                    if (std::string(error.what()).find("unsupported") == std::string::npos
                        && std::string(error.what()).find("truncated") == std::string::npos
                        && std::string(error.what()).find("invalid") == std::string::npos
                        && std::string(error.what()).find("shorter") == std::string::npos
                        && std::string(error.what()).find("declared size") == std::string::npos) {
                        throw;
                    }
                }
                std::filesystem::remove_all(game_root);
                return 0;
            }
        }

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
        FILE* executable = popen(output_path, "r");
        if (executable == nullptr) {
            throw std::runtime_error("cannot start native ELF artifact");
        }
        std::string terminal_output;
        std::array<char, 256> capture{};
        while (std::fgets(capture.data(), static_cast<int>(capture.size()), executable) != nullptr) {
            terminal_output += capture.data();
        }
        const int capture_status = pclose(executable);
        if (capture_status != 0) {
            throw std::runtime_error("native ELF artifact did not exit successfully");
        }
        if (terminal_output.find("AnyPS1 native executable diagnostics") == std::string::npos
            || terminal_output.find("target=linux") == std::string::npos
            || terminal_output.find("entry_point=0x") == std::string::npos
            || terminal_output.find("native lowering complete") == std::string::npos) {
            throw std::runtime_error("native ELF artifact lacks comprehensive diagnostics");
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
