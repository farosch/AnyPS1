#include "MipsWindowsLowerer.h"
#include "relinker/Relinker.h"
#include "format/PsExe.h"
#include "UnsupportedInstructions.h"
#include "PayloadRegion.h"
#include "executable/PeWriter.h"
#include "host/HostServices.h"
#include "MipsLoweringSupport.h"

#include <cstdint>
#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <queue>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif

namespace anyps1 {

namespace {

#ifdef _WIN32
std::filesystem::path find_windows_tool(const char* name)
{
    char* tools = nullptr;
    size_t tools_length = 0;
    if (_dupenv_s(&tools, &tools_length, "VCToolsInstallDir") == 0 &&
        tools != nullptr) {
        const std::filesystem::path candidate =
            std::filesystem::path(tools) / "bin" / "Hostx64" / "x64" / name;
        if (std::filesystem::is_regular_file(candidate)) return candidate;
        std::free(tools);
    }
    const std::filesystem::path roots[] = {
        "C:\\Program Files (x86)\\Microsoft Visual Studio",
        "C:\\Program Files\\Microsoft Visual Studio",
    };
    for (const std::filesystem::path& root : roots) {
        std::error_code error;
        if (!std::filesystem::exists(root, error)) continue;
        for (const auto& edition : std::filesystem::directory_iterator(root, error)) {
            if (!edition.is_directory()) continue;
            const std::filesystem::path variants[] = {
                edition.path() / "VC" / "Tools" / "MSVC",
                edition.path() / "BuildTools" / "VC" / "Tools" / "MSVC",
                edition.path() / "Community" / "VC" / "Tools" / "MSVC",
                edition.path() / "Professional" / "VC" / "Tools" / "MSVC",
                edition.path() / "Enterprise" / "VC" / "Tools" / "MSVC",
            };
            for (const std::filesystem::path& base : variants) {
                if (!std::filesystem::exists(base, error)) continue;
                for (const auto& version : std::filesystem::directory_iterator(base, error)) {
                    if (!version.is_directory()) continue;
                    const std::filesystem::path candidate =
                        version.path() / "bin" / "Hostx64" / "x64" / name;
                    if (std::filesystem::is_regular_file(candidate)) return candidate;
                }
            }
        }
    }
    throw std::runtime_error(
        std::string("cannot locate MSVC Windows tool: ") + name);
}
#endif

#ifdef _WIN32
std::vector<std::filesystem::path> find_windows_library_dirs()
{
    std::vector<std::filesystem::path> directories;
#ifdef _WIN32
    char* tools = nullptr;
    size_t tools_length = 0;
    if (_dupenv_s(&tools, &tools_length, "VCToolsInstallDir") == 0 &&
        tools != nullptr) {
        const std::filesystem::path msvc_tools(tools);
        for (const auto& candidate : {
                 msvc_tools / "lib" / "x64",
                 msvc_tools / "atlmfc" / "lib" / "x64"}) {
            if (std::filesystem::is_directory(candidate)) {
                directories.push_back(candidate);
            }
        }
        std::free(tools);
    }
    const std::filesystem::path roots[] = {
        "C:\\Program Files (x86)\\Microsoft Visual Studio",
        "C:\\Program Files\\Microsoft Visual Studio",
    };
    for (const std::filesystem::path& root : roots) {
        std::error_code error;
        if (!std::filesystem::exists(root, error)) continue;
        for (const auto& edition : std::filesystem::directory_iterator(root, error)) {
            if (!edition.is_directory()) continue;
            const std::filesystem::path variants[] = {
                edition.path() / "VC" / "Tools" / "MSVC",
                edition.path() / "BuildTools" / "VC" / "Tools" / "MSVC",
                edition.path() / "Community" / "VC" / "Tools" / "MSVC",
                edition.path() / "Professional" / "VC" / "Tools" / "MSVC",
                edition.path() / "Enterprise" / "VC" / "Tools" / "MSVC",
            };
            for (const std::filesystem::path& base : variants) {
                if (!std::filesystem::exists(base, error)) continue;
                for (const auto& version : std::filesystem::directory_iterator(base, error)) {
                    const std::filesystem::path lib = version.path() / "lib" / "x64";
                    if (std::filesystem::is_directory(lib)) directories.push_back(lib);
                }
            }
        }
    }
    const std::filesystem::path kits_root =
        "C:\\Program Files (x86)\\Windows Kits\\10\\Lib";
    std::error_code error;
    if (std::filesystem::exists(kits_root, error)) {
        std::vector<std::filesystem::path> versions;
        for (const auto& version : std::filesystem::directory_iterator(kits_root, error)) {
            if (version.is_directory()) versions.push_back(version.path());
        }
        std::sort(versions.begin(), versions.end(),
                  [](const std::filesystem::path& left,
                     const std::filesystem::path& right) {
                      return left.filename().string() > right.filename().string();
                  });
        for (const std::filesystem::path& version : versions) {
            const std::filesystem::path um = version / "um" / "x64";
            const std::filesystem::path ucrt = version / "ucrt" / "x64";
            if (std::filesystem::is_directory(um)) directories.push_back(um);
            if (std::filesystem::is_directory(ucrt)) directories.push_back(ucrt);
            if (!directories.empty()) break;
        }
    }
#endif
    return directories;
}
#endif

#ifdef _WIN32
void emit_string(std::ofstream& source,
                 const std::string& label,
                 const std::string& value)
{
    source << label << " db ";
    bool first = true;
    std::size_t characters_in_line = 0;
    for (unsigned char character : value) {
        if (characters_in_line == 32) {
            source << "\n db ";
            characters_in_line = 0;
            first = true;
        }
        if (!first) source << ',';
        source << static_cast<unsigned int>(character);
        first = false;
        ++characters_in_line;
    }
    if (!first) source << ',';
    source << "0\n";
    source << label << "_length EQU $-" << label << "\n";
}
#endif

#ifdef _WIN32
bool run_windows_tool(const std::filesystem::path& tool,
                      const std::string& arguments)
{
    std::wstring tool_wide(tool.wstring());
    std::wstring arguments_wide(arguments.begin(), arguments.end());
    std::wstring command_line =
        L"\"" + tool_wide + L"\"" + L" " + arguments_wide;
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(
            nullptr,
            command_line.data(),
            nullptr,
            nullptr,
            FALSE,
            0,
            nullptr,
            nullptr,
            &startup,
            &process)) {
        return false;
    }
    WaitForSingleObject(process.hProcess, INFINITE);
    DWORD exit_code = 0;
    GetExitCodeProcess(process.hProcess, &exit_code);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return exit_code == 0;
}
#endif

}

