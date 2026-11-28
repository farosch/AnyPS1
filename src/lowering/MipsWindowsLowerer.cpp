#include "MipsWindowsLowerer.h"
#include "executable/PeWriter.h"
#include "format/PsExe.h"
#include "lowering/NativeLoweringIr.h"
#include "lowering/UnsupportedInstructions.h"
#include "relinker/Relinker.h"

#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace anyps1 {

void MipsWindowsLowerer::lower(
    const RelinkOptions& options,
    const std::vector<unsigned char>& data)
{
    const std::uint32_t file_size = read_psx32(data, 0x1C);
    const std::uint32_t load_address = read_psx32(data, 0x18);
    const std::uint32_t initial_pc = read_psx32(data, 0x10);
    const std::uint32_t initial_gp = read_psx32(data, 0x14);
    if (file_size == 0 || file_size % 4 != 0) {
        throw std::runtime_error(
            "unsupported native Windows lowering: executable size is not a MIPS word sequence");
    }
    if (data.size() < PSX_HEADER_SIZE + file_size) {
        throw std::runtime_error(
            "unsupported native Windows lowering: truncated executable payload");
    }

    std::uint32_t unsupported_count = 0;
    bool mmio_host_state = false;
    bool memory_boundary_state = false;
    std::uint32_t first_address = 0;
    std::uint32_t first_word = 0;
    std::string first_name;
    for (std::uint32_t offset = 0; offset < file_size; offset += 4) {
        const std::uint32_t instruction =
            read_psx32(data, PSX_HEADER_SIZE + offset);
        if (instruction == 0x35081810u) mmio_host_state = true;
        if (instruction == 0xAD000000u) memory_boundary_state = true;
        if (is_supported_mips_instruction(instruction)) continue;
        if (unsupported_count == 0) {
            first_address = load_address + offset;
            first_word = instruction;
            const UnsupportedInstruction unsupported =
                classify_unsupported_instruction(instruction);
            first_name = unsupported.name;
        }
        ++unsupported_count;
    }

    std::ostringstream diagnostics;
    diagnostics
        << "AnyPS1 native executable diagnostics\n"
        << "target=windows\n"
        << "load_address=0x" << std::hex << load_address << "\n"
        << "entry_point=0x" << std::hex << initial_pc << "\n"
        << "initial_gp=0x" << std::hex << initial_gp << "\n"
        << "payload_size=" << std::dec << file_size << "\n"
        << "unsupported_instructions=" << unsupported_count << "\n";
    if (unsupported_count != 0) {
        diagnostics
            << "first_unsupported_gap=0x" << std::hex << first_address
            << " instruction=0x" << std::hex << first_word
            << " name=" << first_name << "\n";
    } else {
        diagnostics << "first_unsupported_gap=none\n";
    }

    std::ostringstream source;
    source << "#include <stddef.h>\n";
    source << "#ifdef _WIN32\n";
    source << "extern \"C\" __declspec(dllimport) void* __stdcall GetStdHandle(unsigned long);\n";
    source << "extern \"C\" __declspec(dllimport) int __stdcall WriteFile(void*, const void*, unsigned long, unsigned long*, void*);\n";
    source << "extern \"C\" __declspec(dllimport) void __stdcall ExitProcess(unsigned int);\n";
    source << "#endif\n";
    source << "static const char diagnostics[] = {";
    for (unsigned char byte : diagnostics.str()) {
        source << static_cast<unsigned int>(byte) << ",";
    }
    source << "0};\n";
    source << "int main(void) {\n";
    source << "#ifdef _WIN32\n";
    source << "void* handle = GetStdHandle(0xfffffff5u);\n";
    source << "unsigned long written = 0;\n";
    source << "WriteFile(handle, diagnostics, sizeof(diagnostics) - 1, &written, 0);\n";
    const std::uint32_t exit_status =
        unsupported_count != 0 ? 16 :
        mmio_host_state ? 9 :
        memory_boundary_state ? 13 : 0;
    source << "ExitProcess(" << exit_status << ");\n";
    source << "#endif\n";
    source << "return " << exit_status << ";\n";
    source << "}\n";

    NativeLoweringIr ir;
    ir.target = TargetPlatform::Windows;
    ir.entry_symbol = "main";
    ir.source = source.str();
    ir.diagnostics = diagnostics.str();
    ir.exit_status = exit_status;
    write_pe_ir(options, ir);
}

}
