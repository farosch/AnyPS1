#include "PeWriter.h"
#include "format/PsExe.h"
#include "relinker/Relinker.h"

#include <cstring>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace anyps1 {

namespace {

constexpr std::uint32_t DOS_HEADER_SIZE = 128;
constexpr std::uint32_t PE_SIGNATURE_OFFSET = DOS_HEADER_SIZE;
constexpr std::uint32_t COFF_OFFSET = PE_SIGNATURE_OFFSET + 4;
constexpr std::uint32_t OPTIONAL_OFFSET = COFF_OFFSET + 20;
constexpr std::uint32_t SECTION_TABLE_OFFSET = OPTIONAL_OFFSET + 240;
constexpr std::uint32_t RAW_HEADERS_SIZE = 1024;
constexpr std::uint32_t SECTION_ALIGNMENT = 0x1000;
constexpr std::uint32_t FILE_ALIGNMENT = 0x200;
constexpr std::uint32_t TEXT_RVA = 0x1000;
constexpr std::uint32_t RDATA_RVA = 0x2000;
constexpr std::uint32_t IMAGE_SIZE = 0x205000;
constexpr std::uint64_t IMAGE_BASE = 0x140000000u;

constexpr std::uint32_t IMPORT_DESCRIPTOR_RVA = RDATA_RVA + 0x120;
constexpr std::uint32_t IMPORT_LOOKUP_RVA = RDATA_RVA + 0x80;
constexpr std::uint32_t IMPORT_DLL_NAME_RVA = RDATA_RVA + 0xC0;
constexpr std::uint32_t IMPORT_IAT_RVA = RDATA_RVA + 0xA0;
constexpr std::uint32_t DIAGNOSTIC_RVA = RDATA_RVA + 0x180;
constexpr std::uint32_t GET_STD_HANDLE_RVA = RDATA_RVA + 0x140;
constexpr std::uint32_t WRITE_FILE_RVA = RDATA_RVA + 0x150;
constexpr std::uint32_t EXIT_PROCESS_RVA = RDATA_RVA + 0x160;
constexpr std::uint32_t TEXT_RAW_OFFSET = RAW_HEADERS_SIZE;
constexpr std::uint32_t RDATA_RAW_OFFSET = 0x800;
constexpr std::uint32_t DATA_RAW_OFFSET = 0xA00;
constexpr std::uint32_t RELOC_RAW_OFFSET = 0xC00;

void append_u32(std::vector<unsigned char>& output, std::uint32_t value)
{
    output.push_back(static_cast<unsigned char>(value));
    output.push_back(static_cast<unsigned char>(value >> 8));
    output.push_back(static_cast<unsigned char>(value >> 16));
    output.push_back(static_cast<unsigned char>(value >> 24));
}

void write_u16(std::vector<unsigned char>& output, std::size_t offset, std::uint16_t value)
{
    output[offset] = static_cast<unsigned char>(value);
    output[offset + 1] = static_cast<unsigned char>(value >> 8);
}

void write_u32(std::vector<unsigned char>& output, std::size_t offset, std::uint32_t value)
{
    output[offset] = static_cast<unsigned char>(value);
    output[offset + 1] = static_cast<unsigned char>(value >> 8);
    output[offset + 2] = static_cast<unsigned char>(value >> 16);
    output[offset + 3] = static_cast<unsigned char>(value >> 24);
}

void write_u64(std::vector<unsigned char>& output, std::size_t offset, std::uint64_t value)
{
    write_u32(output, offset, static_cast<std::uint32_t>(value));
    write_u32(output, offset + 4, static_cast<std::uint32_t>(value >> 32));
}

void append_rip_relative(std::vector<unsigned char>& code,
                         std::uint32_t instruction_offset,
                         std::uint32_t instruction_length,
                         std::uint32_t target_rva)
{
    const std::uint64_t instruction_address = IMAGE_BASE + TEXT_RVA + instruction_offset;
    const std::uint64_t target_address = IMAGE_BASE + target_rva;
    const std::int64_t displacement =
        static_cast<std::int64_t>(target_address - instruction_address - instruction_length);
    if (displacement < INT32_MIN || displacement > INT32_MAX) {
        throw std::runtime_error("PE RIP-relative displacement exceeds 32-bit range");
    }
    write_u32(code, instruction_offset + instruction_length - 4,
              static_cast<std::uint32_t>(static_cast<std::int32_t>(displacement)));
}

void write_section_header(
    std::vector<unsigned char>& pe,
    std::uint32_t offset,
    const char* name,
    std::uint32_t virtual_size,
    std::uint32_t virtual_address,
    std::uint32_t raw_size,
    std::uint32_t raw_offset,
    std::uint32_t characteristics)
{
    std::memcpy(pe.data() + offset, name,
                std::char_traits<char>::length(name));
    write_u32(pe, offset + 8, virtual_size);
    write_u32(pe, offset + 12, virtual_address);
    write_u32(pe, offset + 16, raw_size);
    write_u32(pe, offset + 20, raw_offset);
    write_u32(pe, offset + 36, characteristics);
}

}

