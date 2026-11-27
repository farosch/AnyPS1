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

enum class Ps1FileCallKind {
    Unsupported,
    Open,
    Read,
    Seek,
    Write,
    Close,
};

struct Ps1FileCallPlan {
    Ps1FileCallKind kind = Ps1FileCallKind::Unsupported;
    std::uint32_t a0_vector = 0;
    int linux_syscall = -1;
    const char* windows_function = nullptr;
};

Ps1FileCallPlan map_ps1_file_call(std::uint32_t vector);

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

class MemoryCardRuntime {
public:
    bool open_slot(
        int slot,
        const std::string& file_path,
        std::string& error);
    bool read_block(
        int slot,
        std::uint32_t block,
        std::uint8_t* data,
        std::size_t size,
        std::string& error);
    bool write_block(
        int slot,
        std::uint32_t block,
        const std::uint8_t* data,
        std::size_t size,
        std::string& error);
    void close_slot(int slot);

private:
    std::string slot_paths_[2];
};

class DiscImageRuntime {
public:
    bool open_disc(
        const std::string& bin_path,
        bool mode2,
        std::string& error);
    bool read_sector(
        std::uint32_t lba,
        std::uint8_t* data,
        std::size_t size,
        std::string& error);
    void close_disc();

private:
    std::string bin_path_;
    bool mode2_ = false;
};

bool is_supported_bios_vector(std::uint32_t vector);

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
