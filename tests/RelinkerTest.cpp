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
        return 0;
    } catch (const std::exception& error) {
        std::remove(input_path);
        std::remove(output_path);
        std::cerr << "Relinker test failed: " << error.what() << "\n";
        return 1;
    }
}
