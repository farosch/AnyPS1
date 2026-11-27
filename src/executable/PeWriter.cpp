#include "PeWriter.h"
#include "relinker/Relinker.h"
#include "format/PsExe.h"

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
constexpr std::uint32_t BSS_RVA = 0x3000;
constexpr std::uint32_t RELOC_RVA = 0x4000;
constexpr std::uint32_t IMAGE_SIZE = 0x5000;
constexpr std::uint64_t IMAGE_BASE = 0x140000000ull;

void append_u16(std::vector<unsigned char>& output, std::uint16_t value)
{
    output.push_back(static_cast<unsigned char>(value & 0xFF));
    output.push_back(static_cast<unsigned char>((value >> 8) & 0xFF));
}

void append_u32(std::vector<unsigned char>& output, std::uint32_t value)
{
    output.push_back(static_cast<unsigned char>(value & 0xFF));
    output.push_back(static_cast<unsigned char>((value >> 8) & 0xFF));
    output.push_back(static_cast<unsigned char>((value >> 16) & 0xFF));
    output.push_back(static_cast<unsigned char>((value >> 24) & 0xFF));
}

void write_u16(std::vector<unsigned char>& output, std::size_t offset, std::uint16_t value)
{
    output[offset] = static_cast<unsigned char>(value & 0xFF);
    output[offset + 1] = static_cast<unsigned char>((value >> 8) & 0xFF);
}

void write_u32(std::vector<unsigned char>& output, std::size_t offset, std::uint32_t value)
{
    output[offset] = static_cast<unsigned char>(value & 0xFF);
    output[offset + 1] = static_cast<unsigned char>((value >> 8) & 0xFF);
    output[offset + 2] = static_cast<unsigned char>((value >> 16) & 0xFF);
    output[offset + 3] = static_cast<unsigned char>((value >> 24) & 0xFF);
}

void write_u64(std::vector<unsigned char>& output, std::size_t offset, std::uint64_t value)
{
    write_u32(output, offset, static_cast<std::uint32_t>(value & 0xFFFFFFFFu));
    write_u32(output, offset + 4, static_cast<std::uint32_t>((value >> 32) & 0xFFFFFFFFu));
}

