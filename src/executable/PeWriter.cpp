#include "PeWriter.h"
#include "lowering/NativeLoweringIr.h"
#include "relinker/Relinker.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#else
#include <cerrno>
#include <cstring>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace anyps1 {

namespace {

#ifdef _WIN32
std::string environment_value(const char* name)
{
    char* value = nullptr;
    size_t length = 0;
    if (_dupenv_s(&value, &length, name) != 0 || value == nullptr) {
        return {};
    }
    std::string result(value);
    std::free(value);
    return result;
}

bool run_windows_tool(
    const std::filesystem::path& tool,
    const std::vector<std::wstring>& arguments)
{
    std::wstring command_line = L"\"" + tool.wstring() + L"\"";
    for (const std::wstring& argument : arguments) {
        command_line += L" ";
        if (argument.find(L' ') != std::wstring::npos) {
            command_line += L"\"" + argument + L"\"";
        } else {
            command_line += argument;
        }
    }

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
    const DWORD wait_result =
        WaitForSingleObject(process.hProcess, 120000u);
    if (wait_result == WAIT_TIMEOUT) {
        TerminateProcess(process.hProcess, 124u);
        WaitForSingleObject(process.hProcess, INFINITE);
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
        return false;
    }
    DWORD exit_code = 0;
    GetExitCodeProcess(process.hProcess, &exit_code);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return exit_code == 0;
}
#endif

#ifndef _WIN32
bool run_posix_tool(
    const char* tool,
    const std::vector<std::string>& arguments)
{
#ifndef _WIN32
    const pid_t child = fork();
    if (child < 0) {
        throw std::runtime_error(
            std::string("cannot start ") + tool + ": " +
            std::string(std::strerror(errno)));
    }
    if (child == 0) {
        std::vector<const char*> argv;
        argv.reserve(arguments.size() + 2);
        argv.push_back(tool);
        for (const std::string& argument : arguments) {
            argv.push_back(argument.c_str());
        }
        argv.push_back(nullptr);
        execvp(tool, const_cast<char* const*>(argv.data()));
        _exit(127);
    }

    int status = 0;
    if (waitpid(child, &status, 0) < 0) {
        throw std::runtime_error(
            std::string("cannot wait for ") + tool);
    }
    return WIFEXITED(status) && WEXITSTATUS(status) == 0;
#else
    (void)tool;
    (void)arguments;
    return false;
#endif
}
#endif

}

void write_pe_layout(
    const RelinkOptions&,
    const std::vector<unsigned char>&,
    std::uint32_t,
    std::uint32_t)
{
    throw std::runtime_error(
        "manual PE layout lowering is unsupported; use the native lowering IR");
}

void write_pe_ir(
    const RelinkOptions& options,
    const NativeLoweringIr& ir)
{
    if (ir.target != TargetPlatform::Windows) {
        throw std::runtime_error(
            "cannot emit a Windows PE image from a non-Windows lowering IR");
    }
    if (ir.source.empty()) {
        throw std::runtime_error(
            "native Windows lowering IR has no source");
    }

    std::vector<std::filesystem::path> sources;
    std::vector<std::filesystem::path> objects;
    if (ir.chunks.empty()) {
        sources.push_back(
            std::filesystem::path(options.output + ".native-pe.cpp"));
        objects.push_back(
            std::filesystem::path(options.output + ".native-pe.obj"));
    } else {
        sources.reserve(ir.chunks.size());
        objects.reserve(ir.chunks.size());
        for (std::size_t index = 0; index < ir.chunks.size(); ++index) {
            sources.push_back(std::filesystem::path(
                options.output + ".native-pe." +
                std::to_string(index) + ".cpp"));
            objects.push_back(std::filesystem::path(
                options.output + ".native-pe." +
                std::to_string(index) + ".obj"));
        }
    }
    for (std::size_t index = 0; index < sources.size(); ++index) {
        std::ofstream source_output(sources[index], std::ios::trunc);
        if (!source_output) {
            throw std::runtime_error(
                "cannot create native Windows lowering source");
        }
        source_output << (ir.chunks.empty()
                              ? ir.source
                              : ir.chunks[index]);
        source_output.close();
    }

#ifdef _WIN32
    const std::string clang_environment = environment_value("ANYPS1_CLANG_CL");
    const std::string lld_environment = environment_value("ANYPS1_LLD_LINK");
    const std::filesystem::path clang =
        std::filesystem::path(
            clang_environment.empty() ? "clang-cl" : clang_environment);
    const std::filesystem::path lld =
        std::filesystem::path(
            lld_environment.empty() ? "lld-link" : lld_environment);

    for (std::size_t index = 0; index < sources.size(); ++index) {
        std::vector<std::wstring> compile_arguments{
            L"/nologo",
            L"/c",
            L"/O2",
            L"/GS-",
            L"/GR-",
            L"/EHs-c-",
            L"/Fo:" + objects[index].wstring(),
            sources[index].wstring(),
        };
        if (!run_windows_tool(clang, compile_arguments)) {
            throw std::runtime_error(
                "native Windows lowering Clang compilation failed");
        }
    }

    std::vector<std::wstring> link_arguments{
        L"/nologo",
        L"/NODEFAULTLIB",
        L"/SUBSYSTEM:CONSOLE",
        L"/ENTRY:" + std::wstring(ir.entry_symbol.begin(), ir.entry_symbol.end()),
        L"/BASE:0x140000000",
        L"/OUT:" + std::filesystem::path(options.output).wstring(),
    };
    for (const std::filesystem::path& object : objects) {
        link_arguments.push_back(object.wstring());
    }
    link_arguments.push_back(L"kernel32.lib");
    if (!run_windows_tool(lld, link_arguments)) {
        throw std::runtime_error(
            "native Windows lowering LLD link failed");
    }
#else
    for (std::size_t index = 0; index < sources.size(); ++index) {
        if (!run_posix_tool(
                "clang++",
                {
                    "--target=x86_64-pc-windows-msvc",
                    "-c",
                    "-O2",
                    "-o",
                    objects[index].string(),
                    sources[index].string(),
                })) {
            throw std::runtime_error(
                "native Windows lowering Clang compilation failed");
        }
    }
    std::vector<std::string> posix_link_arguments{
        "/nologo",
        "/NODEFAULTLIB",
        "/SUBSYSTEM:CONSOLE",
        "/ENTRY:" + ir.entry_symbol,
        "/BASE:0x140000000",
        "/OUT:" + options.output,
    };
    for (const std::filesystem::path& object : objects) {
        posix_link_arguments.push_back(object.string());
    }
    if (!run_posix_tool("lld-link", posix_link_arguments)) {
        throw std::runtime_error(
            "native Windows lowering LLD link failed");
    }
#endif

#ifdef _WIN32
    const std::string keep_value =
        environment_value("ANYPS1_KEEP_ASSEMBLY");
#else
    const char* keep_environment = std::getenv("ANYPS1_KEEP_ASSEMBLY");
    const std::string keep_value =
        keep_environment == nullptr ? "" : keep_environment;
#endif
    if (keep_value.empty()) {
        std::error_code remove_error;
        for (const std::filesystem::path& source_path : sources) {
            std::filesystem::remove(source_path, remove_error);
        }
        for (const std::filesystem::path& object_path : objects) {
            std::filesystem::remove(object_path, remove_error);
        }
    }
}

}
