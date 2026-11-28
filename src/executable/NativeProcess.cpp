#include "NativeProcess.h"

#ifndef _WIN32
#include <cerrno>
#include <cstring>
#include <csignal>
#include <fcntl.h>
#include <poll.h>
#include <sys/wait.h>
#include <unistd.h>
#else
#include <windows.h>
#endif

namespace anyps1 {

#ifndef _WIN32
NativeProcessResult run_native_process(
    const char* executable,
    const std::vector<std::string>& arguments,
    std::chrono::milliseconds timeout)
{
    NativeProcessResult result;
    int stdout_pipe[2]{};
    int stderr_pipe[2]{};
    if (pipe(stdout_pipe) != 0 || pipe(stderr_pipe) != 0) {
        if (stdout_pipe[0] != 0) close(stdout_pipe[0]);
        if (stdout_pipe[1] != 0) close(stdout_pipe[1]);
        if (stderr_pipe[0] != 0) close(stderr_pipe[0]);
        if (stderr_pipe[1] != 0) close(stderr_pipe[1]);
        result.stderr_text = std::strerror(errno);
        return result;
    }

    const pid_t child = fork();
    if (child < 0) {
        close(stdout_pipe[0]);
        close(stdout_pipe[1]);
        close(stderr_pipe[0]);
        close(stderr_pipe[1]);
        result.stderr_text = std::strerror(errno);
        return result;
    }
    if (child == 0) {
        dup2(stdout_pipe[1], STDOUT_FILENO);
        dup2(stderr_pipe[1], STDERR_FILENO);
        close(stdout_pipe[0]);
        close(stdout_pipe[1]);
        close(stderr_pipe[0]);
        close(stderr_pipe[1]);
        std::vector<const char*> argv;
        argv.reserve(arguments.size() + 2);
        argv.push_back(executable);
        for (const std::string& argument : arguments) {
            argv.push_back(argument.c_str());
        }
        argv.push_back(nullptr);
        execvp(executable, const_cast<char* const*>(argv.data()));
        _exit(127);
    }

    result.started = true;
    close(stdout_pipe[1]);
    close(stderr_pipe[1]);

    const int stdout_flags = fcntl(stdout_pipe[0], F_GETFL, 0);
    const int stderr_flags = fcntl(stderr_pipe[0], F_GETFL, 0);
    fcntl(stdout_pipe[0], F_SETFL, stdout_flags | O_NONBLOCK);
    fcntl(stderr_pipe[0], F_SETFL, stderr_flags | O_NONBLOCK);

    const auto deadline =
        std::chrono::steady_clock::now() + timeout;
    int status = 0;
    for (;;) {
        char buffer[4096];
        pollfd pipes[] = {
            {stdout_pipe[0], POLLIN, 0},
            {stderr_pipe[0], POLLIN, 0},
        };
        poll(pipes, 2, 10);
        if (pipes[0].revents & POLLIN) {
            const ssize_t count =
                ::read(stdout_pipe[0], buffer, sizeof(buffer));
            if (count > 0) result.stdout_text.append(buffer, count);
        }
        if (pipes[1].revents & POLLIN) {
            const ssize_t count =
                ::read(stderr_pipe[0], buffer, sizeof(buffer));
            if (count > 0) result.stderr_text.append(buffer, count);
        }

        const pid_t waited = waitpid(child, &status, WNOHANG);
        if (waited == child) break;
        if (waited < 0) {
            result.stderr_text = std::strerror(errno);
            close(stdout_pipe[0]);
            close(stderr_pipe[0]);
            return result;
        }
        if (std::chrono::steady_clock::now() >= deadline) {
            result.timed_out = true;
            kill(child, SIGKILL);
            waitpid(child, &status, 0);
            break;
        }
    }

    char buffer[4096];
    for (;;) {
        const ssize_t count =
            ::read(stdout_pipe[0], buffer, sizeof(buffer));
        if (count <= 0) break;
        result.stdout_text.append(buffer, count);
    }
    for (;;) {
        const ssize_t count =
            ::read(stderr_pipe[0], buffer, sizeof(buffer));
        if (count <= 0) break;
        result.stderr_text.append(buffer, count);
    }
    close(stdout_pipe[0]);
    close(stderr_pipe[0]);

    if (WIFEXITED(status)) {
        result.exit_code = WEXITSTATUS(status);
    } else {
        result.exit_code = -1;
    }
    return result;
}
#else
NativeProcessResult run_native_process(
    const std::filesystem::path& executable,
    const std::vector<std::wstring>& arguments,
    std::chrono::milliseconds timeout)
{
    NativeProcessResult result;
    std::wstring command_line = L"\"" + executable.wstring() + L"\"";
    for (const std::wstring& argument : arguments) {
        command_line += L" ";
        if (argument.find(L' ') != std::wstring::npos) {
            command_line += L"\"" + argument + L"\"";
        } else {
            command_line += argument;
        }
    }

    HANDLE stdout_read = nullptr;
    HANDLE stdout_write = nullptr;
    HANDLE stderr_read = nullptr;
    HANDLE stderr_write = nullptr;
    SECURITY_ATTRIBUTES security{};
    security.nLength = sizeof(security);
    security.bInheritHandle = TRUE;
    if (!CreatePipe(&stdout_read, &stdout_write, &security, 0) ||
        !CreatePipe(&stderr_read, &stderr_write, &security, 0)) {
        result.stderr_text = "cannot create native process pipes";
        return result;
    }

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdOutput = stdout_write;
    startup.hStdError = stderr_write;
    startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(
            nullptr,
            command_line.data(),
            nullptr,
            nullptr,
            TRUE,
            0,
            nullptr,
            nullptr,
            &startup,
            &process)) {
        CloseHandle(stdout_read);
        CloseHandle(stdout_write);
        CloseHandle(stderr_read);
        CloseHandle(stderr_write);
        result.stderr_text = "cannot start native process";
        return result;
    }
    result.started = true;
    CloseHandle(stdout_write);
    CloseHandle(stderr_write);

    const DWORD wait_result =
        WaitForSingleObject(process.hProcess, static_cast<DWORD>(timeout.count()));
    if (wait_result == WAIT_TIMEOUT) {
        result.timed_out = true;
        TerminateProcess(process.hProcess, 124);
    }

    char buffer[4096];
    DWORD count = 0;
    while (PeekNamedPipe(stdout_read, nullptr, 0, nullptr, &count, nullptr) &&
           count != 0) {
        DWORD read = 0;
        if (!ReadFile(stdout_read, buffer, sizeof(buffer), &read, nullptr) ||
            read == 0) break;
        result.stdout_text.append(buffer, read);
    }
    while (PeekNamedPipe(stderr_read, nullptr, 0, nullptr, &count, nullptr) &&
           count != 0) {
        DWORD read = 0;
        if (!ReadFile(stderr_read, buffer, sizeof(buffer), &read, nullptr) ||
            read == 0) break;
        result.stderr_text.append(buffer, read);
    }

    DWORD exit_code = 0;
    GetExitCodeProcess(process.hProcess, &exit_code);
    result.exit_code = static_cast<int>(exit_code);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    CloseHandle(stdout_read);
    CloseHandle(stderr_read);
    return result;
}
#endif

}
