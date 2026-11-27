#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace anyps1 {

enum class HostServiceKind {
    Console,
    File,
    Input,
    Audio,
    Video,
    VulkanFramebuffer,
    CdRom,
    Timers,
    Controllers,
    MemoryCard,
    Spu,
    Dma,
    Interrupt,
    Cache,
    Peripheral
};

struct HostServiceMapping {
    HostServiceKind kind = HostServiceKind::Console;
    std::string ps1_service;
    std::string native_service;
    bool implemented = false;
};

HostServiceMapping map_host_service(HostServiceKind kind);

enum class HostExitCode : int {
    Success = 0,
    UnsupportedInstruction = 16,
    UnsupportedHostService = 17,
    HostRuntimeError = 18,
};

std::string host_exit_log(HostExitCode code);

class NativeFileRuntime {
public:
    NativeFileRuntime();
    ~NativeFileRuntime();

    int open(const std::string& path, bool write, bool append,
             std::string& error);
    int read(int handle, void* data, std::size_t size,
             std::string& error);
    int write(int handle, const void* data, std::size_t size,
              std::string& error);
    long seek(int handle, long offset, int origin,
              std::string& error);
    bool close(int handle, std::string& error);

private:
    struct Impl;
    Impl* impl_;
};

class SdlHostRuntime {
public:
    SdlHostRuntime();
    ~SdlHostRuntime();

    bool initialize(
        const std::string& title,
        std::uint32_t width,
        std::uint32_t height,
        std::string& error);
    bool poll_events(std::string& error);
    bool should_close() const;
    bool present_framebuffer(
        const std::uint32_t* pixels,
        std::uint32_t width,
        std::uint32_t height,
        std::string& error);
    bool queue_audio(
        const std::int16_t* samples,
        std::size_t sample_count,
        std::string& error);
    void shutdown();

private:
    struct Impl;
    Impl* impl_;
};

}
