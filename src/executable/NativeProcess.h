#pragma once

#include <chrono>
#include <filesystem>
#include <string>
#include <vector>

namespace anyps1 {

struct NativeProcessResult {
    bool started = false;
    bool timed_out = false;
    int exit_code = -1;
    std::string stdout_text;
    std::string stderr_text;
};

#ifndef _WIN32
NativeProcessResult run_native_process(
    const char* executable,
    const std::vector<std::string>& arguments,
    std::chrono::milliseconds timeout);
#else
NativeProcessResult run_native_process(
    const std::filesystem::path& executable,
    const std::vector<std::wstring>& arguments,
    std::chrono::milliseconds timeout);
#endif

}