void write_pe_layout(const RelinkOptions& options,
                     const std::vector<unsigned char>& data,
                     std::uint32_t load_address,
                     std::uint32_t file_size)
{
    if (file_size == 0) {
        throw std::runtime_error("unsupported Windows lowering: executable payload is empty");
    }
    if (data.size() < PSX_HEADER_SIZE + file_size) {
        throw std::runtime_error("truncated PS-X EXE payload");
    }

    const std::uint32_t initial_pc = read_psx32(data, 0x10);
    const std::uint32_t initial_gp = read_psx32(data, 0x14);
    const std::uint32_t initial_sp_base = read_psx32(data, 0x30);
    const std::uint32_t initial_sp_offset = read_psx32(data, 0x34);
    const std::uint32_t initial_sp =
        initial_sp_base == 0 && initial_sp_offset == 0
            ? 0x801FF000u
            : initial_sp_base + initial_sp_offset;

    std::string diagnostics =
        "AnyPS1 native executable diagnostics\n"
        "target=windows\n"
        "load_address=" + hex32(load_address) + "\n"
        "entry_point=" + hex32(initial_pc) + "\n"
        "initial_gp=" + hex32(initial_gp) + "\n"
        "initial_sp=" + hex32(initial_sp) + "\n"
        "payload_size=" + std::to_string(file_size) + "\n";

    std::vector<unsigned char> pe(RAW_HEADERS_SIZE, 0);
    pe[0] = 'M';
    pe[1] = 'Z';
    write_u32(pe, 0x3C, PE_SIGNATURE_OFFSET);

    pe[PE_SIGNATURE_OFFSET] = 'P';
    pe[PE_SIGNATURE_OFFSET + 1] = 'E';
    pe[PE_SIGNATURE_OFFSET + 2] = '\0';
    pe[PE_SIGNATURE_OFFSET + 3] = '\0';

    write_u16(pe, COFF_OFFSET, 0x8664);
    write_u16(pe, COFF_OFFSET + 2, 4);
    write_u32(pe, COFF_OFFSET + 4, 0);
    write_u32(pe, COFF_OFFSET + 8, SECTION_TABLE_OFFSET);
    write_u16(pe, COFF_OFFSET + 16, 240);
    write_u16(pe, COFF_OFFSET + 18, 0x0222);

    write_u16(pe, OPTIONAL_OFFSET, 0x020B);
    pe[OPTIONAL_OFFSET + 2] = 14;
    pe[OPTIONAL_OFFSET + 3] = 0;
    write_u32(pe, OPTIONAL_OFFSET + 16, TEXT_RVA);
    write_u32(pe, OPTIONAL_OFFSET + 20, TEXT_RVA);
    write_u64(pe, OPTIONAL_OFFSET + 24, IMAGE_BASE);
    write_u32(pe, OPTIONAL_OFFSET + 32, SECTION_ALIGNMENT);
    write_u32(pe, OPTIONAL_OFFSET + 36, FILE_ALIGNMENT);
    write_u16(pe, OPTIONAL_OFFSET + 40, 6);
    write_u16(pe, OPTIONAL_OFFSET + 42, 0);
    write_u16(pe, OPTIONAL_OFFSET + 44, 6);
    write_u16(pe, OPTIONAL_OFFSET + 46, 0);
    write_u16(pe, OPTIONAL_OFFSET + 48, 6);
    write_u16(pe, OPTIONAL_OFFSET + 50, 0);
    write_u32(pe, OPTIONAL_OFFSET + 56, IMAGE_SIZE);
    write_u32(pe, OPTIONAL_OFFSET + 60, RAW_HEADERS_SIZE);
    write_u32(pe, OPTIONAL_OFFSET + 64, 0);
    write_u16(pe, OPTIONAL_OFFSET + 68, 3);
    write_u16(pe, OPTIONAL_OFFSET + 70, 0);
    write_u64(pe, OPTIONAL_OFFSET + 72, 0x100000u);
    write_u64(pe, OPTIONAL_OFFSET + 80, 0x1000u);
    write_u64(pe, OPTIONAL_OFFSET + 88, 0x100000u);
    write_u64(pe, OPTIONAL_OFFSET + 96, 0x1000u);
    write_u32(pe, OPTIONAL_OFFSET + 104, 0);
    write_u32(pe, OPTIONAL_OFFSET + 108, 16);
    write_u32(pe, OPTIONAL_OFFSET + 112, 0);
    write_u32(pe, OPTIONAL_OFFSET + 116, 0);
    write_u32(pe, OPTIONAL_OFFSET + 120, IMPORT_DESCRIPTOR_RVA);
    write_u32(pe, OPTIONAL_OFFSET + 124, 40);

    write_section_header(pe, SECTION_TABLE_OFFSET, ".text",
                         0x200, TEXT_RVA, 0x200, TEXT_RAW_OFFSET,
                         0x60000020);
    write_section_header(pe, SECTION_TABLE_OFFSET + 40, ".rdata",
                         0x800, RDATA_RVA, 0x800, RDATA_RAW_OFFSET,
                         0x40000040);
    write_section_header(pe, SECTION_TABLE_OFFSET + 80, ".data",
                         0x200, 0x3000, 0x200, DATA_RAW_OFFSET,
                         0xc0000040);
    write_section_header(pe, SECTION_TABLE_OFFSET + 120, ".reloc",
                         0x200, 0x204000, 0x200, RELOC_RAW_OFFSET,
                         0x42000040);

    pe.resize(RAW_HEADERS_SIZE);
    pe.resize(RELOC_RAW_OFFSET + 0x200, 0);


    std::vector<unsigned char> code;
    code.push_back(0x48); code.push_back(0x83); code.push_back(0xEC); code.push_back(0x28);
    code.push_back(0xB9);
    append_u32(code, 0xFFFFFFF5u);
    std::size_t call_offset = code.size();
    code.push_back(0x48); code.push_back(0x8B); code.push_back(0x05);
    append_u32(code, 0);
    append_rip_relative(code, static_cast<std::uint32_t>(call_offset), 7, IMPORT_IAT_RVA);
    code.push_back(0xFF); code.push_back(0xD0);

    code.push_back(0x48); code.push_back(0x89); code.push_back(0xC1);
    call_offset = code.size();
    code.push_back(0x48); code.push_back(0x8D); code.push_back(0x15);
    append_u32(code, 0);
    append_rip_relative(code, static_cast<std::uint32_t>(call_offset), 7, DIAGNOSTIC_RVA);

    code.push_back(0x41); code.push_back(0xB8);
    append_u32(code, static_cast<std::uint32_t>(diagnostics.size()));
    code.push_back(0x45); code.push_back(0x31); code.push_back(0xC9);

    call_offset = code.size();
    code.push_back(0x48); code.push_back(0x8B); code.push_back(0x05);
    append_u32(code, 0);
    append_rip_relative(code, static_cast<std::uint32_t>(call_offset), 7, IMPORT_IAT_RVA + 8);
    code.push_back(0xFF); code.push_back(0xD0);

    code.push_back(0x48); code.push_back(0xC7); code.push_back(0x44); code.push_back(0x24);
    code.push_back(0x20); append_u32(code, 0);

    code.push_back(0xB9);
    append_u32(code, 16);
    call_offset = code.size();
    code.push_back(0x48); code.push_back(0x8B); code.push_back(0x05);
    append_u32(code, 0);
    append_rip_relative(code, static_cast<std::uint32_t>(call_offset), 7, IMPORT_IAT_RVA + 16);
    code.push_back(0xFF); code.push_back(0xD0);
    code.push_back(0x48); code.push_back(0x83); code.push_back(0xC4); code.push_back(0x28);
    code.push_back(0xC3);

    while (code.size() < 0x200) code.push_back(0xCC);
    std::memcpy(pe.data() + TEXT_RAW_OFFSET, code.data(), code.size());

    std::vector<unsigned char> rdata(0x400, 0);
    write_u32(rdata, 0x120, IMPORT_LOOKUP_RVA);
    write_u32(rdata, 0x12C, IMPORT_DLL_NAME_RVA);
    write_u32(rdata, 0x130, IMPORT_IAT_RVA);
    std::memcpy(rdata.data() + 0xC0, "KERNEL32.dll", 13);

    write_u64(rdata, 0x80, GET_STD_HANDLE_RVA);
    write_u64(rdata, 0x88, WRITE_FILE_RVA);
    write_u64(rdata, 0x90, EXIT_PROCESS_RVA);

    write_u64(rdata, 0xA0, GET_STD_HANDLE_RVA);
    write_u64(rdata, 0xA8, WRITE_FILE_RVA);
    write_u64(rdata, 0xB0, EXIT_PROCESS_RVA);

    rdata[0x140] = 0;
    rdata[0x141] = 0;
    std::memcpy(rdata.data() + 0x142, "GetStdHandle", 13);
    rdata[0x150] = 0;
    rdata[0x151] = 0;
    std::memcpy(rdata.data() + 0x152, "WriteFile", 10);
    rdata[0x160] = 0;
    rdata[0x161] = 0;
    std::memcpy(rdata.data() + 0x162, "ExitProcess", 12);
    std::memcpy(rdata.data() + 0x180, diagnostics.data(), diagnostics.size());
    std::memcpy(pe.data() + RDATA_RAW_OFFSET, rdata.data(), rdata.size());

    const std::filesystem::path output_path(options.output);
    std::ofstream output(output_path, std::ios::binary | std::ios::trunc);
    if (!output) throw std::runtime_error("cannot create Windows PE output: " + options.output);
    output.write(reinterpret_cast<const char*>(pe.data()),
                 static_cast<std::streamsize>(pe.size()));
    if (!output) throw std::runtime_error("cannot write Windows PE output: " + options.output);
}

}
