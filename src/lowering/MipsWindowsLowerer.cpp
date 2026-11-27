#include "MipsWindowsLowerer.h"
#include "relinker/Relinker.h"
#include "format/PsExe.h"
#include "UnsupportedInstructions.h"
#include "executable/PeWriter.h"
#include "host/HostServices.h"

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
    for (unsigned char character : value) {
        if (!first) source << ',';
        source << static_cast<unsigned int>(character);
        first = false;
    }
    if (!first) source << ',';
    source << "0\n";
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

    std::set<std::uint32_t> leaders;
    std::set<std::uint32_t> delay_slots;
    std::set<std::uint32_t> processed;
    std::queue<std::uint32_t> work;
    leaders.insert(initial_pc);
    work.push(initial_pc);
    std::uint32_t unsupported_count = 0;
    std::uint32_t first_unsupported_address = 0;
    std::uint32_t first_unsupported_word = 0;
    std::string first_unsupported_name;

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
        const bool branch = opcode == 0x01 ||
            opcode == 0x04 || opcode == 0x05 ||
            opcode == 0x06 || opcode == 0x07 ||
            opcode == 0x14 || opcode == 0x15 ||
            opcode == 0x16 || opcode == 0x17;
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
            leaders.insert(target);
            leaders.insert(address + 8);
            work.push(target);
            work.push(address + 8);
        } else if (!jr && !jalr && !stop) {
            leaders.insert(address + 4);
            work.push(address + 4);
        }
        if (branch || jump || jr || jalr) {
            const std::uint32_t delay_address = address + 4;
            if (delay_address >= load_address &&
                delay_address < static_cast<std::uint64_t>(load_address) + file_size) {
                delay_slots.insert(delay_address);
                leaders.insert(delay_address);
                work.push(delay_address);
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
        if (!is_supported_mips_instruction(instruction)) {
            ++unsupported_count;
            if (first_unsupported_address == 0) {
                first_unsupported_address = address;
                first_unsupported_word = instruction;
                first_unsupported_name =
                    classify_unsupported_instruction(instruction).name;
            }
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
    source << "cop0 db 128 dup(0)\n";
    source << "fregs dd 32 dup(0)\n";
    source << "fpcsr dd 0\n";
    source << "written dq 0\n";

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
    source << "main PROC\n";
    source << "sub rsp,40\n";
    source << "mov ecx,-11\n";
    source << "call GetStdHandle\n";
    source << "mov r12,rax\n";

    const char* diagnostics[] = {
        "diag_banner", "diag_target", "diag_load", "diag_entry",
        "diag_gp", "diag_sp", "diag_payload", "diag_unsupported",
        "diag_first_gap", "diag_host_loop"
    };
    for (const char* label : diagnostics) {
        source << "mov rcx,r12\n";
        source << "lea rdx," << label << "\n";
        source << "mov r8d,LENGTHOF " << label << "\n";
        source << "mov r9,OFFSET written\n";
        source << "mov qword ptr [rsp+32],0\n";
        source << "call WriteFile\n";
    }

    if (unsupported_count != 0) {
        source << "mov ecx,16\n";
    } else if (opcode == 0x2F) {
        source << "nop\n";
    } else {
        source << "mov ecx,0\n";
    }
    source << "call ExitProcess\n";

    source << "lea rsi,payload\n";
    source << "lea rdi,ram\n";
    source << "mov ecx," << file_size << "\n";
    source << "rep movsb\n";
    source << "mov dword ptr regs+116," << initial_sp << "\n";
    source << "mov dword ptr regs+112," << initial_gp << "\n";
    source << "lea r12,regs\n";
    source << "jmp p" << std::hex << initial_pc << std::dec << "\n";

    for (std::uint32_t address : leaders) {
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
        source << "p" << std::hex << address << std::dec << ":\n";
        if (instruction == 0) continue;

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
                for (std::uint32_t target : leaders) {
                    if (target >= load_address &&
                        target < static_cast<std::uint64_t>(load_address) + file_size) {
                        source << "cmp eax," << target << "\n";
                        source << "je p" << std::hex << target << std::dec << "\n";
                    }
                }
                source << "jmp unsupported_exit\n";
            } else if (special == 0x09) {
                source << "mov dword ptr [r12+" << rd * 4 << "],"
                       << address + 8 << "\n";
                source << "jmp unsupported_exit\n";
            } else if (special == 0x0C) {
                source << "mov ecx,0\n";
                source << "call ExitProcess\n";
            } else if (special == 0x0D) {
                source << "mov ecx,9\n";
                source << "call ExitProcess\n";
            } else {
                source << "jmp unsupported_exit\n";
            }
        } else if (opcode == 0x09) {
            source << "mov eax,dword ptr [r12+" << rs * 4 << "]\n";
            source << "add eax," << static_cast<std::int32_t>(
                static_cast<std::int16_t>(immediate)) << "\n";
            source << "mov dword ptr [r12+" << rt * 4 << "],eax\n";
        } else if (opcode == 0x0D) {
            source << "mov eax,dword ptr [r12+" << rs * 4 << "]\n";
            source << "or eax," << immediate << "\n";
            source << "mov dword ptr [r12+" << rt * 4 << "],eax\n";
        } else if (opcode == 0x0F) {
            source << "mov dword ptr [r12+" << rt * 4 << "],"
                   << (immediate << 16) << "\n";
        } else if (opcode == 0x04 || opcode == 0x05 ||
                   opcode == 0x06 || opcode == 0x07 ||
                   opcode == 0x14 || opcode == 0x15 ||
                   opcode == 0x16 || opcode == 0x17) {
            const std::int32_t displacement =
                static_cast<std::int32_t>(static_cast<std::int16_t>(immediate)) * 4;
            const std::uint32_t target =
                static_cast<std::uint32_t>(
                    static_cast<std::int64_t>(address) + 4 + displacement);
            if (opcode == 0x04) {
                source << "mov eax,dword ptr [r12+" << rs * 4 << "]\n";
                source << "cmp eax,dword ptr [r12+" << rt * 4 << "]\n";
                source << "je p" << std::hex << target << std::dec << "\n";
            } else if (opcode == 0x05) {
                source << "mov eax,dword ptr [r12+" << rs * 4 << "]\n";
                source << "cmp eax,dword ptr [r12+" << rt * 4 << "]\n";
                source << "jne p" << std::hex << target << std::dec << "\n";
            } else if (opcode == 0x06) {
                source << "cmp dword ptr [r12+" << rs * 4 << "],0\n";
                source << "jle p" << std::hex << target << std::dec << "\n";
            } else if (opcode == 0x07) {
                source << "cmp dword ptr [r12+" << rs * 4 << "],0\n";
                source << "jg p" << std::hex << target << std::dec << "\n";
            } else if (opcode == 0x14) {
                source << "mov eax,dword ptr [r12+" << rs * 4 << "]\n";
                source << "cmp eax,dword ptr [r12+" << rt * 4 << "]\n";
                source << "je p" << std::hex << target << std::dec << "\n";
            } else if (opcode == 0x15) {
                source << "mov eax,dword ptr [r12+" << rs * 4 << "]\n";
                source << "cmp eax,dword ptr [r12+" << rt * 4 << "]\n";
                source << "jne p" << std::hex << target << std::dec << "\n";
            } else if (opcode == 0x16) {
                source << "cmp dword ptr [r12+" << rs * 4 << "],0\n";
                source << "jle p" << std::hex << target << std::dec << "\n";
            } else if (opcode == 0x17) {
                source << "cmp dword ptr [r12+" << rs * 4 << "],0\n";
                source << "jg p" << std::hex << target << std::dec << "\n";
            }
        } else if (opcode == 0x02 || opcode == 0x03) {
            const std::uint32_t target =
                ((address + 4) & 0xF0000000u) |
                ((instruction & 0x03FFFFFFu) << 2);
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
                        source << "mov rdx,dword ptr [r12+20]\n";
                        source << "mov r8d,dword ptr [r12+24]\n";
                        source << "lea r9,written\n";
                        source << "call ReadFile\n";
                    } else if (file_call.kind == Ps1FileCallKind::Write) {
                        source << "mov ecx,dword ptr [r12+16]\n";
                        source << "mov rdx,dword ptr [r12+20]\n";
                        source << "mov r8d,dword ptr [r12+24]\n";
                        source << "lea r9,written\n";
                        source << "call WriteFile\n";
                    } else if (file_call.kind == Ps1FileCallKind::Seek) {
                        source << "mov ecx,dword ptr [r12+16]\n";
                        source << "mov edx,dword ptr [r12+20]\n";
                        source << "xor r8d,r8d\n";
                        source << "call SetFilePointer\n";
                    } else {
                        source << "mov ecx,dword ptr [r12+16]\n";
                        source << "mov edx,dword ptr [r12+20]\n";
                        source << "mov r8d,dword ptr [r12+24]\n";
                        source << "call CreateFileA\n";
                    }
                } else {
                    source << "call p" << std::hex << target << std::dec << "\n";
                }
            } else {
                source << "jmp p" << std::hex << target << std::dec << "\n";
            }
        } else if (opcode == 0x23 || opcode == 0x21 ||
                   opcode == 0x20 || opcode == 0x24 || opcode == 0x25) {
            source << "mov eax,dword ptr [r12+" << rs * 4 << "]\n";
            source << "add eax," << static_cast<std::int32_t>(
                static_cast<std::int16_t>(immediate)) << "\n";
            source << "and eax,01FFFFFh\n";
            source << "lea rbx,ram\n";
            if (opcode == 0x20) source << "movsx edx,byte ptr [rbx+rax]\n";
            else if (opcode == 0x21) source << "movsx edx,word ptr [rbx+rax]\n";
            else if (opcode == 0x23) source << "mov edx,dword ptr [rbx+rax]\n";
            else if (opcode == 0x24) source << "movzx edx,byte ptr [rbx+rax]\n";
            else source << "movzx edx,word ptr [rbx+rax]\n";
            source << "mov dword ptr [r12+" << rt * 4 << "],edx\n";
        } else if (opcode == 0x2B || opcode == 0x29 || opcode == 0x28) {
            source << "mov eax,dword ptr [r12+" << rs * 4 << "]\n";
            source << "add eax," << static_cast<std::int32_t>(
                static_cast<std::int16_t>(immediate)) << "\n";
            source << "and eax,01FFFFFh\n";
            source << "lea rbx,ram\n";
            source << "mov edx,dword ptr [r12+" << rt * 4 << "]\n";
            if (opcode == 0x28) source << "mov byte ptr [rbx+rax],dl\n";
            else if (opcode == 0x29) source << "mov word ptr [rbx+rax],dx\n";
            else source << "mov dword ptr [rbx+rax],edx\n";
        } else {
            source << "jmp unsupported_exit\n";
        }
    }

    source << "normal_exit:\n";
    source << "lea rdx,diag_completed\n";
    source << "mov r8d,LENGTHOF diag_completed\n";
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
        "/INCREMENTAL:NO /OUT:\"" +
        options.output + "\" \"" + object.string() + "\"";
    for (const std::filesystem::path& library_dir :
         find_windows_library_dirs()) {
        link_arguments += " /LIBPATH:\"" + library_dir.string() + "\"";
    }
    if (!run_windows_tool(link, link_arguments)) {
        throw std::runtime_error(
            "native Windows lowering linker failed");
    }
    std::filesystem::remove(assembly);
    std::filesystem::remove(object);
#endif
}

}