void append_rip_relative(std::vector<unsigned char>& code,
                         std::uint32_t instruction_offset,
                         std::uint32_t instruction_length,
                         std::uint32_t target_rva)
{
    const std::uint64_t instruction_address =
        IMAGE_BASE + TEXT_RVA + instruction_offset;
    const std::uint64_t target_address =
        IMAGE_BASE + target_rva;
    const std::int64_t displacement =
        static_cast<std::int64_t>(target_address - instruction_address - instruction_length);
    if (displacement < INT32_MIN || displacement > INT32_MAX) {
        throw std::runtime_error("PE RIP-relative displacement exceeds 32-bit range");
    }
    write_u32(code, instruction_offset + instruction_length - 4,
              static_cast<std::uint32_t>(static_cast<std::int32_t>(displacement)));
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
    if (file_size > 0x01000000u) {
        throw std::runtime_error("unsupported Windows lowering: executable payload is too large");
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
        "payload_size=" + std::to_string(file_size) + "\n"
        "native Windows MIPS lowering is not implemented\n";
    if (diagnostics.size() > 128u) {
        diagnostics.resize(128u);
    }

    if (data.size() < PSX_HEADER_SIZE + file_size) {
        throw std::runtime_error("truncated PS-X EXE payload");
    }

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
    write_u32(pe, COFF_OFFSET + 8, SECTION_TABLE_OFFSET);
    write_u16(pe, COFF_OFFSET + 16, 240);
    write_u16(pe, COFF_OFFSET + 18, 0x0002 | 0x0020 | 0x0100);

    write_u16(pe, OPTIONAL_OFFSET, 0x020B);
    pe[OPTIONAL_OFFSET + 2] = 14;
    pe[OPTIONAL_OFFSET + 3] = 0;
    write_u32(pe, OPTIONAL_OFFSET + 4, 0);
    write_u32(pe, OPTIONAL_OFFSET + 8, 0);
    write_u32(pe, OPTIONAL_OFFSET + 12, 0);
    write_u32(pe, OPTIONAL_OFFSET + 16, TEXT_RVA);
    write_u32(pe, OPTIONAL_OFFSET + 20, TEXT_RVA);
    write_u64(pe, OPTIONAL_OFFSET + 24, IMAGE_BASE);
    write_u32(pe, OPTIONAL_OFFSET + 32, SECTION_ALIGNMENT);
    write_u32(pe, OPTIONAL_OFFSET + 36, FILE_ALIGNMENT);
    write_u16(pe, OPTIONAL_OFFSET + 40, 6);
    write_u16(pe, OPTIONAL_OFFSET + 42, 0);
    write_u16(pe, OPTIONAL_OFFSET + 44, 0);
    write_u16(pe, OPTIONAL_OFFSET + 46, 0);
    write_u16(pe, OPTIONAL_OFFSET + 48, 6);
    write_u16(pe, OPTIONAL_OFFSET + 50, 0);
    write_u16(pe, OPTIONAL_OFFSET + 56, 0);
    write_u32(pe, OPTIONAL_OFFSET + 60, IMAGE_SIZE);
    write_u32(pe, OPTIONAL_OFFSET + 64, RAW_HEADERS_SIZE);
    write_u32(pe, OPTIONAL_OFFSET + 68, 0);
    write_u16(pe, OPTIONAL_OFFSET + 72, 3);
    write_u16(pe, OPTIONAL_OFFSET + 74, 0);
    write_u64(pe, OPTIONAL_OFFSET + 76, 0x100000u);
    write_u64(pe, OPTIONAL_OFFSET + 84, 0x1000u);
    write_u64(pe, OPTIONAL_OFFSET + 92, 0x100000u);
    write_u64(pe, OPTIONAL_OFFSET + 100, 0x1000u);
    write_u32(pe, OPTIONAL_OFFSET + 108, 0);
    write_u32(pe, OPTIONAL_OFFSET + 112, 16);

    write_u32(pe, OPTIONAL_OFFSET + 120, RDATA_RVA);
    write_u32(pe, OPTIONAL_OFFSET + 124, 0x200);
    write_u32(pe, OPTIONAL_OFFSET + 152, RELOC_RVA);
    write_u32(pe, OPTIONAL_OFFSET + 156, 8);

    const std::uint32_t section_offset = SECTION_TABLE_OFFSET;
    std::memcpy(pe.data() + section_offset, ".text", 5);
    write_u32(pe, section_offset + 8, 0x200);
    write_u32(pe, section_offset + 12, TEXT_RVA);
    write_u32(pe, section_offset + 16, 0x200);
    write_u32(pe, section_offset + 20, RAW_HEADERS_SIZE);
    write_u16(pe, section_offset + 32, 0);
    write_u16(pe, section_offset + 34, 0);
    write_u32(pe, section_offset + 36, 0x60000020);

    const std::size_t rdata_section = section_offset + 40;
    std::memcpy(pe.data() + rdata_section, ".rdata", 6);
    write_u32(pe, rdata_section + 8, 0x200);
    write_u32(pe, rdata_section + 12, RDATA_RVA);
    write_u32(pe, rdata_section + 16, 0x200);
    write_u32(pe, rdata_section + 20, RAW_HEADERS_SIZE + 0x200);
    write_u32(pe, rdata_section + 36, 0x40000040);

    const std::size_t bss_section = section_offset + 80;
    std::memcpy(pe.data() + bss_section, ".bss", 4);
    write_u32(pe, bss_section + 8, 2097152u);
    write_u32(pe, bss_section + 12, BSS_RVA);
    write_u32(pe, bss_section + 16, 0);
    write_u32(pe, bss_section + 20, 0);
    write_u32(pe, bss_section + 36, 0xC8000080);

    const std::size_t reloc_section = section_offset + 120;
    std::memcpy(pe.data() + reloc_section, ".reloc", 6);
    write_u32(pe, reloc_section + 8, 0x200);
    write_u32(pe, reloc_section + 12, RELOC_RVA);
    write_u32(pe, reloc_section + 16, 0x200);
    write_u32(pe, reloc_section + 20, RAW_HEADERS_SIZE + 0x400);
    write_u32(pe, reloc_section + 36, 0x42000040);

    pe.resize(RAW_HEADERS_SIZE);
    pe.resize(RAW_HEADERS_SIZE + 0x200, 0);
    pe.resize(RAW_HEADERS_SIZE + 0x400, 0);
    pe.resize(RAW_HEADERS_SIZE + 0x600, 0);

    std::vector<unsigned char> code;
    const std::uint32_t iat_rva = RDATA_RVA + 0x120;
    const std::uint32_t diagnostic_rva = RDATA_RVA + 0x180;
    const std::uint32_t written_rva = RDATA_RVA + 0x160;

    code.push_back(0xB9);
    append_u32(code, 0xFFFFFFF5u);

    std::size_t call_offset = code.size();
    code.push_back(0x48); code.push_back(0x8B); code.push_back(0x05);
    append_u32(code, 0);
    append_rip_relative(code, static_cast<std::uint32_t>(call_offset), 7,
                        iat_rva);
    code.push_back(0xFF); code.push_back(0xD0);

    code.push_back(0x48); code.push_back(0x89); code.push_back(0xC1);

    call_offset = code.size();
    code.push_back(0x48); code.push_back(0x8D); code.push_back(0x15);
    append_u32(code, 0);
    append_rip_relative(code, static_cast<std::uint32_t>(call_offset), 7,
                        diagnostic_rva);

    code.push_back(0x41); code.push_back(0xB8);
    append_u32(code, static_cast<std::uint32_t>(diagnostics.size()));

    code.push_back(0x45); code.push_back(0x31); code.push_back(0xC9);
    code.push_back(0x48); code.push_back(0x83); code.push_back(0xEC); code.push_back(0x28);

    call_offset = code.size();
    code.push_back(0x48); code.push_back(0x8D); code.push_back(0x05);
    append_u32(code, 0);
    append_rip_relative(code, static_cast<std::uint32_t>(call_offset), 7,
                        written_rva);
    code.push_back(0x48); code.push_back(0x89); code.push_back(0x44); code.push_back(0x24);
    code.push_back(0x20);

    call_offset = code.size();
    code.push_back(0x48); code.push_back(0x8B); code.push_back(0x05);
    append_u32(code, 0);
    append_rip_relative(code, static_cast<std::uint32_t>(call_offset), 7,
                        iat_rva + 8);
    code.push_back(0xFF); code.push_back(0xD0);
    code.push_back(0x48); code.push_back(0x83); code.push_back(0xC4); code.push_back(0x28);

    code.push_back(0xB9);
    append_u32(code, 16);
    call_offset = code.size();
    code.push_back(0x48); code.push_back(0x8B); code.push_back(0x05);
    append_u32(code, 0);
    append_rip_relative(code, static_cast<std::uint32_t>(call_offset), 7,
                        iat_rva + 16);
    code.push_back(0xFF); code.push_back(0xD0);

    while (code.size() < 0x200) code.push_back(0xCC);
    std::memcpy(pe.data() + RAW_HEADERS_SIZE, code.data(), code.size());

    std::vector<unsigned char> rdata(0x200, 0);
    write_u32(rdata, 0x00, RDATA_RVA + 0x100);
    write_u32(rdata, 0x0C, RDATA_RVA + 0x170);
    write_u32(rdata, 0x10, RDATA_RVA + 0x120);

    rdata[0x128] = 0;
    rdata[0x129] = 0;
    std::memcpy(rdata.data() + 0x12A, "GetStdHandle", 13);
    rdata[0x140] = 0;
    rdata[0x141] = 0;
    std::memcpy(rdata.data() + 0x142, "WriteFile", 9);
    rdata[0x150] = 0;
    rdata[0x151] = 0;
    std::memcpy(rdata.data() + 0x152, "ExitProcess", 11);
    std::memcpy(rdata.data() + 0x170, "KERNEL32.dll", 12);

    write_u64(rdata, 0x100, RDATA_RVA + 0x128);
    write_u64(rdata, 0x108, RDATA_RVA + 0x140);
    write_u64(rdata, 0x110, RDATA_RVA + 0x150);
    write_u64(rdata, 0x118, 0);

    write_u64(rdata, 0x120, RDATA_RVA + 0x128);
    write_u64(rdata, 0x128, RDATA_RVA + 0x140);
    write_u64(rdata, 0x130, RDATA_RVA + 0x150);
    write_u64(rdata, 0x138, 0);

    std::memcpy(rdata.data() + 0x180, diagnostics.data(), diagnostics.size());
    std::memcpy(pe.data() + RAW_HEADERS_SIZE + 0x200, rdata.data(), rdata.size());

    std::vector<unsigned char> reloc(0x200, 0);
    write_u32(reloc, 0, TEXT_RVA);
    write_u32(reloc, 4, 8);
    std::memcpy(pe.data() + RAW_HEADERS_SIZE + 0x400, reloc.data(), reloc.size());

    const std::filesystem::path output_path(options.output);
    std::ofstream output(output_path, std::ios::binary | std::ios::trunc);
    if (!output) throw std::runtime_error("cannot create Windows PE output: " + options.output);
    output.write(reinterpret_cast<const char*>(pe.data()),
                 static_cast<std::streamsize>(pe.size()));
    if (!output) throw std::runtime_error("cannot write Windows PE output: " + options.output);
}
}
