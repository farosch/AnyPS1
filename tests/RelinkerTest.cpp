#include "relinker/Relinker.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {

void write32(std::ofstream& output, std::size_t offset, std::uint32_t value)
{
    output.seekp(static_cast<std::streamoff>(offset));
    const std::array<unsigned char, 4> bytes{
        static_cast<unsigned char>(value),
        static_cast<unsigned char>(value >> 8),
        static_cast<unsigned char>(value >> 16),
        static_cast<unsigned char>(value >> 24),
    };
    output.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
}

}

int main()
{
    const char* input_path = "/tmp/anyps1-relinker-test.ps-exe";
    const char* output_path = "/tmp/anyps1-relinker-test.elf";
    const char* windows_output_path = "/tmp/anyps1-relinker-test.exe";
    try {
        std::ofstream input(input_path, std::ios::binary | std::ios::trunc);
        std::vector<unsigned char> exe(2080, 0);
        std::memcpy(exe.data(), "PS-X EXE", 8);
        exe[0x10] = 0x08;
        exe[0x11] = 0x00;
        exe[0x12] = 0x01;
        exe[0x13] = 0x80;
        exe[0x18] = 0x00;
        exe[0x19] = 0x00;
        exe[0x1A] = 0x01;
        exe[0x1B] = 0x80;
        exe[0x1C] = 0x20;
        exe[0x1D] = 0x00;
        exe[0x1E] = 0x00;
        exe[0x1F] = 0x00;
        input.write(reinterpret_cast<const char*>(exe.data()), static_cast<std::streamsize>(exe.size()));
        input.close();

        anyps1::RelinkOptions options;
        options.input = input_path;
        options.output = output_path;
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
        windows_options.input = input_path;
        windows_options.output = windows_output_path;
        windows_options.target = anyps1::TargetPlatform::Windows;
        if (!anyps1::relink(windows_options)) {
            throw std::runtime_error("Windows relink failed");
        }
        std::ifstream windows_artifact(windows_output_path, std::ios::binary);
        std::array<unsigned char, 132> windows_header{};
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

        std::ifstream artifact(output_path, std::ios::binary);
        std::array<unsigned char, 52> header{};
        artifact.read(reinterpret_cast<char*>(header.data()), header.size());
        if (header[0] != 0x7F || header[1] != 'E' || header[2] != 'L' || header[3] != 'F') {
            throw std::runtime_error("invalid ELF magic");
        }
        artifact.seekg(152);
        std::array<unsigned char, 4> shstr_size{};
        artifact.read(reinterpret_cast<char*>(shstr_size.data()), shstr_size.size());
        if (shstr_size[0] == 0 && shstr_size[1] == 0 && shstr_size[2] == 0 && shstr_size[3] == 0) {
            throw std::runtime_error("ELF section-name table is empty");
        }

        std::remove(input_path);
        std::remove(output_path);
        std::remove(windows_output_path);
        return 0;
    } catch (const std::exception& error) {
        std::remove(input_path);
        std::remove(output_path);
        std::remove(windows_output_path);
        std::cerr << "Relinker test failed: " << error.what() << "\n";
        return 1;
    }
}