void MipsWindowsLowerer::lower(
    const RelinkOptions& options,
    const std::vector<unsigned char>& data)
{
#ifndef _WIN32
    write_pe_layout(
        options,
        data,
        read_psx32(data, 0x18),
        read_psx32(data, 0x1C));
#else
    const std::uint32_t file_size = read_psx32(data, 0x1C);
    const std::uint32_t load_address = read_psx32(data, 0x18);
    const std::uint32_t initial_pc = read_psx32(data, 0x10);
    const std::uint32_t initial_gp = read_psx32(data, 0x14);
    const std::uint32_t initial_sp_base = read_psx32(data, 0x30);
    const std::uint32_t initial_sp_offset = read_psx32(data, 0x34);
    const std::uint32_t initial_sp =
        initial_sp_base == 0 && initial_sp_offset == 0
            ? 0x801FF000u
            : initial_sp_base + initial_sp_offset;
    if (file_size == 0 || file_size % 4 != 0) {
        throw std::runtime_error(
            "unsupported native Windows lowering: executable size is not a MIPS word sequence");
    }
    if (initial_pc < load_address ||
        initial_pc >= static_cast<std::uint64_t>(load_address) + file_size) {
        throw std::runtime_error(
            "unsupported native Windows lowering: entry point outside executable payload");
    }
    const std::uint32_t payload_ram_offset =
        load_address & 0x1FFFFFu;
    if (payload_ram_offset > 0x200000u ||
        file_size > 0x200000u - payload_ram_offset) {
        throw std::runtime_error(
            "unsupported native Windows lowering: "
            "executable payload exceeds PS1 RAM");
    }

    std::set<std::uint32_t> leaders;
    std::set<std::uint32_t> delay_slots;
    std::set<std::uint32_t> processed;
    std::queue<std::uint32_t> work;
    leaders.insert(initial_pc);
    work.push(initial_pc);
    std::uint32_t unsupported_count = 0;
    std::uint32_t reachable_count = 0;
    std::uint32_t supported_count = 0;
    std::uint32_t embedded_data_count = 0;
    std::uint32_t unknown_region_count = 0;
    std::uint32_t call_edges = 0;
    std::uint32_t unresolved_indirect_transfers = 0;
    std::uint32_t first_unsupported_address = 0;
    std::uint32_t first_unsupported_word = 0;
    std::string first_unsupported_name;
    std::set<std::string> unsupported_family_names;
    std::set<std::uint32_t> emittable_leaders;
    for (std::uint32_t target : discover_payload_pointer_targets(
             data,
             load_address,
             file_size)) {
        leaders.insert(target);
        work.push(target);
    }

    while (!work.empty()) {
        const std::uint32_t address = work.front();
        work.pop();
        if (!processed.insert(address).second) continue;
        if (address < load_address ||
            address >= static_cast<std::uint64_t>(load_address) + file_size ||
            (address & 3u) != 0) continue;
        const std::uint32_t offset = address - load_address;
        if (offset + 4 > file_size) continue;
        const std::uint32_t instruction =
            read_psx32(data, PSX_HEADER_SIZE + offset);
        const std::uint32_t opcode = instruction >> 26;
        const std::uint32_t immediate = instruction & 0xFFFFu;
        const std::uint32_t special = instruction & 63u;
        const std::uint32_t rs = (instruction >> 21) & 31u;
        const bool branch = opcode == 0x01 ||
            opcode == 0x04 || opcode == 0x05 ||
            opcode == 0x06 || opcode == 0x07 ||
            opcode == 0x14 || opcode == 0x15 ||
            opcode == 0x16 || opcode == 0x17 ||
            (opcode == 0x11 && (rs == 8 || rs == 12));
        const bool jump = opcode == 0x02 || opcode == 0x03;
        const bool jr = opcode == 0 && special == 0x08;
        const bool jalr = opcode == 0 && special == 0x09;
        const bool stop = opcode == 0 &&
            (special == 0x0C || special == 0x0D);

        if (branch) {
            const std::int32_t displacement =
                static_cast<std::int32_t>(static_cast<std::int16_t>(immediate)) * 4;
            const std::uint32_t target =
                static_cast<std::uint32_t>(
                    static_cast<std::int64_t>(address) + 4 + displacement);
            leaders.insert(target);
            leaders.insert(address + 8);
            work.push(target);
            work.push(address + 8);
        } else if (jump) {
            const std::uint32_t target =
                ((address + 4) & 0xF0000000u) |
                ((instruction & 0x03FFFFFFu) << 2);
            if (opcode == 0x03) ++call_edges;
            if ((target & 0xFF000000u) == 0xA0000000u ||
                (target & 0xFF000000u) == 0xB0000000u ||
                (target & 0xFF000000u) == 0xC0000000u) {
                leaders.insert(address + 8);
                work.push(address + 8);
            } else {
                leaders.insert(target);
                work.push(target);
            }
            if (opcode == 0x03) {
                leaders.insert(address + 8);
                work.push(address + 8);
            }
        } else if (!jr && !jalr && !stop) {
            leaders.insert(address + 4);
            work.push(address + 4);
        }
        if (jr || jalr) {
            if (jalr) {
                ++call_edges;
                leaders.insert(address + 8);
                work.push(address + 8);
            }
            const std::uint32_t resolved_target =
                resolve_static_indirect_target(
                    data,
                    load_address,
                    file_size,
                    address,
                    rs);
            if (resolved_target >= load_address &&
                resolved_target < static_cast<std::uint64_t>(load_address) + file_size &&
                (resolved_target & 3u) == 0) {
                leaders.insert(resolved_target);
                work.push(resolved_target);
            } else {
                const Ps1BiosCallPlan bios_call = map_ps1_bios_call(
                    resolved_target,
                    resolve_static_indirect_target(
                        data,
                        load_address,
                        file_size,
                        address + 4,
                        9u));
                const std::uint32_t return_target =
                    bios_call.kind == Ps1BiosCallKind::Unsupported
                        ? 0u
                        : resolve_static_indirect_target(
                            data,
                            load_address,
                            file_size,
                            address,
                            31u);
                if (return_target >= load_address &&
                    return_target < static_cast<std::uint64_t>(load_address) + file_size &&
                    (return_target & 3u) == 0) {
                    leaders.insert(return_target);
                    work.push(return_target);
                } else {
                    const std::vector<std::uint32_t> table_targets =
                        discover_indirect_table_targets(
                            data,
                            load_address,
                            file_size,
                            address,
                            rs);
                    if (table_targets.empty()) {
                        ++unresolved_indirect_transfers;
                    } else {
                        for (std::uint32_t target : table_targets) {
                            leaders.insert(target);
                            work.push(target);
                        }
                    }
                }
            }
        }
        if (branch || jump || jr || jalr) {
            const std::uint32_t delay_address = address + 4;
            if (delay_address >= load_address &&
                delay_address < static_cast<std::uint64_t>(load_address) + file_size) {
                delay_slots.insert(delay_address);
                leaders.insert(delay_address);
            }
        }
    }

    for (std::uint32_t address : leaders) {
        if (address < load_address ||
            address >= static_cast<std::uint64_t>(load_address) + file_size ||
            (address & 3u) != 0) continue;
        const std::uint32_t offset = address - load_address;
        if (offset + 4 > file_size) continue;
        const std::uint32_t instruction =
            read_psx32(data, PSX_HEADER_SIZE + offset);
        const PayloadRegionKind region =
            classify_payload_region(address, instruction, true);
        if (region == PayloadRegionKind::EmbeddedData) {
            ++embedded_data_count;
            continue;
        }
        if (region == PayloadRegionKind::Instruction ||
            is_supported_mips_instruction(instruction)) {
            emittable_leaders.insert(address);
        }
        if (region == PayloadRegionKind::Unknown) ++unknown_region_count;
        ++reachable_count;
        if (!is_supported_mips_instruction(instruction)) {
            ++unsupported_count;
            unsupported_family_names.insert(
                classify_unsupported_instruction(instruction).name);
            if (first_unsupported_address == 0) {
                first_unsupported_address = address;
                first_unsupported_word = instruction;
                first_unsupported_name =
                    classify_unsupported_instruction(instruction).name;
            }
        } else {
            ++supported_count;
        }
    }

    const std::filesystem::path assembly =
        std::filesystem::path(options.output + ".native.asm");
    const std::filesystem::path object =
        std::filesystem::path(options.output + ".native.obj");
    std::ofstream source(assembly, std::ios::trunc);
    if (!source) {
        throw std::runtime_error(
            "cannot create native Windows lowering assembly");
    }

    source << "OPTION DOTNAME\nOPTION CASEMAP:NONE\n";
    source << ".data\nALIGN 8\n";
    source << "regs dd 34 dup(0)\n";
    source << "hi_lo dd 2 dup(0)\n";
    source << "ram db 2097152 dup(0)\n";
    source << "scratchpad db 1024 dup(0)\n";
    source << "mmio_state db 8192 dup(0)\n";
    source << "host_cycles dd 0\n";
    source << "cop0 db 128 dup(0)\n";
    source << "cop3_data db 128 dup(0)\n";
    source << "fregs dd 32 dup(0)\n";
    source << "gte_data dd 32 dup(0)\n";
    source << "gte_ctrl dd 32 dup(0)\n";
    source << "fpcsr dd 0\n";
    source << "written dq 0\n";
    source << "pending_load_valid db 0\n";
    source << "pending_load_register db 0\n";
    source << "pending_load_value dd 0\n";
    source << "previous_load_valid db 0\n";
    source << "previous_load_register db 0\n";
    source << "previous_load_value dd 0\n";
    source << "delay_redirect_valid db 0\n";
    source << "delay_redirect_address dd 0\n";

    emit_string(source, "diag_banner",
                "AnyPS1 native executable diagnostics\n");
    emit_string(source, "diag_target", "target=windows\n");
    emit_string(source, "diag_load",
                "load_address=" + hex32(load_address) + "\n");
    emit_string(source, "diag_entry",
                "entry_point=" + hex32(initial_pc) + "\n");
    emit_string(source, "diag_gp",
                "initial_gp=" + hex32(initial_gp) + "\n");
    emit_string(source, "diag_sp",
                "initial_sp=" + hex32(initial_sp) + "\n");
    emit_string(source, "diag_payload",
                "payload_size=" + std::to_string(file_size) + "\n");
    emit_string(source, "diag_unsupported",
                "unsupported_instructions=" +
                std::to_string(unsupported_count) + "\n");
    emit_string(source, "diag_lowered",
                "lowered_words=" + std::to_string(leaders.size()) + "\n");
    const std::uint32_t payload_words = file_size / 4u;
    const std::uint32_t coverage_percent =
        payload_words == 0 ? 0u :
        static_cast<std::uint32_t>(leaders.size() * 100u / payload_words);
    emit_string(source, "diag_coverage",
                "lowered_coverage_percent=" +
                std::to_string(coverage_percent) + "\n");
    emit_string(source, "diag_edges",
                "call_edges=" + std::to_string(call_edges) + "\n");
    emit_string(source, "diag_unresolved",
                "unresolved_indirect_transfers=" +
                std::to_string(unresolved_indirect_transfers) + "\n");
    emit_string(source, "diag_reachable",
                "reachable_words=" + std::to_string(reachable_count) + "\n");
    emit_string(source, "diag_supported",
                "supported_reachable_words=" +
                std::to_string(supported_count) + "\n");
    emit_string(source, "diag_embedded",
                "embedded_data_words=" +
                std::to_string(embedded_data_count) + "\n");
    emit_string(source, "diag_unknown",
                "unknown_region_words=" +
                std::to_string(unknown_region_count) + "\n");
    emit_string(source, "diag_first_gap",
                first_unsupported_address == 0
                    ? "first_unsupported_gap=none\n"
                    : "first_unsupported_gap=" +
                      hex32(first_unsupported_address) +
                      " name=" + first_unsupported_name + "\n");
    const HostLoopPlan host_loop = plan_native_host_loop();
    emit_string(source, "diag_host_loop",
                "host_loop input=" + std::to_string(host_loop.input_polling) +
                " window=" + std::to_string(host_loop.window_events) +
                " audio=" + std::to_string(host_loop.audio) +
                " framebuffer=" + std::to_string(host_loop.framebuffer) +
                " vulkan=" + std::to_string(host_loop.vulkan) +
                " memory_card=" + std::to_string(host_loop.memory_card) +
                " disc=" + std::to_string(host_loop.disc) + "\n");
    emit_string(source, "diag_completed",
                "native lowering complete\n");
    source << "payload db ";
    for (std::uint32_t offset = 0; offset < file_size; ++offset) {
        if (offset != 0) {
            if ((offset % 16u) == 0) source << "\n db ";
            else source << ',';
        }
        source << static_cast<unsigned int>(data[PSX_HEADER_SIZE + offset]);
    }
    source << "\n";

    source << "INCLUDELIB kernel32.lib\n";
    source << "EXTERN anyps1_gte_execute:PROC\n";
    source << "EXTERN GetStdHandle:PROC\n";
    source << "EXTERN WriteFile:PROC\n";
    source << "EXTERN ExitProcess:PROC\n";
    source << "EXTERN CreateFileA:PROC\n";
    source << "EXTERN ReadFile:PROC\n";
    source << "EXTERN SetFilePointer:PROC\n";
    source << "EXTERN CloseHandle:PROC\n";
    source << ".code\n";
    source << "print_diag PROC\n";
    source << "sub rsp,40\n";
    source << "mov rcx,r12\n";
    source << "mov r9,OFFSET written\n";
    source << "mov qword ptr [rsp+32],0\n";
    source << "call WriteFile\n";
    source << "add rsp,40\n";
    source << "ret\n";
    source << "print_diag ENDP\n";
    source << "unsupported_exit PROC\n";
    source << "mov ecx,16\n";
    source << "call ExitProcess\n";
    source << "unsupported_exit ENDP\n";
    source << "indirect_jump PROC\n";
    const Ps1BiosCallPlan memcpy_call =
        map_ps1_bios_call(0xA0u, 0x39u);
    source << "cmp eax," << memcpy_call.vector << "\n";
    source << "jne indirect_leader_dispatch\n";
    source << "cmp dword ptr regs+36," << memcpy_call.function << "\n";
    source << "jne unresolved_indirect_exit\n";
    source << "mov eax,dword ptr regs+16\n";
    source << "mov dword ptr regs+8,eax\n";
    source << "test eax,eax\n";
    source << "jz bios_memcpy_return\n";
    source << "mov ecx,dword ptr regs+24\n";
    source << "test ecx,ecx\n";
    source << "jle bios_memcpy_return\n";
    source << "and eax,1FFFFFFFh\n";
    source << "cmp eax,800000h\n";
    source << "jae bios_memory_fault_exit\n";
    source << "and eax,1FFFFFh\n";
    source << "mov r10d,eax\n";
    source << "add r10d,ecx\n";
    source << "jb bios_memory_fault_exit\n";
    source << "cmp r10d,200000h\n";
    source << "ja bios_memory_fault_exit\n";
    source << "mov r11d,dword ptr regs+20\n";
    source << "and r11d,1FFFFFFFh\n";
    source << "cmp r11d,800000h\n";
    source << "jae bios_memory_fault_exit\n";
    source << "and r11d,1FFFFFh\n";
    source << "mov r8d,r11d\n";
    source << "add r8d,ecx\n";
    source << "jb bios_memory_fault_exit\n";
    source << "cmp r8d,200000h\n";
    source << "ja bios_memory_fault_exit\n";
    source << "lea r8,ram\n";
    source << "mov r10d,eax\n";
    source << "add r8,r10\n";
    source << "lea r9,ram\n";
    source << "add r9,r11\n";
    source << "bios_memcpy_byte:\n";
    source << "mov dl,byte ptr [r8]\n";
    source << "mov byte ptr [r9],dl\n";
    source << "inc r8\n";
    source << "inc r9\n";
    source << "dec ecx\n";
    source << "jnz bios_memcpy_byte\n";
    source << "bios_memcpy_return:\n";
    source << "mov eax,dword ptr regs+124\n";
    source << "jmp indirect_jump\n";
    source << "indirect_leader_dispatch:\n";
    for (std::uint32_t target : emittable_leaders) {
        if (target >= load_address &&
            target < static_cast<std::uint64_t>(load_address) + file_size) {
            source << "cmp eax," << target << "\n";
            source << "je p" << std::hex << target << std::dec << "\n";
        }
    }
    source << "jmp unresolved_indirect_exit\n";
    source << "indirect_jump ENDP\n";
    source << "unresolved_indirect_exit PROC\n";
    source << "mov ecx,17\n";
    source << "call ExitProcess\n";
    source << "unresolved_indirect_exit ENDP\n";
    source << "bios_memory_fault_exit PROC\n";
    source << "mov ecx,13\n";
    source << "call ExitProcess\n";
    source << "bios_memory_fault_exit ENDP\n";
    source << "main PROC\n";
    source << "sub rsp,40\n";
    source << "mov ecx,-11\n";
    source << "call GetStdHandle\n";
    source << "mov r12,rax\n";

    const char* diagnostics[] = {
        "diag_banner", "diag_target", "diag_load", "diag_entry",
        "diag_gp", "diag_sp", "diag_payload", "diag_unsupported",
        "diag_lowered", "diag_coverage", "diag_edges", "diag_unresolved",
        "diag_reachable", "diag_supported", "diag_embedded", "diag_unknown",
        "diag_first_gap", "diag_host_loop"
    };
    for (const char* label : diagnostics) {
        source << "mov rcx,r12\n";
        source << "lea rdx," << label << "\n";
        source << "mov r8d," << label << "_length\n";
        source << "mov r9,OFFSET written\n";
        source << "mov qword ptr [rsp+32],0\n";
        source << "call WriteFile\n";
    }

    source << "lea rsi,payload\n";
    source << "lea rdi,ram\n";
    source << "add rdi," << payload_ram_offset << "\n";
    source << "mov ecx," << file_size << "\n";
    source << "rep movsb\n";
    source << "mov dword ptr regs+116," << initial_sp << "\n";
    source << "mov dword ptr regs+112," << initial_gp << "\n";
    source << "lea r12,regs\n";
    if (emittable_leaders.find(initial_pc) != emittable_leaders.end()) {
        source << "jmp p" << std::hex << initial_pc << std::dec << "\n";
    } else {
        source << "jmp unsupported_exit\n";
    }

    auto emit_label_jump = [&](std::uint32_t target) {
        if (emittable_leaders.find(target) != emittable_leaders.end()) {
            source << "jmp p" << std::hex << target << std::dec << "\n";
        } else {
            source << "jmp unsupported_exit\n";
        }
    };

    for (std::uint32_t address : leaders) {
        if (emittable_leaders.find(address) == emittable_leaders.end()) continue;
        if (address < load_address ||
            address >= static_cast<std::uint64_t>(load_address) + file_size ||
            (address & 3u) != 0) continue;
        const std::uint32_t offset = address - load_address;
        if (offset + 4 > file_size) continue;
        const std::uint32_t instruction =
            read_psx32(data, PSX_HEADER_SIZE + offset);
        const std::uint32_t opcode = instruction >> 26;
        const std::uint32_t rs = (instruction >> 21) & 31u;
        const std::uint32_t rt = (instruction >> 16) & 31u;
        const std::uint32_t rd = (instruction >> 11) & 31u;
        const std::uint32_t shamt = (instruction >> 6) & 31u;
        const std::uint32_t immediate = instruction & 0xFFFFu;
        const std::uint32_t special = instruction & 63u;
        source << "p" << std::hex << address << std::dec << "::\n";
        source << "inc dword ptr host_cycles\n";
        source << "cmp dword ptr host_cycles,1024\n";
        source << "jb host_tick_done_" << std::hex << address << std::dec << "\n";
        source << "mov dword ptr host_cycles,0\n";
        source << "or dword ptr mmio_state+070h,4\n";
        source << "host_tick_done_" << std::hex << address << std::dec << ":\n";
        source << "mov al,byte ptr pending_load_valid\n";
        source << "mov byte ptr previous_load_valid,al\n";
        source << "mov al,byte ptr pending_load_register\n";
        source << "mov byte ptr previous_load_register,al\n";
        source << "mov eax,dword ptr pending_load_value\n";
        source << "mov dword ptr previous_load_value,eax\n";
        source << "mov byte ptr pending_load_valid,0\n";
        source << "mov dword ptr [r12],0\n";
        if (instruction == 0) {
            source << "cmp byte ptr previous_load_valid,0\n";
            source << "je p" << std::hex << address << std::dec << "_load_done\n";
            source << "movzx eax,byte ptr previous_load_register\n";
            source << "mov edx,dword ptr previous_load_value\n";
            source << "mov dword ptr [r12+rax*4],edx\n";
            source << "p" << std::hex << address << std::dec << "_load_done:\n";
            source << "mov dword ptr [r12],0\n";
            const std::uint32_t predecessor = address - 4;
            if (delay_slots.find(address) != delay_slots.end() &&
                predecessor >= load_address) {
                const std::uint32_t predecessor_instruction =
                    read_psx32(
                        data,
                        PSX_HEADER_SIZE + predecessor - load_address);
                const std::uint32_t predecessor_opcode =
                    predecessor_instruction >> 26;
                if (predecessor_opcode == 0x02 ||
                    predecessor_opcode == 0x03) {
                    const std::uint32_t target =
                        ((predecessor + 4) & 0xF0000000u) |
                        ((predecessor_instruction & 0x03FFFFFFu) << 2);
                    source << "cmp byte ptr delay_redirect_valid,0\n";
                    if (emittable_leaders.find(address + 4) !=
                        emittable_leaders.end()) {
                        source << "je p"
                               << std::hex << (address + 4) << std::dec << "\n";
                    } else {
                        source << "je unsupported_exit\n";
                    }
                    if (target >= load_address &&
                        target < static_cast<std::uint64_t>(load_address) + file_size &&
                        emittable_leaders.find(target) != emittable_leaders.end()) {
                        source << "jmp p"
                               << std::hex << target << std::dec << "\n";
                    } else {
                        source << "jmp unsupported_exit\n";
                    }
                } else if (predecessor_opcode == 0 &&
                           ((predecessor_instruction & 63u) == 0x08 ||
                            (predecessor_instruction & 63u) == 0x09)) {
                    source << "cmp byte ptr delay_redirect_valid,0\n";
                    if (emittable_leaders.find(address + 4) !=
                        emittable_leaders.end()) {
                        source << "je p"
                               << std::hex << (address + 4) << std::dec << "\n";
                    } else {
                        source << "je unsupported_exit\n";
                    }
                    source << "mov eax,dword ptr delay_redirect_address\n";
                    source << "jmp indirect_jump\n";
                } else if ((predecessor_opcode >= 0x04 &&
                     predecessor_opcode <= 0x07) ||
                    predecessor_opcode == 0x01 ||
                    predecessor_opcode == 0x11 ||
                    (predecessor_opcode >= 0x14 &&
                     predecessor_opcode <= 0x17)) {
                    const std::int32_t displacement =
                        static_cast<std::int32_t>(
                            static_cast<std::int16_t>(
                                predecessor_instruction & 0xFFFFu)) * 4;
                    const std::uint32_t target =
                        static_cast<std::uint32_t>(
                            static_cast<std::int64_t>(predecessor) +
                            4 + displacement);
                    source << "cmp byte ptr delay_redirect_valid,0\n";
                    if (emittable_leaders.find(address + 4) !=
                        emittable_leaders.end()) {
                        source << "je p"
                               << std::hex << (address + 4) << std::dec << "\n";
                    } else {
                        source << "je unsupported_exit\n";
                    }
                    if (target >= load_address &&
                        target < static_cast<std::uint64_t>(load_address) + file_size &&
                        emittable_leaders.find(target) != emittable_leaders.end()) {
                        source << "jmp p"
                               << std::hex << target << std::dec << "\n";
                    } else {
                        source << "jmp unsupported_exit\n";
                    }
                }
            }
            continue;
        }

        if (opcode == 0) {
            if (special == 0x00) {
                source << "mov eax,dword ptr [r12+" << rt * 4 << "]\n";
                source << "shl eax," << shamt << "\n";
                source << "mov dword ptr [r12+" << rd * 4 << "],eax\n";
            } else if (special == 0x02) {
                source << "mov eax,dword ptr [r12+" << rt * 4 << "]\n";
                source << "shr eax," << shamt << "\n";
                source << "mov dword ptr [r12+" << rd * 4 << "],eax\n";
            } else if (special == 0x03) {
                source << "mov eax,dword ptr [r12+" << rt * 4 << "]\n";
                source << "sar eax," << shamt << "\n";
                source << "mov dword ptr [r12+" << rd * 4 << "],eax\n";
            } else if (special == 0x04) {
                source << "mov ecx,dword ptr [r12+" << rs * 4 << "]\n";
                source << "mov eax,dword ptr [r12+" << rt * 4 << "]\n";
                source << "shl eax,cl\n";
                source << "mov dword ptr [r12+" << rd * 4 << "],eax\n";
            } else if (special == 0x06) {
                source << "mov ecx,dword ptr [r12+" << rs * 4 << "]\n";
                source << "mov eax,dword ptr [r12+" << rt * 4 << "]\n";
                source << "shr eax,cl\n";
                source << "mov dword ptr [r12+" << rd * 4 << "],eax\n";
            } else if (special == 0x07) {
                source << "mov ecx,dword ptr [r12+" << rs * 4 << "]\n";
                source << "mov eax,dword ptr [r12+" << rt * 4 << "]\n";
                source << "sar eax,cl\n";
                source << "mov dword ptr [r12+" << rd * 4 << "],eax\n";
            } else if (special == 0x18) {
                source << "mov eax,dword ptr [r12+" << rs * 4 << "]\n";
                source << "mov ecx,dword ptr [r12+" << rt * 4 << "]\n";
                source << "imul ecx\n";
                source << "mov dword ptr hi_lo,eax\n";
                source << "mov dword ptr hi_lo+4,edx\n";
            } else if (special == 0x19) {
                source << "mov eax,dword ptr [r12+" << rs * 4 << "]\n";
                source << "mov ecx,dword ptr [r12+" << rt * 4 << "]\n";
                source << "mul ecx\n";
                source << "mov dword ptr hi_lo,eax\n";
                source << "mov dword ptr hi_lo+4,edx\n";
            } else if (special == 0x1A || special == 0x1B) {
                source << "mov eax,dword ptr [r12+" << rs * 4 << "]\n";
                source << "mov ecx,dword ptr [r12+" << rt * 4 << "]\n";
                source << "test ecx,ecx\n";
                source << "je divide_zero_exit\n";
                if (special == 0x1A) source << "cdq\nidiv ecx\n";
                else source << "xor edx,edx\ndiv ecx\n";
                source << "mov dword ptr hi_lo,eax\n";
                source << "mov dword ptr hi_lo+4,edx\n";
            } else if (special == 0x10) {
                source << "mov eax,dword ptr hi_lo+4\n";
                source << "mov dword ptr [r12+" << rd * 4 << "],eax\n";
            } else if (special == 0x11) {
                source << "mov eax,dword ptr [r12+" << rs * 4 << "]\n";
                source << "mov dword ptr hi_lo+4,eax\n";
            } else if (special == 0x12) {
                source << "mov eax,dword ptr hi_lo\n";
                source << "mov dword ptr [r12+" << rd * 4 << "],eax\n";
            } else if (special == 0x13) {
                source << "mov eax,dword ptr [r12+" << rs * 4 << "]\n";
                source << "mov dword ptr hi_lo,eax\n";
            } else if (special == 0x20) {
                source << "mov eax,dword ptr [r12+" << rs * 4 << "]\n";
                source << "add eax,dword ptr [r12+" << rt * 4 << "]\n";
                source << "mov dword ptr [r12+" << rd * 4 << "],eax\n";
            } else if (special == 0x22) {
                source << "mov eax,dword ptr [r12+" << rs * 4 << "]\n";
                source << "sub eax,dword ptr [r12+" << rt * 4 << "]\n";
                source << "mov dword ptr [r12+" << rd * 4 << "],eax\n";
            } else if (special == 0x21 || special == 0x23 ||
                       special == 0x24 || special == 0x25 ||
                       special == 0x26 || special == 0x27 ||
                       special == 0x2A || special == 0x2B) {
                source << "mov eax,dword ptr [r12+" << rs * 4 << "]\n";
                if (special == 0x21) source << "add eax,dword ptr [r12+" << rt * 4 << "]\n";
                else if (special == 0x23) source << "sub eax,dword ptr [r12+" << rt * 4 << "]\n";
                else if (special == 0x24) source << "and eax,dword ptr [r12+" << rt * 4 << "]\n";
                else if (special == 0x25) source << "or eax,dword ptr [r12+" << rt * 4 << "]\n";
                else if (special == 0x26) source << "xor eax,dword ptr [r12+" << rt * 4 << "]\n";
                else if (special == 0x27) {
                    source << "or eax,dword ptr [r12+" << rt * 4 << "]\n";
                    source << "not eax\n";
                } else {
                    source << "cmp eax,dword ptr [r12+" << rt * 4 << "]\n";
                    if (special == 0x2A) source << "setl al\n";
                    else source << "setb al\n";
                    source << "movzx eax,al\n";
                }
                source << "mov dword ptr [r12+" << rd * 4 << "],eax\n";
            } else if (special == 0x08) {
                source << "mov eax,dword ptr [r12+" << rs * 4 << "]\n";
                source << "mov dword ptr delay_redirect_address,eax\n";
                source << "mov byte ptr delay_redirect_valid,1\n";
                source << "mov al,byte ptr previous_load_valid\n";
                source << "mov byte ptr pending_load_valid,al\n";
                source << "mov al,byte ptr previous_load_register\n";
                source << "mov byte ptr pending_load_register,al\n";
                source << "mov eax,dword ptr previous_load_value\n";
                source << "mov dword ptr pending_load_value,eax\n";
                emit_label_jump(address + 4);
            } else if (special == 0x09) {
                source << "mov dword ptr [r12+" << rd * 4 << "],"
                       << address + 8 << "\n";
                source << "mov eax,dword ptr [r12+" << rs * 4 << "]\n";
                source << "mov dword ptr delay_redirect_address,eax\n";
                source << "mov byte ptr delay_redirect_valid,1\n";
                source << "mov al,byte ptr previous_load_valid\n";
                source << "mov byte ptr pending_load_valid,al\n";
                source << "mov al,byte ptr previous_load_register\n";
                source << "mov byte ptr pending_load_register,al\n";
                source << "mov eax,dword ptr previous_load_value\n";
                source << "mov dword ptr pending_load_value,eax\n";
                emit_label_jump(address + 4);
            } else if (special == 0x0C) {
                source << "mov ecx,0\n";
                source << "call ExitProcess\n";
            } else if (special == 0x0D) {
                source << "mov ecx,9\n";
                source << "call ExitProcess\n";
            } else {
                source << "jmp unsupported_exit\n";
            }
        } else if (opcode == 0x08) {
            source << "mov eax,dword ptr [r12+" << rs * 4 << "]\n";
            source << "add eax," << static_cast<std::int32_t>(
                static_cast<std::int16_t>(immediate)) << "\n";
            source << "jo overflow_exit\n";
            source << "mov dword ptr [r12+" << rt * 4 << "],eax\n";
        } else if (opcode == 0x09) {
            source << "mov eax,dword ptr [r12+" << rs * 4 << "]\n";
            source << "add eax," << static_cast<std::int32_t>(
                static_cast<std::int16_t>(immediate)) << "\n";
            source << "mov dword ptr [r12+" << rt * 4 << "],eax\n";
        } else if (opcode == 0x0A) {
            source << "mov eax,dword ptr [r12+" << rs * 4 << "]\n";
            source << "cmp eax," << static_cast<std::int32_t>(
                static_cast<std::int16_t>(immediate)) << "\n";
            source << "setl al\n";
            source << "movzx eax,al\n";
            source << "mov dword ptr [r12+" << rt * 4 << "],eax\n";
        } else if (opcode == 0x0B) {
            source << "mov eax,dword ptr [r12+" << rs * 4 << "]\n";
            source << "cmp eax," << immediate << "\n";
            source << "setb al\n";
            source << "movzx eax,al\n";
            source << "mov dword ptr [r12+" << rt * 4 << "],eax\n";
        } else if (opcode == 0x0C) {
            source << "mov eax,dword ptr [r12+" << rs * 4 << "]\n";
            source << "and eax," << immediate << "\n";
            source << "mov dword ptr [r12+" << rt * 4 << "],eax\n";
        } else if (opcode == 0x0D) {
            source << "mov eax,dword ptr [r12+" << rs * 4 << "]\n";
            source << "or eax," << immediate << "\n";
            source << "mov dword ptr [r12+" << rt * 4 << "],eax\n";
        } else if (opcode == 0x0E) {
            source << "mov eax,dword ptr [r12+" << rs * 4 << "]\n";
            source << "xor eax," << immediate << "\n";
            source << "mov dword ptr [r12+" << rt * 4 << "],eax\n";
        } else if (opcode == 0x0F) {
            source << "mov dword ptr [r12+" << rt * 4 << "],"
                   << (immediate << 16) << "\n";
        } else if (opcode == 0x10) {
            const std::uint32_t cop0_register = rd;
            if (rs == 0 || rs == 2) {
                source << "mov eax,dword ptr cop0+" << cop0_register * 4 << "\n";
                source << "mov dword ptr [r12+" << rt * 4 << "],eax\n";
            } else if (rs == 4 || rs == 6) {
                source << "mov eax,dword ptr [r12+" << rt * 4 << "]\n";
                source << "mov dword ptr cop0+" << cop0_register * 4 << ",eax\n";
            } else if (rs == 16 && special == 0x10) {
                source << "mov eax,dword ptr cop0+48\n";
                source << "and eax,0FFFFFFFCh\n";
                source << "mov dword ptr cop0+48,eax\n";
            } else {
                source << "jmp unsupported_exit\n";
            }
        } else if (opcode == 0x11) {
            const std::uint32_t cop1_fd = rd;
            if (rs == 0) {
                source << "mov eax,dword ptr fregs+" << cop1_fd * 4 << "\n";
                source << "mov dword ptr [r12+" << rt * 4 << "],eax\n";
            } else if (rs == 2) {
                source << "mov eax,dword ptr fpcsr\n";
                source << "mov dword ptr [r12+" << rt * 4 << "],eax\n";
            } else if (rs == 4) {
                source << "mov eax,dword ptr [r12+" << rt * 4 << "]\n";
                source << "mov dword ptr fregs+" << cop1_fd * 4 << ",eax\n";
            } else if (rs == 6) {
                source << "mov eax,dword ptr [r12+" << rt * 4 << "]\n";
                source << "mov dword ptr fpcsr,eax\n";
            } else if (rs == 16) {
                const std::uint32_t ft = rt;
                const std::uint32_t fs = rd;
                const std::uint32_t fd = shamt;
                if (special == 0 || special == 1 ||
                    special == 2 || special == 3) {
                    source << "movss xmm0,dword ptr fregs+" << fs * 4 << "\n";
                    if (special == 0) source << "addss xmm0,dword ptr fregs+" << ft * 4 << "\n";
                    if (special == 1) source << "subss xmm0,dword ptr fregs+" << ft * 4 << "\n";
                    if (special == 2) source << "mulss xmm0,dword ptr fregs+" << ft * 4 << "\n";
                    if (special == 3) source << "divss xmm0,dword ptr fregs+" << ft * 4 << "\n";
                    source << "movss dword ptr fregs+" << fd * 4 << ",xmm0\n";
                } else if (special == 4) {
                    source << "sqrtss xmm0,dword ptr fregs+" << fs * 4 << "\n";
                    source << "movss dword ptr fregs+" << fd * 4 << ",xmm0\n";
                } else if (special == 5) {
                    source << "mov eax,dword ptr fregs+" << fs * 4 << "\n";
                    source << "and eax,7FFFFFFFh\n";
                    source << "mov dword ptr fregs+" << fd * 4 << ",eax\n";
                } else if (special == 6) {
                    source << "mov eax,dword ptr fregs+" << fs * 4 << "\n";
                    source << "mov dword ptr fregs+" << fd * 4 << ",eax\n";
                } else if (special == 7) {
                    source << "mov eax,dword ptr fregs+" << fs * 4 << "\n";
                    source << "xor eax,80000000h\n";
                    source << "mov dword ptr fregs+" << fd * 4 << ",eax\n";
                } else if (special >= 12 && special <= 15) {
                    const unsigned mode =
                        special == 12 ? 0u :
                        special == 14 ? 2u :
                        special == 15 ? 1u : 0u;
                    source << "roundss xmm0,dword ptr fregs+" << fs * 4 << "," << mode << "\n";
                    source << "cvttss2si eax,xmm0\n";
                    source << "mov dword ptr fregs+" << fd * 4 << ",eax\n";
                } else if (special == 0x20) {
                    source << "cvttss2si eax,dword ptr fregs+" << fs * 4 << "\n";
                    source << "mov dword ptr fregs+" << fd * 4 << ",eax\n";
                } else if (special == 0x24) {
                    source << "cvtsi2ss xmm0,dword ptr fregs+" << fs * 4 << "\n";
                    source << "movss dword ptr fregs+" << fd * 4 << ",xmm0\n";
                } else if (special >= 0x30) {
                    source << "mov eax,dword ptr fpcsr\n";
                    source << "and eax,0FFFFFFFEh\n";
                    if (special != 0x30) {
                        source << "movss xmm0,dword ptr fregs+" << fs * 4 << "\n";
                        source << "ucomiss xmm0,dword ptr fregs+" << ft * 4 << "\n";
                        if (special == 0x31 || special == 0x38 || special == 0x3A) {
                            source << "setp dl\n";
                        } else if (special == 0x32 || special == 0x35 ||
                                   special == 0x39) {
                            source << "sete dl\n";
                        } else if (special == 0x33 || special == 0x3B) {
                            source << "setnp dl\n";
                        } else if (special == 0x34 || special == 0x3C) {
                            source << "setb dl\n";
                        } else if (special == 0x36 || special == 0x3D) {
                            source << "setae dl\n";
                        } else if (special == 0x37 || special == 0x3E) {
                            source << "setbe dl\n";
                        } else if (special == 0x3F) {
                            source << "seta dl\n";
                        } else {
                            source << "xor dl,dl\n";
                        }
                        source << "movzx edx,dl\n";
                        source << "or eax,edx\n";
                    }
                    source << "mov dword ptr fpcsr,eax\n";
                } else {
                    source << "jmp unsupported_exit\n";
                }
            } else {
                source << "jmp unsupported_exit\n";
            }
        } else if (opcode == 0x12) {
            const std::uint32_t gte_register = rd;
            if (rs == 0) {
                source << "mov eax,dword ptr gte_data+" << gte_register * 4 << "\n";
                source << "mov dword ptr [r12+" << rt * 4 << "],eax\n";
            } else if (rs == 2) {
                source << "mov eax,dword ptr gte_ctrl+" << gte_register * 4 << "\n";
                source << "mov dword ptr [r12+" << rt * 4 << "],eax\n";
            } else if (rs == 4) {
                source << "mov eax,dword ptr [r12+" << rt * 4 << "]\n";
                source << "mov dword ptr gte_data+" << gte_register * 4 << ",eax\n";
            } else if (rs == 6) {
                source << "mov eax,dword ptr [r12+" << rt * 4 << "]\n";
                source << "mov dword ptr gte_ctrl+" << gte_register * 4 << ",eax\n";
            } else if (rs >= 16) {
                source << "mov ecx," << instruction << "\n";
                source << "lea rdx,gte_data\n";
                source << "lea r8,gte_ctrl\n";
                source << "call anyps1_gte_execute\n";
                source << "test eax,eax\n";
                source << "jz unsupported_exit\n";
            } else {
                source << "jmp unsupported_exit\n";
            }
        } else if (opcode == 0x13) {
            if (rs == 0 || rs == 2) {
                source << "mov eax,dword ptr cop3_data+" << rd * 4 << "\n";
                source << "mov dword ptr [r12+" << rt * 4 << "],eax\n";
            } else if (rs == 4 || rs == 6) {
                source << "mov eax,dword ptr [r12+" << rt * 4 << "]\n";
                source << "mov dword ptr cop3_data+" << rd * 4 << ",eax\n";
            } else {
                source << "jmp unsupported_exit\n";
            }
        } else if (opcode == 0x04 || opcode == 0x05 ||
                   opcode == 0x06 || opcode == 0x07 ||
                   opcode == 0x14 || opcode == 0x15 ||
                   opcode == 0x16 || opcode == 0x17 ||
                   opcode == 0x01 || opcode == 0x11) {
            const std::int32_t displacement =
                static_cast<std::int32_t>(
                    static_cast<std::int16_t>(immediate)) * 4;
            const std::uint32_t target =
                static_cast<std::uint32_t>(
                    static_cast<std::int64_t>(address) + 4 + displacement);
            const bool valid_target =
                target >= load_address &&
                target < static_cast<std::uint64_t>(load_address) + file_size &&
                emittable_leaders.find(target) != emittable_leaders.end();
            source << "mov byte ptr delay_redirect_valid,0\n";
            const std::string taken_label =
                "branch_taken_" + std::to_string(address);
            if (opcode == 0x01) {
                source << "cmp dword ptr [r12+" << rs * 4 << "],0\n";
                if ((rt & 1u) == 0) source << "jl " << taken_label << "\n";
                else source << "jge " << taken_label << "\n";
            } else if (opcode == 0x11) {
                source << "mov eax,dword ptr fpcsr\n";
                if (rs == 8) {
                    source << "test eax,1\n";
                    source << "jnz " << taken_label << "\n";
                } else if (rs == 12) {
                    source << "test eax,1\n";
                    source << "jz " << taken_label << "\n";
                }
            } else if (opcode == 0x04) {
                source << "mov eax,dword ptr [r12+" << rs * 4 << "]\n";
                source << "cmp eax,dword ptr [r12+" << rt * 4 << "]\n";
                source << "je " << taken_label << "\n";
            } else if (opcode == 0x05) {
                source << "mov eax,dword ptr [r12+" << rs * 4 << "]\n";
                source << "cmp eax,dword ptr [r12+" << rt * 4 << "]\n";
                source << "jne " << taken_label << "\n";
            } else if (opcode == 0x06) {
                source << "cmp dword ptr [r12+" << rs * 4 << "],0\n";
                source << "jle " << taken_label << "\n";
            } else if (opcode == 0x07) {
                source << "cmp dword ptr [r12+" << rs * 4 << "],0\n";
                source << "jg " << taken_label << "\n";
            } else if (opcode == 0x14) {
                source << "mov eax,dword ptr [r12+" << rs * 4 << "]\n";
                source << "cmp eax,dword ptr [r12+" << rt * 4 << "]\n";
                source << "je " << taken_label << "\n";
            } else if (opcode == 0x15) {
                source << "mov eax,dword ptr [r12+" << rs * 4 << "]\n";
                source << "cmp eax,dword ptr [r12+" << rt * 4 << "]\n";
                source << "jne " << taken_label << "\n";
            } else if (opcode == 0x16) {
                source << "cmp dword ptr [r12+" << rs * 4 << "],0\n";
                source << "jle " << taken_label << "\n";
            } else if (opcode == 0x17) {
                source << "cmp dword ptr [r12+" << rs * 4 << "],0\n";
                source << "jg " << taken_label << "\n";
            }
            source << "mov al,byte ptr previous_load_valid\n";
            source << "mov byte ptr pending_load_valid,al\n";
            source << "mov al,byte ptr previous_load_register\n";
            source << "mov byte ptr pending_load_register,al\n";
            source << "mov eax,dword ptr previous_load_value\n";
            source << "mov dword ptr pending_load_value,eax\n";
            emit_label_jump(address + 4);
            source << taken_label << ":\n";
            if (!valid_target) {
                source << "jmp unsupported_exit\n";
            } else {
                source << "mov byte ptr delay_redirect_valid,1\n";
                source << "mov al,byte ptr previous_load_valid\n";
                source << "mov byte ptr pending_load_valid,al\n";
                source << "mov al,byte ptr previous_load_register\n";
                source << "mov byte ptr pending_load_register,al\n";
                source << "mov eax,dword ptr previous_load_value\n";
                source << "mov dword ptr pending_load_value,eax\n";
                emit_label_jump(address + 4);
            }
        } else if (opcode == 0x02 || opcode == 0x03) {
            const std::uint32_t target =
                ((address + 4) & 0xF0000000u) |
                ((instruction & 0x03FFFFFFu) << 2);
            const bool valid_target =
                target >= load_address &&
                target < static_cast<std::uint64_t>(load_address) + file_size &&
                emittable_leaders.find(target) != emittable_leaders.end();
            if (opcode == 0x03) {
                source << "mov dword ptr [r12+124]," << address + 8 << "\n";
                const Ps1FileCallPlan file_call =
                    map_ps1_file_call(target);
                if (file_call.kind != Ps1FileCallKind::Unsupported &&
                    file_call.windows_function != nullptr) {
                    if (file_call.kind == Ps1FileCallKind::Close) {
                        source << "mov ecx,dword ptr [r12+16]\n";
                        source << "call CloseHandle\n";
                    } else if (file_call.kind == Ps1FileCallKind::Read) {
                        source << "mov ecx,dword ptr [r12+16]\n";
                        source << "mov eax,dword ptr [r12+20]\n";
                        source << "and eax,1FFFFFFFh\n";
                        source << "cmp eax,200000h\n";
                        source << "jae memory_fault_exit\n";
                        source << "mov r10d,eax\n";
                        source << "add r10d,dword ptr [r12+24]\n";
                        source << "cmp r10d,200000h\n";
                        source << "ja memory_fault_exit\n";
                        source << "lea rdx,ram\n";
                        source << "add rdx,rax\n";
                        source << "mov r8d,dword ptr [r12+24]\n";
                        source << "lea r9,written\n";
                        source << "call ReadFile\n";
                    } else if (file_call.kind == Ps1FileCallKind::Write) {
                        source << "mov ecx,dword ptr [r12+16]\n";
                        source << "mov eax,dword ptr [r12+20]\n";
                        source << "and eax,1FFFFFFFh\n";
                        source << "cmp eax,200000h\n";
                        source << "jae memory_fault_exit\n";
                        source << "mov r10d,eax\n";
                        source << "add r10d,dword ptr [r12+24]\n";
                        source << "cmp r10d,200000h\n";
                        source << "ja memory_fault_exit\n";
                        source << "lea rdx,ram\n";
                        source << "add rdx,rax\n";
                        source << "mov r8d,dword ptr [r12+24]\n";
                        source << "lea r9,written\n";
                        source << "call WriteFile\n";
                    } else if (file_call.kind == Ps1FileCallKind::Seek) {
                        source << "mov ecx,dword ptr [r12+16]\n";
                        source << "mov edx,dword ptr [r12+20]\n";
                        source << "xor r8d,r8d\n";
                        source << "call SetFilePointer\n";
                    } else {
                        source << "mov eax,dword ptr [r12+16]\n";
                        source << "and eax,1FFFFFFFh\n";
                        source << "cmp eax,200000h\n";
                        source << "jae memory_fault_exit\n";
                        source << "lea rcx,ram\n";
                        source << "add rcx,rax\n";
                        source << "mov edx,dword ptr [r12+20]\n";
                        source << "mov r8d,dword ptr [r12+24]\n";
                        source << "call CreateFileA\n";
                    }
                } else if (valid_target) {
                    source << "mov byte ptr delay_redirect_valid,1\n";
                    source << "mov al,byte ptr previous_load_valid\n";
                    source << "mov byte ptr pending_load_valid,al\n";
                    source << "mov al,byte ptr previous_load_register\n";
                    source << "mov byte ptr pending_load_register,al\n";
                    source << "mov eax,dword ptr previous_load_value\n";
                    source << "mov dword ptr pending_load_value,eax\n";
                    emit_label_jump(address + 4);
                } else {
                    source << "jmp unsupported_exit\n";
                }
            } else {
                if (!valid_target) {
                    source << "jmp unsupported_exit\n";
                } else {
                    source << "mov byte ptr delay_redirect_valid,1\n";
                source << "mov al,byte ptr previous_load_valid\n";
                source << "mov byte ptr pending_load_valid,al\n";
                source << "mov al,byte ptr previous_load_register\n";
                source << "mov byte ptr pending_load_register,al\n";
                source << "mov eax,dword ptr previous_load_value\n";
                source << "mov dword ptr pending_load_value,eax\n";
                    emit_label_jump(address + 4);
                }
            }
        } else if (opcode == 0x23 || opcode == 0x21 ||
                   opcode == 0x20 || opcode == 0x24 || opcode == 0x25 ||
                   opcode == 0x22 || opcode == 0x26 ||
                   opcode == 0x31 || opcode == 0x32) {
            const std::uint32_t access_size =
                opcode == 0x20 || opcode == 0x24 ? 1u :
                opcode == 0x21 || opcode == 0x25 ? 2u : 4u;
            source << "mov eax,dword ptr [r12+" << rs * 4 << "]\n";
            source << "add eax," << static_cast<std::int32_t>(
                static_cast<std::int16_t>(immediate)) << "\n";
            source << "and eax,1FFFFFFFh\n";
            source << "cmp eax,1F801000h\n";
            source << "jb host_address_continue_" << std::hex << address << std::dec << "\n";
            source << "cmp eax,1F802000h\n";
            source << "jae host_address_continue_" << std::hex << address << std::dec << "\n";
            source << "sub eax,1F801000h\n";
            source << "lea rbx,mmio_state\n";
            if (opcode == 0x20) source << "movsx edx,byte ptr [rbx+rax]\n";
            else if (opcode == 0x21) source << "movsx edx,word ptr [rbx+rax]\n";
            else if (opcode == 0x23) source << "mov edx,dword ptr [rbx+rax]\n";
            else if (opcode == 0x24) source << "movzx edx,byte ptr [rbx+rax]\n";
            else source << "movzx edx,word ptr [rbx+rax]\n";
            source << "mov dword ptr pending_load_value,edx\n";
            source << "mov byte ptr pending_load_register," << rt << "\n";
            source << "mov byte ptr pending_load_valid,1\n";
            source << "jmp mmio_load_complete_" << std::hex << address << std::dec << "\n";
            source << "host_address_continue_" << std::hex << address << std::dec << ":\n";
            source << "cmp eax,1F800000h\n";
            source << "jb ram_load_" << std::hex << address << std::dec << "\n";
            source << "cmp eax,1F800400h\n";
            source << "jae memory_fault_exit\n";
            source << "sub eax,1F800000h\n";
            source << "cmp eax," << (0x400u - access_size) << "\n";
            source << "ja memory_fault_exit\n";
            source << "lea rbx,scratchpad\n";
            source << "jmp load_region_" << std::hex << address << std::dec << "\n";
            source << "ram_load_" << std::hex << address << std::dec << ":\n";
            source << "cmp eax,800000h\n";
            source << "jae memory_fault_exit\n";
            source << "and eax,1FFFFFh\n";
            source << "cmp eax," << (0x200000u - access_size) << "\n";
            source << "ja memory_fault_exit\n";
            source << "lea rbx,ram\n";
            source << "load_region_" << std::hex << address << std::dec << ":\n";
            if (opcode == 0x22 || opcode == 0x26) {
                source << "mov r10d,eax\n";
                source << "and eax,0FFFFFFFCh\n";
                source << "mov edx,dword ptr [r12+" << rt * 4 << "]\n";
                source << "mov ecx,dword ptr [rbx+rax]\n";
                source << "and r10d,3\n";
                if (opcode == 0x22) {
                    source << "cmp r10d,0\nje lwl0_" << std::hex << address << std::dec << "\n";
                    source << "cmp r10d,1\nje lwl1_" << std::hex << address << std::dec << "\n";
                    source << "cmp r10d,2\nje lwl2_" << std::hex << address << std::dec << "\n";
                    source << "mov edx,ecx\njmp lwl_done_" << std::hex << address << std::dec << "\n";
                    source << "lwl0_" << std::hex << address << std::dec << ": shl ecx,24\nand edx,00FFFFFFh\nor edx,ecx\njmp lwl_done_" << std::hex << address << std::dec << "\n";
                    source << "lwl1_" << std::hex << address << std::dec << ": shl ecx,16\nand edx,0000FFFFh\nor edx,ecx\njmp lwl_done_" << std::hex << address << std::dec << "\n";
                    source << "lwl2_" << std::hex << address << std::dec << ": shl ecx,8\nand edx,000000FFh\nor edx,ecx\nlwl_done_" << std::hex << address << std::dec << ":\n";
                } else {
                    source << "cmp r10d,0\nje lwr0_" << std::hex << address << std::dec << "\n";
                    source << "cmp r10d,1\nje lwr1_" << std::hex << address << std::dec << "\n";
                    source << "cmp r10d,2\nje lwr2_" << std::hex << address << std::dec << "\n";
                    source << "shr ecx,24\nand edx,0FFFFFF00h\nor edx,ecx\njmp lwr_done_" << std::hex << address << std::dec << "\n";
                    source << "lwr0_" << std::hex << address << std::dec << ": mov edx,ecx\njmp lwr_done_" << std::hex << address << std::dec << "\n";
                    source << "lwr1_" << std::hex << address << std::dec << ": shr ecx,8\nand edx,0FF000000h\nor edx,ecx\njmp lwr_done_" << std::hex << address << std::dec << "\n";
                    source << "lwr2_" << std::hex << address << std::dec << ": shr ecx,16\nand edx,0FFFF0000h\nor edx,ecx\nlwr_done_" << std::hex << address << std::dec << ":\n";
                }
            } else if (opcode == 0x20) source << "movsx edx,byte ptr [rbx+rax]\n";
            else if (opcode == 0x21) source << "movsx edx,word ptr [rbx+rax]\n";
            else if (opcode == 0x23) source << "mov edx,dword ptr [rbx+rax]\n";
            else if (opcode == 0x24) source << "movzx edx,byte ptr [rbx+rax]\n";
            else if (opcode == 0x25) source << "movzx edx,word ptr [rbx+rax]\n";
            else if (opcode == 0x31 || opcode == 0x32) {
                source << "mov edx,dword ptr [rbx+rax]\n";
                source << "mov dword ptr fregs+" << rt * 4 << ",edx\n";
            }
            if (opcode == 0x31 || opcode == 0x32) {
                // LWC1/LWC2/ SWC2-style value transfers are represented in PS1 memory.
            } else {
                source << "mov dword ptr pending_load_value,edx\n";
                source << "mov byte ptr pending_load_register," << rt << "\n";
                source << "mov byte ptr pending_load_valid,1\n";
            }
            source << "mmio_load_complete_" << std::hex << address << std::dec << ":\n";
        } else if (opcode == 0x2B || opcode == 0x29 || opcode == 0x28 ||
                   opcode == 0x2E || opcode == 0x39 || opcode == 0x3A ||
                   opcode == 0x2A) {
            const std::uint32_t access_size =
                opcode == 0x28 ? 1u : opcode == 0x29 ? 2u : 4u;
            source << "mov eax,dword ptr [r12+" << rs * 4 << "]\n";
            source << "add eax," << static_cast<std::int32_t>(
                static_cast<std::int16_t>(immediate)) << "\n";
            source << "and eax,1FFFFFFFh\n";
            source << "cmp eax,1F801000h\n";
            source << "jb host_store_continue_" << std::hex << address << std::dec << "\n";
            source << "cmp eax,1F802000h\n";
            source << "jae host_store_continue_" << std::hex << address << std::dec << "\n";
            source << "sub eax,1F801000h\n";
            source << "lea rbx,mmio_state\n";
            source << "mov edx,dword ptr [r12+" << rt * 4 << "]\n";
            if (opcode == 0x28) source << "mov byte ptr [rbx+rax],dl\n";
            else if (opcode == 0x29) source << "mov word ptr [rbx+rax],dx\n";
            else source << "mov dword ptr [rbx+rax],edx\n";
            source << "jmp mmio_store_complete_" << std::hex << address << std::dec << "\n";
            source << "host_store_continue_" << std::hex << address << std::dec << ":\n";
            source << "cmp eax,1F800000h\n";
            source << "jb ram_store_" << std::hex << address << std::dec << "\n";
            source << "cmp eax,1F800400h\n";
            source << "jae memory_fault_exit\n";
            source << "sub eax,1F800000h\n";
            source << "cmp eax," << (0x400u - access_size) << "\n";
            source << "ja memory_fault_exit\n";
            source << "lea rbx,scratchpad\n";
            source << "jmp store_region_" << std::hex << address << std::dec << "\n";
            source << "ram_store_" << std::hex << address << std::dec << ":\n";
            source << "cmp eax,800000h\n";
            source << "jae memory_fault_exit\n";
            source << "and eax,1FFFFFh\n";
            source << "cmp eax," << (0x200000u - access_size) << "\n";
            source << "ja memory_fault_exit\n";
            source << "lea rbx,ram\n";
            source << "store_region_" << std::hex << address << std::dec << ":\n";
            source << "mov edx,dword ptr [r12+" << rt * 4 << "]\n";
            if (opcode == 0x2A) {
                source << "mov r10d,eax\n";
                source << "and eax,0FFFFFFFCh\n";
                source << "mov ecx,dword ptr [rbx+rax]\n";
                source << "and r10d,3\n";
                source << "cmp r10d,0\nje swl0_" << std::hex << address << std::dec << "\n";
                source << "cmp r10d,1\nje swl1_" << std::hex << address << std::dec << "\n";
                source << "cmp r10d,2\nje swl2_" << std::hex << address << std::dec << "\n";
                source << "mov ecx,edx\njmp swl_done_" << std::hex << address << std::dec << "\n";
                source << "swl0_" << std::hex << address << std::dec << ": and ecx,0FFFFFF00h\nmov r9d,edx\nshr r9d,24\nor ecx,r9d\njmp swl_done_" << std::hex << address << std::dec << "\n";
                source << "swl1_" << std::hex << address << std::dec << ": and ecx,0FFFF0000h\nmov r9d,edx\nshr r9d,16\nor ecx,r9d\njmp swl_done_" << std::hex << address << std::dec << "\n";
                source << "swl2_" << std::hex << address << std::dec << ": and ecx,0FF000000h\nmov r9d,edx\nshr r9d,8\nor ecx,r9d\nswl_done_" << std::hex << address << std::dec << ":\n";
                source << "mov dword ptr [rbx+rax],ecx\n";
            } else if (opcode == 0x2E) {
                source << "mov r10d,eax\n";
                source << "and eax,0FFFFFFFCh\n";
                source << "mov ecx,dword ptr [rbx+rax]\n";
                source << "and r10d,3\n";
                source << "cmp r10d,0\nje swr0_" << std::hex << address << std::dec << "\n";
                source << "cmp r10d,1\nje swr1_" << std::hex << address << std::dec << "\n";
                source << "cmp r10d,2\nje swr2_" << std::hex << address << std::dec << "\n";
                source << "and ecx,00FFFFFFh\nmov r9d,edx\nshl r9d,24\nor ecx,r9d\njmp swr_done_" << std::hex << address << std::dec << "\n";
                source << "swr0_" << std::hex << address << std::dec << ": mov ecx,edx\njmp swr_done_" << std::hex << address << std::dec << "\n";
                source << "swr1_" << std::hex << address << std::dec << ": and ecx,000000FFh\nmov r9d,edx\nshl r9d,8\nor ecx,r9d\njmp swr_done_" << std::hex << address << std::dec << "\n";
                source << "swr2_" << std::hex << address << std::dec << ": and ecx,0000FFFFh\nmov r9d,edx\nshl r9d,16\nor ecx,r9d\nswr_done_" << std::hex << address << std::dec << ":\n";
                source << "mov dword ptr [rbx+rax],ecx\n";
            } else if (opcode == 0x28) source << "mov byte ptr [rbx+rax],dl\n";
            else if (opcode == 0x29) source << "mov word ptr [rbx+rax],dx\n";
            else if (opcode == 0x39 || opcode == 0x3A) {
                source << "mov edx,dword ptr fregs+" << rt * 4 << "\n";
                source << "mov dword ptr [rbx+rax],edx\n";
            } else {
                source << "mov dword ptr [rbx+rax],edx\n";
            }
            source << "mmio_store_complete_" << std::hex << address << std::dec << ":\n";
        } else {
            source << "jmp unsupported_exit\n";
        }
        source << "cmp byte ptr previous_load_valid,0\n";
        source << "je p" << std::hex << address << std::dec << "_load_done\n";
        source << "movzx eax,byte ptr previous_load_register\n";
        source << "mov edx,dword ptr previous_load_value\n";
        source << "mov dword ptr [r12+rax*4],edx\n";
        source << "p" << std::hex << address << std::dec << "_load_done:\n";
        source << "mov dword ptr [r12],0\n";
        if (delay_slots.find(address) != delay_slots.end()) {
            const std::uint32_t predecessor = address - 4;
            if (predecessor >= load_address) {
                const std::uint32_t predecessor_instruction =
                    read_psx32(
                        data,
                        PSX_HEADER_SIZE + predecessor - load_address);
                const std::uint32_t predecessor_opcode =
                    predecessor_instruction >> 26;
                if (predecessor_opcode == 0x02 ||
                    predecessor_opcode == 0x03) {
                    const std::uint32_t target =
                        ((predecessor + 4) & 0xF0000000u) |
                        ((predecessor_instruction & 0x03FFFFFFu) << 2);
                    source << "cmp byte ptr delay_redirect_valid,0\n";
                    if (emittable_leaders.find(address + 4) !=
                        emittable_leaders.end()) {
                        source << "je p"
                               << std::hex << (address + 4) << std::dec << "\n";
                    } else {
                        source << "je unsupported_exit\n";
                    }
                    if (target >= load_address &&
                        target < static_cast<std::uint64_t>(load_address) + file_size &&
                        emittable_leaders.find(target) != emittable_leaders.end()) {
                        source << "jmp p"
                               << std::hex << target << std::dec << "\n";
                    } else {
                        source << "jmp unsupported_exit\n";
                    }
                } else if (predecessor_opcode == 0 &&
                           ((predecessor_instruction & 63u) == 0x08 ||
                            (predecessor_instruction & 63u) == 0x09)) {
                    source << "cmp byte ptr delay_redirect_valid,0\n";
                    if (emittable_leaders.find(address + 4) !=
                        emittable_leaders.end()) {
                        source << "je p"
                               << std::hex << (address + 4) << std::dec << "\n";
                    } else {
                        source << "je unsupported_exit\n";
                    }
                    source << "mov eax,dword ptr delay_redirect_address\n";
                    source << "jmp indirect_jump\n";
                } else if ((predecessor_opcode >= 0x04 &&
                     predecessor_opcode <= 0x07) ||
                    predecessor_opcode == 0x01 ||
                    predecessor_opcode == 0x11 ||
                    (predecessor_opcode >= 0x14 &&
                     predecessor_opcode <= 0x17)) {
                    const std::int32_t displacement =
                        static_cast<std::int32_t>(
                            static_cast<std::int16_t>(
                                predecessor_instruction & 0xFFFFu)) * 4;
                    const std::uint32_t target =
                        static_cast<std::uint32_t>(
                            static_cast<std::int64_t>(predecessor) +
                            4 + displacement);
                    source << "cmp byte ptr delay_redirect_valid,0\n";
                    if (emittable_leaders.find(address + 4) !=
                        emittable_leaders.end()) {
                        source << "je p"
                               << std::hex << (address + 4) << std::dec << "\n";
                    } else {
                        source << "je unsupported_exit\n";
                    }
                    if (target >= load_address &&
                        target < static_cast<std::uint64_t>(load_address) + file_size &&
                        emittable_leaders.find(target) != emittable_leaders.end()) {
                        source << "jmp p"
                               << std::hex << target << std::dec << "\n";
                    } else {
                        source << "jmp unsupported_exit\n";
                    }
                }
            }
        }
    }

    source << "jmp normal_exit\n";
    source << "divide_zero_exit:\n";
    source << "mov ecx,11\n";
    source << "call ExitProcess\n";
    source << "overflow_exit:\n";
    source << "mov ecx,12\n";
    source << "call ExitProcess\n";
    source << "unsupported_host_exit:\n";
    source << "mov ecx,17\n";
    source << "call ExitProcess\n";
    source << "memory_fault_exit:\n";
    source << "mov ecx,13\n";
    source << "call ExitProcess\n";
    source << "normal_exit:\n";
    source << "lea rdx,diag_completed\n";
    source << "mov r8d,diag_completed_length\n";
    source << "mov rcx,r12\n";
    source << "mov r9,OFFSET written\n";
    source << "mov qword ptr [rsp+32],0\n";
    source << "call WriteFile\n";
    source << "mov ecx,0\n";
    source << "call ExitProcess\n";
    source << "main ENDP\n";
    source << "END\n";
    source.flush();
    source.close();

    const std::filesystem::path ml64 = find_windows_tool("ml64.exe");
    const std::filesystem::path link = find_windows_tool("link.exe");
    std::string ml64_arguments =
        "/nologo /c /Fo\"" + object.string() +
        "\" \"" + assembly.string() + "\"";
    if (!run_windows_tool(ml64, ml64_arguments)) {
        throw std::runtime_error(
            "native Windows lowering assembly compilation failed");
    }
    std::string link_arguments =
        "/NOLOGO /ENTRY:main /SUBSYSTEM:CONSOLE "
        "/INCREMENTAL:NO /MERGE:.pdata=.rdata /OUT:\"" +
        options.output + "\" \"" + object.string() + "\"";
    const std::filesystem::path gte_library =
        std::filesystem::path(ANYPS1_BUILD_DIR) / "src" /
        "Debug" / "anyps1_gte.lib";
    if (!std::filesystem::is_regular_file(gte_library)) {
        throw std::runtime_error(
            "cannot locate native Windows GTE runtime library");
    }
    link_arguments += " \"" + gte_library.string() + "\"";
    for (const std::filesystem::path& library_dir :
         find_windows_library_dirs()) {
        link_arguments += " /LIBPATH:\"" + library_dir.string() + "\"";
    }
    if (!run_windows_tool(link, link_arguments)) {
        throw std::runtime_error(
            "native Windows lowering linker failed");
    }
    char* keep_assembly = nullptr;
    size_t keep_assembly_length = 0;
    const bool keep =
        _dupenv_s(
            &keep_assembly,
            &keep_assembly_length,
            "ANYPS1_KEEP_ASSEMBLY") == 0 &&
        keep_assembly != nullptr;
    std::free(keep_assembly);
    if (!keep) {
        std::filesystem::remove(assembly);
        std::filesystem::remove(object);
    }
#endif
}

}
