#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include "host/Ps1DeviceRuntime.h"
#include "input/Input.h"

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

enum class Ps1BiosCallKind {
    Unsupported,
    InitHeap,
    B0Alloc,
    B0Free,
    B0InitHeap,
    B0DeliverEvent,
    B0OpenEvent,
    B0CloseEvent,
    B0WaitEvent,
    B0TestEvent,
    B0EnableEvent,
    B0DisableEvent,
    B0InitPad,
    B0StartPad,
    B0StopPad,
    B0PadInitDirect,
    B0PadReceive,
    B0ReturnFromException,
    B0ResetEntryInt,
    B0InitCard,
    B0StartCard,
    B0StopCard,
    C0InitRCnt,
    C0InitException,
    C0SysEnqIntRP,
    C0SysDeqIntRP,
    C0SysEnqSyscall,
    C0SysDeqSyscall,
    C0SetConfig,
    C0SetLang,
    C0InitHeap,
    C0ChangeClearRCnt,
    C0FlushCache,
    C0CardInfo,
    C0CardLoad,
    C0CardAuto,
    C0NewCard
};

struct Ps1BiosCallPlan {
    Ps1BiosCallKind kind = Ps1BiosCallKind::Unsupported;
    std::uint32_t vector = 0;
    std::uint32_t function = 0;
};

Ps1BiosCallPlan map_ps1_bios_call(
    std::uint32_t vector,
    std::uint32_t function);

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
    bool is_open() const;
    void close_disc();

private:
    std::string bin_path_;
    bool mode2_ = false;
};

bool is_supported_bios_vector(std::uint32_t vector);

struct HostLoopPlan {
    bool input_polling = true;
    bool window_events = true;
    bool audio = true;
    bool framebuffer = true;
    bool vulkan = true;
    bool memory_card = true;
    bool disc = true;
    bool vblank_scheduling = true;
};

HostLoopPlan plan_native_host_loop();

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

class NativeHostLoopRuntime {
public:
    NativeHostLoopRuntime();
    ~NativeHostLoopRuntime();

    NativeHostLoopRuntime(const NativeHostLoopRuntime&) = delete;
    NativeHostLoopRuntime& operator=(const NativeHostLoopRuntime&) = delete;

    bool initialize(
        const std::string& title,
        std::uint32_t width,
        std::uint32_t height,
        const std::string& input_config_path,
        std::string& error);
    bool poll(Ps1InputState& input, std::string& error);
    bool present_framebuffer(
        const std::uint32_t* pixels,
        std::uint32_t width,
        std::uint32_t height,
        std::string& error);
    bool queue_audio(
        const std::int16_t* samples,
        std::size_t sample_count,
        std::string& error);
    bool should_close() const;
    bool open_memory_card(
        int slot,
        const std::string& file_path,
        std::string& error);
    bool read_memory_card(
        int slot,
        std::uint32_t block,
        std::uint8_t* data,
        std::size_t size,
        std::string& error);
    bool write_memory_card(
        int slot,
        std::uint32_t block,
        const std::uint8_t* data,
        std::size_t size,
        std::string& error);
    void close_memory_card(int slot);
    bool open_disc(
        const std::string& bin_path,
        bool mode2,
        std::string& error);
    bool read_disc(
        std::uint32_t lba,
        std::uint8_t* data,
        std::size_t size,
        std::string& error);
    void close_disc();
    std::string structured_exit_log(HostExitCode code) const;
    std::uint32_t controller_state() const;
    void attach_ram(std::uint32_t* ram);
    void set_controller_state(std::uint32_t buttons);
    std::uint32_t read(std::uint32_t address);
    int write(std::uint32_t address, std::uint32_t value);
    int spu_write(std::uint32_t address, std::uint32_t value);
    std::uint32_t spu_read(std::uint32_t address);
    int gpu_write(std::uint32_t address, std::uint32_t value);
    std::uint32_t mmio_read(std::uint32_t address);
    int mmio_write(std::uint32_t address, std::uint32_t value);
    int tick(std::uint32_t cycles);
    std::uint32_t frame_count() const;
    std::uint32_t presented_frame_count() const;
    std::uint32_t vblank_count() const;
    std::uint64_t queued_audio_samples() const;
    int vulkan_init(
        const std::string& title,
        std::uint32_t width,
        std::uint32_t height,
        std::string& error);
    int vulkan_present(std::string& error);
    void shutdown();

private:
    struct Impl;
    Impl* impl_;
};

extern "C" {
int anyps1_host_loop_init(const char* title);
int anyps1_host_loop_poll();
int anyps1_host_loop_present(
    const std::uint32_t* pixels,
    std::uint32_t width,
    std::uint32_t height);
std::uint32_t anyps1_host_loop_controller_state();
int anyps1_host_loop_spu_write(
    std::uint32_t address,
    std::uint32_t value);
std::uint32_t anyps1_host_loop_spu_read(std::uint32_t address);
int anyps1_host_loop_audio_queue(
    const std::int16_t* samples,
    std::size_t sample_count);
int anyps1_host_loop_gpu_write(
    std::uint32_t address,
    std::uint32_t value);
std::uint32_t anyps1_host_loop_mmio_read(std::uint32_t address);
int anyps1_host_loop_mmio_write(
    std::uint32_t address,
    std::uint32_t value);
int anyps1_host_loop_tick(std::uint32_t cycles);
int anyps1_host_loop_attach_ram(std::uint32_t* ram);
std::uint32_t anyps1_host_loop_frame_count();
std::uint32_t anyps1_host_loop_presented_frame_count();
std::uint32_t anyps1_host_loop_vblank_count();
std::uint64_t anyps1_host_loop_queued_audio_samples();
int anyps1_host_loop_memory_card_open(
    int slot,
    const char* path);
int anyps1_host_loop_memory_card_read(
    int slot,
    std::uint32_t block,
    std::uint32_t offset);
int anyps1_host_loop_memory_card_write(
    int slot,
    std::uint32_t block,
    std::uint32_t offset,
    std::uint32_t value);
int anyps1_host_loop_disc_open(
    const char* path,
    int mode2);
std::uint32_t anyps1_host_loop_disc_read(
    std::uint32_t lba,
    std::uint32_t offset);
void anyps1_host_loop_disc_close();
void anyps1_host_loop_memory_card_close(int slot);
int anyps1_host_loop_vulkan_init(
    const char* title,
    std::uint32_t width,
    std::uint32_t height);
int anyps1_host_loop_vulkan_present();
void anyps1_host_loop_shutdown();
}

}
