#include "PeWriter.h"
#include "NativeProcess.h"
#include "lowering/NativeLoweringIr.h"
#include "relinker/Relinker.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <chrono>
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
    const std::vector<std::wstring>& arguments,
    std::string& error)
{
    const NativeProcessResult result = run_native_process(
        tool,
        arguments,
        std::chrono::minutes(2));
    if (result.timed_out) error = "native Windows cross-tool timed out";
    else if (!result.stderr_text.empty()) error = result.stderr_text;
    if (result.timed_out) error = "native Windows tool timed out";
    else if (!result.stderr_text.empty()) error = result.stderr_text;
    return result.started &&
        !result.timed_out &&
        result.exit_code == 0;
}
#endif

#ifndef _WIN32
bool run_posix_tool(
    const char* tool,
    const std::vector<std::string>& arguments,
    std::string& error)
{
    const NativeProcessResult result = run_native_process(
        tool,
        arguments,
        std::chrono::minutes(2));
    if (!result.started) {
        error = "tool could not be started";
    } else if (result.timed_out) {
        error = "tool timed out";
    } else if (!result.stderr_text.empty()) {
        error = result.stderr_text;
    } else if (!result.stdout_text.empty()) {
        error = result.stdout_text;
    }
    return result.started &&
        !result.timed_out &&
        result.exit_code == 0;
}
#endif

}

void write_pe_ir(
    const RelinkOptions& options,
    const NativeLoweringIr& ir)
{
    if (ir.target != TargetPlatform::Windows) {
        throw std::runtime_error(
            "cannot emit a Windows PE image from a non-Windows lowering IR");
    }
    if (ir.chunks.empty()) {
        throw std::runtime_error(
            "native Windows lowering IR has no chunk sources");
    }

    std::vector<std::filesystem::path> sources;
    std::vector<std::filesystem::path> objects;
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
    for (std::size_t index = 0; index < sources.size(); ++index) {
        std::ofstream source_output(sources[index], std::ios::trunc);
        if (!source_output) {
            throw std::runtime_error(
                "cannot create native Windows lowering source");
        }
        source_output << ir.chunks[index];
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
        std::string tool_error;
        tool_error.clear();
        if (!run_windows_tool(clang, compile_arguments, tool_error)) {
            throw std::runtime_error(
                "native Windows lowering Clang compilation failed" +
                    (tool_error.empty()
                         ? std::string{}
                         : std::string(": ") + tool_error));
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
    std::string tool_error;
    if (!run_windows_tool(lld, link_arguments, tool_error)) {
        throw std::runtime_error(
            "native Windows lowering LLD link failed" +
                (tool_error.empty()
                     ? std::string{}
                     : std::string(": ") + tool_error));
    }
#else
    const std::filesystem::path kernel32_def =
        std::filesystem::path(options.output + ".kernel32.def");
    const std::filesystem::path kernel32_import =
        std::filesystem::path(options.output + ".kernel32.lib");
    {
        std::ofstream def_output(kernel32_def, std::ios::trunc);
        if (!def_output) {
            throw std::runtime_error(
                "cannot create kernel32 import definition");
        }
        def_output << "LIBRARY kernel32.dll\n";
        def_output << "EXPORTS\n";
        def_output << "GetStdHandle\n";
        def_output << "WriteFile\n";
        def_output << "ExitProcess\n";
        def_output.close();
    }
    std::string tool_error;
    if (!run_posix_tool(
            "llvm-dlltool",
            {
                "-m",
                "i386:x86-64",
                "-d",
                kernel32_def.string(),
                "-l",
                kernel32_import.string(),
            },
            tool_error)) {
        throw std::runtime_error(
            "cannot create kernel32 import library" +
                (tool_error.empty()
                     ? std::string{}
                     : std::string(": ") + tool_error));
    }
    for (std::size_t index = 0; index < sources.size(); ++index) {
        std::string tool_error;
        if (!run_posix_tool(
                "clang++",
                {
                    "--target=x86_64-pc-windows-msvc",
                    "-c",
                    "-O2",
                    "-o",
                    objects[index].string(),
                    sources[index].string(),
                },
                tool_error)) {
            throw std::runtime_error(
                "native Windows lowering Clang compilation failed" +
                    (tool_error.empty()
                         ? std::string{}
                         : std::string(": ") + tool_error));
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
    posix_link_arguments.push_back(kernel32_import.string());
    tool_error.clear();
    if (!run_posix_tool("lld-link", posix_link_arguments, tool_error)) {
        throw std::runtime_error(
            "native Windows lowering LLD link failed" +
                (tool_error.empty()
                     ? std::string{}
                     : std::string(": ") + tool_error));
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
            if (remove_error) {
                throw std::runtime_error(
                    "cannot remove temporary PE source: " +
                    source_path.string() + ": " +
                    remove_error.message());
            }
        }
        for (const std::filesystem::path& object_path : objects) {
            std::filesystem::remove(object_path, remove_error);
            if (remove_error) {
                throw std::runtime_error(
                    "cannot remove temporary PE object: " +
                    object_path.string() + ": " +
                    remove_error.message());
            }
        }
        std::filesystem::remove(
            std::filesystem::path(options.output + ".kernel32.def"),
            remove_error);
        if (remove_error) {
            throw std::runtime_error(
                "cannot remove temporary kernel32 definition");
        }
        std::filesystem::remove(
            std::filesystem::path(options.output + ".kernel32.lib"),
            remove_error);
        if (remove_error) {
            throw std::runtime_error(
                "cannot remove temporary kernel32 import library");
        }
    }
}

}
