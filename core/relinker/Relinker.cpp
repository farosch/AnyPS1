#include "Relinker.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>

namespace anyps1 {

namespace {

constexpr std::uint32_t PSX_HEADER_SIZE = 2048;

void write32(std::ofstream& output, std::uint32_t value)
{
    const std::array<unsigned char, 4> bytes{
        static_cast<unsigned char>(value),
        static_cast<unsigned char>(value >> 8),
        static_cast<unsigned char>(value >> 16),
        static_cast<unsigned char>(value >> 24),
    };
    output.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
}

void write16(std::ofstream& output, std::uint16_t value)
{
    const std::array<unsigned char, 2> bytes{
        static_cast<unsigned char>(value),
        static_cast<unsigned char>(value >> 8),
    };
    output.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
}

std::uint32_t read32(const std::vector<unsigned char>& data, std::size_t offset)
{
    if (offset + 4 > data.size()) {
        throw std::runtime_error("truncated PS-X EXE header");
    }
    return static_cast<std::uint32_t>(data[offset])
        | (static_cast<std::uint32_t>(data[offset + 1]) << 8)
        | (static_cast<std::uint32_t>(data[offset + 2]) << 16)
        | (static_cast<std::uint32_t>(data[offset + 3]) << 24);
}

}

bool relink(const RelinkOptions& options)
{
    if (options.input.empty()) {
        throw std::invalid_argument("input path is empty");
    }
    if (options.output.empty()) {
        throw std::invalid_argument("output path is empty");
    }

    std::ifstream input(options.input, std::ios::binary);
    if (!input) {
        throw std::runtime_error("cannot open input PS-X EXE: " + options.input);
    }
    const std::vector<unsigned char> data{
        std::istreambuf_iterator<char>(input),
        std::istreambuf_iterator<char>()
    };
    if (data.size() < PSX_HEADER_SIZE) {
        throw std::runtime_error("PS-X EXE is shorter than its header");
    }
    if (data.size() < 8 || std::memcmp(data.data(), "PS-X EXE", 8) != 0) {
        throw std::runtime_error("input is not a PS-X EXE");
    }

    const std::uint32_t initial_pc = read32(data, 0x10);
    const std::uint32_t initial_gp = read32(data, 0x14);
    const std::uint32_t load_address = read32(data, 0x18);
    const std::uint32_t file_size = read32(data, 0x1C);
    const std::uint64_t code_end = static_cast<std::uint64_t>(PSX_HEADER_SIZE) + file_size;
    if (code_end > data.size()) {
        throw std::runtime_error("PS-X EXE declared size exceeds available input");
    }
    if (load_address < 0x80000000 || load_address >= 0x80200000) {
        throw std::runtime_error("PS-X EXE load address is outside PS1 KSEG0");
    }
    if (initial_pc != 0 && (initial_pc < load_address || initial_pc >= load_address + file_size)) {
        throw std::runtime_error("PS-X EXE entry point is outside the executable image");
    }

    std::string section_names;
    section_names.push_back('\0');
    section_names += ".text.psx";
    section_names.push_back('\0');
    section_names += "shstrtab";
    section_names.push_back('\0');
    const std::uint32_t text_name_offset = 1;
    const std::uint32_t shstr_name_offset = 11;
    const std::uint32_t elf_header_size = 52;
    const std::uint32_t section_header_size = 40;
    const std::uint32_t section_count = 3;
    const std::uint32_t section_header_offset = elf_header_size;
    const std::uint32_t text_offset = (section_header_offset + section_header_size * section_count + 15) & ~15u;
    const std::uint32_t shstr_offset = text_offset + file_size;

    std::ofstream output(options.output, std::ios::binary | std::ios::trunc);
    if (!output) {
        throw std::runtime_error("cannot open output artifact: " + options.output);
    }

    const unsigned char elf_ident[16]{
        0x7F, 'E', 'L', 'F',
        1,
        1,
        1,
        0,
        0, 0, 0, 0, 0, 0, 0, 0,
    };
    output.write(reinterpret_cast<const char*>(elf_ident), sizeof(elf_ident));
    write16(output, 1);
    write16(output, 8);
    write32(output, 1);
    write32(output, 0);
    write32(output, 0);
    write32(output, section_header_offset);
    write32(output, 0);
    write16(output, elf_header_size);
    write16(output, 0);
    write16(output, 0);
    write16(output, section_header_size);
    write16(output, section_count);
    write16(output, 2);

    write32(output, 0);
    write32(output, 0);
    write32(output, 0);
    write32(output, 0);
    write32(output, 0);
    write32(output, 0);
    write32(output, 0);
    write32(output, 0);
    write32(output, 0);
    write32(output, 0);

    write32(output, text_name_offset);
    write32(output, 1);
    write32(output, 6);
    write32(output, load_address);
    write32(output, text_offset);
    write32(output, file_size);
    write32(output, 0);
    write32(output, 0);
    write32(output, 4);
    write32(output, 0);

    write32(output, shstr_name_offset);
    write32(output, 3);
    write32(output, 0);
    write32(output, 0);
    write32(output, shstr_offset);
    write32(output, section_names.size());
    write32(output, 0);
    write32(output, 0);
    write32(output, 1);
    write32(output, 0);

    while (output.tellp() >= 0 && static_cast<std::uint32_t>(output.tellp()) < text_offset) {
        output.put('\0');
    }
    output.write(
        reinterpret_cast<const char*>(data.data() + PSX_HEADER_SIZE),
        static_cast<std::streamsize>(file_size)
    );
    output.write(section_names.data(), static_cast<std::streamsize>(section_names.size()));

    if (!output) {
        throw std::runtime_error("failed while writing relinker artifact");
    }
    return true;
}

}
