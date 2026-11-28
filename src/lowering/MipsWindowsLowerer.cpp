#include "MipsWindowsLowerer.h"
#include "MipsLowerer.h"
#include "executable/PeWriter.h"
#include "lowering/NativeLoweringIr.h"
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
    const MipsExecutablePlan plan = build_mips_executable_plan(options, data);
    const std::uint32_t file_size = plan.file_size;
    const std::uint32_t load_address = plan.load_address;
    const std::uint32_t initial_pc = plan.initial_pc;
    const std::uint32_t initial_gp = plan.initial_gp;
    const std::uint32_t unsupported_count = plan.unsupported_count;
    const std::uint32_t first_address = plan.first_unsupported_address;
    const std::uint32_t first_word = plan.first_unsupported_word;
    const std::string& first_name = plan.first_unsupported_name;

    std::ostringstream diagnostics;
    diagnostics
        << "AnyPS1 native executable diagnostics\n"
        << "target=windows\n"
        << "load_address=0x" << std::hex << load_address << "\n"
        << "entry_point=0x" << std::hex << initial_pc << "\n"
        << "initial_gp=0x" << std::hex << initial_gp << "\n"
        << "payload_size=" << std::dec << file_size << "\n"
        << "reachable_words=" << plan.reachable_count << "\n"
        << "supported_reachable_words=" << plan.supported_count << "\n"
        << "unsupported_instructions=" << unsupported_count << "\n";
    if (unsupported_count != 0) {
        diagnostics
            << "first_unsupported_gap=0x" << std::hex << first_address
            << " instruction=0x" << std::hex << first_word
            << " name=" << first_name << "\n";
    } else {
        diagnostics << "first_unsupported_gap=none\n";
    }
    if (unsupported_count == 0) {
        if (plan.native_host_loop_required) {
            diagnostics
                << "next_runtime_blocker=PS1 MMIO/host-device state progression\n";
        } else if (plan.supported_count != 0) {
            diagnostics
                << "next_runtime_blocker=Windows native MIPS instruction emission\n";
        } else {
            diagnostics
                << "next_runtime_blocker=no reachable supported MIPS instructions\n";
        }
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
        plan.supported_count != 0 ? 17 : 17;
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
