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
#ifndef _WIN32
#include <sys/wait.h>
#endif

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
            anyps1::RelinkOptions missing_target_options;
            missing_target_options.input = game_root;
            try {
                anyps1::relink(missing_target_options);
                throw std::runtime_error("relink unexpectedly accepted a missing target");
            } catch (const std::exception& error) {
                if (std::string(error.what()).find("target platform must be set explicitly") ==
                    std::string::npos) {
                    throw;
                }
            }
        }

        if (std::string(fixture_path).find("native-bios-vector.ps-exe") != std::string::npos) {
            anyps1::RelinkOptions bios_options;
            bios_options.input = game_root;
            bios_options.target = anyps1::TargetPlatform::Linux;
            if (!anyps1::relink(bios_options)) {
                throw std::runtime_error("partial BIOS-vector fixture relink failed");
            }
            std::filesystem::remove_all(game_root);
            return 0;
        }

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
                malformed_options.target = anyps1::TargetPlatform::Linux;
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

#ifndef _WIN32
        anyps1::RelinkOptions options;
        options.input = game_root;
        options.target = anyps1::TargetPlatform::Linux;
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
        const int exit_status =
            WIFEXITED(capture_status) ? WEXITSTATUS(capture_status) : -1;
        const bool partial_fixture =
            std::string(fixture_path).find("native-unsupported.ps-exe") != std::string::npos;
        if (exit_status != 0 && !(partial_fixture && exit_status == 16)) {
            throw std::runtime_error("native ELF artifact did not exit successfully");
        }
        if (terminal_output.find("AnyPS1 native executable diagnostics") == std::string::npos
            || terminal_output.find("target=linux") == std::string::npos
            || terminal_output.find("entry_point=0x") == std::string::npos
            || (std::string(fixture_path).find("native-cfg-cop0.ps-exe") == std::string::npos
                && std::string(fixture_path).find("comprehensive.ps-exe") == std::string::npos
                && std::string(fixture_path).find("native-unsupported.ps-exe") == std::string::npos
                && terminal_output.find("native lowering complete") == std::string::npos)) {
            throw std::runtime_error("native ELF artifact lacks comprehensive diagnostics");
        }
        if (partial_fixture &&
            (terminal_output.find("unsupported_instructions=") == std::string::npos ||
             terminal_output.find("first_unsupported_gap=0x") == std::string::npos)) {
            throw std::runtime_error("partial native ELF lacks unsupported lowering diagnostics");
        }
        if (terminal_output.find("lowered_coverage_percent=") == std::string::npos ||
            terminal_output.find("call_edges=") == std::string::npos ||
            terminal_output.find("unresolved_indirect_transfers=") == std::string::npos) {
            throw std::runtime_error("native ELF lacks CFG coverage diagnostics");
        }
#endif

        anyps1::RelinkOptions windows_options;
        windows_options.input = game_root;
        windows_options.target = anyps1::TargetPlatform::Windows;
        if (!anyps1::relink(windows_options)) {
            throw std::runtime_error("Windows PE relink failed");
        }
        std::ifstream pe(windows_output_path, std::ios::binary);
        std::vector<unsigned char> pe_bytes{
            std::istreambuf_iterator<char>(pe),
            std::istreambuf_iterator<char>()
        };
        if (pe_bytes.size() < 392
            || pe_bytes[0] != 'M'
            || pe_bytes[1] != 'Z') {
            throw std::runtime_error("invalid Windows PE DOS signature");
        }
        const std::uint32_t pe_offset =
            static_cast<std::uint32_t>(pe_bytes[0x3C])
            | (static_cast<std::uint32_t>(pe_bytes[0x3D]) << 8)
            | (static_cast<std::uint32_t>(pe_bytes[0x3E]) << 16)
            | (static_cast<std::uint32_t>(pe_bytes[0x3F]) << 24);
        if (pe_offset + 24 > pe_bytes.size()
            || pe_bytes[pe_offset] != 'P'
            || pe_bytes[pe_offset + 1] != 'E'
            || pe_bytes[pe_offset + 2] != '\0'
            || pe_bytes[pe_offset + 3] != '\0') {
            throw std::runtime_error("invalid Windows PE signature");
        }
        const std::uint16_t optional_magic =
            static_cast<std::uint16_t>(pe_bytes[pe_offset + 24])
            | (static_cast<std::uint16_t>(pe_bytes[pe_offset + 25]) << 8);
        if (optional_magic != 0x20B) {
            throw std::runtime_error("Windows PE output is not PE32+");
        }
        const std::uint16_t section_count =
            static_cast<std::uint16_t>(pe_bytes[pe_offset + 6])
            | (static_cast<std::uint16_t>(pe_bytes[pe_offset + 7]) << 8);
        if (section_count != 4) {
            throw std::runtime_error("Windows PE output has invalid section count");
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
