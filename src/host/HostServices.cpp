#include "host/HostServices.h"
#include "gpu/Graphics.h"

#include <SDL3/SDL.h>

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <vector>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#include <sys/stat.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

namespace anyps1 {

std::string environment_value(const char* name)
{
#ifdef _WIN32
    char* value = nullptr;
    size_t length = 0;
    if (_dupenv_s(&value, &length, name) != 0 || value == nullptr) {
        return {};
    }
    std::string result(value);
    std::free(value);
    return result;
#else
    const char* value = std::getenv(name);
    return value == nullptr ? std::string{} : std::string(value);
#endif
}
std::string host_exit_log(HostExitCode code)
{
    switch (code) {
    case HostExitCode::Success:
        return "host_exit=0";
    case HostExitCode::UnsupportedInstruction:
        return "host_exit=16 unsupported_native_instruction";
    case HostExitCode::UnsupportedHostService:
        return "host_exit=17 unsupported_host_service";
    case HostExitCode::HostRuntimeError:
        return "host_exit=18 host_runtime_error";
    }
    return "host_exit=18 host_runtime_error";
}

Ps1FileCallPlan map_ps1_file_call(std::uint32_t vector)
{
    switch (vector & 0x000000FFu) {
    case 0x32:
        return {Ps1FileCallKind::Open, vector, 2, "CreateFileA"};
    case 0x33:
        return {Ps1FileCallKind::Seek, vector, 8, "SetFilePointer"};
    case 0x34:
        return {Ps1FileCallKind::Read, vector, 0, "ReadFile"};
    case 0x35:
        return {Ps1FileCallKind::Write, vector, 1, "WriteFile"};
    case 0x36:
        return {Ps1FileCallKind::Close, vector, 3, "CloseHandle"};
    default:
        return {Ps1FileCallKind::Unsupported, vector, -1, nullptr};
    }
}

Ps1BiosCallPlan map_ps1_bios_call(
    std::uint32_t vector,
    std::uint32_t function)
{
    std::uint32_t vector_id = vector;
    if ((vector & 0xFF000000u) == 0xA0000000u ||
        (vector & 0xFF000000u) == 0xB0000000u ||
        (vector & 0xFF000000u) == 0xC0000000u) {
        vector_id = vector >> 24;
    }
    if (vector_id == 0xA0u && function == 0x39u) {
        return {Ps1BiosCallKind::Memcpy, vector, function};
    }
    if (vector_id == 0xB0u) {
        switch (function) {
        case 0x00u: return {Ps1BiosCallKind::B0Alloc, vector, function};
        case 0x01u: return {Ps1BiosCallKind::B0Free, vector, function};
        case 0x02u: return {Ps1BiosCallKind::B0InitHeap, vector, function};
        case 0x07u: return {Ps1BiosCallKind::B0DeliverEvent, vector, function};
        case 0x08u: return {Ps1BiosCallKind::B0OpenEvent, vector, function};
        case 0x09u: return {Ps1BiosCallKind::B0CloseEvent, vector, function};
        case 0x0Au: return {Ps1BiosCallKind::B0WaitEvent, vector, function};
        case 0x0Bu: return {Ps1BiosCallKind::B0TestEvent, vector, function};
        case 0x0Cu: return {Ps1BiosCallKind::B0EnableEvent, vector, function};
        case 0x0Du: return {Ps1BiosCallKind::B0DisableEvent, vector, function};
        case 0x12u: return {Ps1BiosCallKind::B0InitPad, vector, function};
        case 0x13u: return {Ps1BiosCallKind::B0StartPad, vector, function};
        case 0x14u: return {Ps1BiosCallKind::B0StopPad, vector, function};
        case 0x15u: return {Ps1BiosCallKind::B0PadInitDirect, vector, function};
        case 0x16u: return {Ps1BiosCallKind::B0PadReceive, vector, function};
        case 0x17u: return {Ps1BiosCallKind::B0ReturnFromException, vector, function};
        case 0x18u: return {Ps1BiosCallKind::B0ResetEntryInt, vector, function};
        case 0x32u: return {Ps1BiosCallKind::B0InitCard, vector, function};
        case 0x33u: return {Ps1BiosCallKind::B0StartCard, vector, function};
        case 0x35u: return {Ps1BiosCallKind::B0StopCard, vector, function};
        default: break;
        }
    }
    if (vector_id == 0xC0u) {
        switch (function) {
        case 0x00u: return {Ps1BiosCallKind::C0SysEnqIntRP, vector, function};
        case 0x01u: return {Ps1BiosCallKind::C0SysDeqIntRP, vector, function};
        case 0x02u: return {Ps1BiosCallKind::C0SysEnqSyscall, vector, function};
        case 0x03u: return {Ps1BiosCallKind::C0SysDeqSyscall, vector, function};
        case 0x08u: return {Ps1BiosCallKind::C0CardInfo, vector, function};
        case 0x09u: return {Ps1BiosCallKind::C0CardLoad, vector, function};
        case 0x0Au: return {Ps1BiosCallKind::C0CardAuto, vector, function};
        case 0x0Bu: return {Ps1BiosCallKind::C0NewCard, vector, function};
        case 0x10u: return {Ps1BiosCallKind::C0FlushCache, vector, function};
        case 0x13u: return {Ps1BiosCallKind::C0ChangeClearRCnt, vector, function};
        default: break;
        }
    }
    return {Ps1BiosCallKind::Unsupported, vector, function};
}

HostServiceMapping map_host_service(HostServiceKind kind)
{
    switch (kind) {
    case HostServiceKind::Console:
        return {kind, "PS1 BIOS console", "Linux write/exit", true};
    case HostServiceKind::File:
        return {kind, "PS1 file service", "POSIX open/read/write/close", true};
    case HostServiceKind::Input:
        return {kind, "PS1 controller ports", "SDL3 keyboard/mouse/gamepad", true};
    case HostServiceKind::Audio:
        return {kind, "PS1 SPU", "SDL3 audio stream", true};
    case HostServiceKind::Video:
        return {kind, "PS1 GPU/VRAM", "SDL3 framebuffer/Vulkan surface", true};
    case HostServiceKind::VulkanFramebuffer:
        return {kind, "PS1 framebuffer", "Vulkan instance/surface/swapchain/resources", true};
    case HostServiceKind::CdRom:
        return {kind, "PS1 CD-ROM registers", "native disc/image API", true};
    case HostServiceKind::Timers:
        return {kind, "PS1 timers", "native monotonic timer or explicit unsupported exit", false};
    case HostServiceKind::Controllers:
        return {kind, "PS1 controller ports", "SDL3 input or explicit unsupported exit", true};
    case HostServiceKind::MemoryCard:
        return {kind, "PS1 memory-card ports", "native title-independent block storage", true};
    case HostServiceKind::Spu:
        return {kind, "PS1 SPU", "SDL3 audio backend", true};
    case HostServiceKind::Dma:
        return {kind, "PS1 DMA channels", "native host memory copy or explicit unsupported exit", false};
    case HostServiceKind::Interrupt:
        return {kind, "PS1 interrupt controller", "native event mapping or explicit unsupported exit", false};
    case HostServiceKind::Cache:
        return {kind, "PS1 cache state", "native section mapping or explicit unsupported exit", false};
    case HostServiceKind::Peripheral:
        return {kind, "PS1 peripheral bus", "documented native device mapping or explicit unsupported exit", false};
    }
    return {kind, "unknown PS1 host service", "unsupported", false};
}

struct NativeFileRuntime::Impl {
    std::map<int, int> descriptors;
    int next_handle = 3;
};

NativeFileRuntime::NativeFileRuntime()
    : impl_(new Impl{})
{
}

NativeFileRuntime::~NativeFileRuntime()
{
    for (const auto& entry : impl_->descriptors) {
#ifdef _WIN32
        _close(entry.second);
#else
        ::close(entry.second);
#endif
    }
    delete impl_;
}

int NativeFileRuntime::open(const std::string& path, bool write, bool append,
                            std::string& error)
{
#ifdef _WIN32
    int flags = (_O_BINARY | (write ? (_O_WRONLY | _O_CREAT) : _O_RDONLY));
    if (append) flags |= _O_APPEND;
    int mode = _S_IREAD | _S_IWRITE;
    int descriptor = -1;
    const errno_t open_error =
        _sopen_s(&descriptor, path.c_str(), flags, _SH_DENYNO, mode);
    if (open_error != 0) {
        error = "native file open failed";
        return -1;
    }
#else
    int flags = write ? (O_WRONLY | O_CREAT) : O_RDONLY;
    if (append) flags |= O_APPEND;
    const int descriptor = ::open(path.c_str(), flags, 0666);
    if (descriptor < 0) {
        error = "native file open failed";
        return -1;
    }
#endif
    const int handle = impl_->next_handle++;
    impl_->descriptors.emplace(handle, descriptor);
    error.clear();
    return handle;
}

int NativeFileRuntime::read(int handle, void* data, std::size_t size,
                            std::string& error)
{
    const auto entry = impl_->descriptors.find(handle);
    if (entry == impl_->descriptors.end()) {
        error = "native file read used invalid handle";
        return -1;
    }
#ifdef _WIN32
    const int result = _read(entry->second, data, static_cast<unsigned int>(size));
#else
    const ssize_t result = ::read(entry->second, data, size);
#endif
    if (result < 0) {
        error = "native file read failed";
        return -1;
    }
    error.clear();
    return result;
}

int NativeFileRuntime::write(int handle, const void* data, std::size_t size,
                             std::string& error)
{
    const auto entry = impl_->descriptors.find(handle);
    if (entry == impl_->descriptors.end()) {
        error = "native file write used invalid handle";
        return -1;
    }
#ifdef _WIN32
    const int result = _write(entry->second, data, static_cast<unsigned int>(size));
#else
    const ssize_t result = ::write(entry->second, data, size);
#endif
    if (result < 0) {
        error = "native file write failed";
        return -1;
    }
    error.clear();
    return result;
}

long NativeFileRuntime::seek(int handle, long offset, int origin,
                             std::string& error)
{
    const auto entry = impl_->descriptors.find(handle);
    if (entry == impl_->descriptors.end()) {
        error = "native file seek used invalid handle";
        return -1;
    }
#ifdef _WIN32
    const long result = _lseek(entry->second, offset, origin);
#else
    const off_t result = ::lseek(entry->second, offset, origin);
#endif
    if (result < 0) {
        error = "native file seek failed";
        return -1;
    }
    error.clear();
    return static_cast<long>(result);
}

bool NativeFileRuntime::close(int handle, std::string& error)
{
    const auto entry = impl_->descriptors.find(handle);
    if (entry == impl_->descriptors.end()) {
        error = "native file close used invalid handle";
        return false;
    }
#ifdef _WIN32
    const int result = _close(entry->second);
#else
    const int result = ::close(entry->second);
#endif
    impl_->descriptors.erase(entry);
    if (result != 0) {
        error = "native file close failed";
        return false;
    }
    error.clear();
    return true;
}

struct SdlHostRuntime::Impl {
    SDL_Window* window = nullptr;
    SDL_Renderer* renderer = nullptr;
    SDL_Texture* framebuffer = nullptr;
    SDL_AudioStream* audio = nullptr;
    bool initialized = false;
    bool closed = false;
};

SdlHostRuntime::SdlHostRuntime()
    : impl_(new Impl{})
{
}

SdlHostRuntime::~SdlHostRuntime()
{
    shutdown();
    delete impl_;
}

bool SdlHostRuntime::initialize(
    const std::string& title,
    std::uint32_t width,
    std::uint32_t height,
    std::string& error)
{
    if (impl_->initialized) {
        error.clear();
        return true;
    }
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMEPAD |
                  SDL_INIT_EVENTS)) {
        error = "cannot initialize SDL host runtime: " +
            std::string(SDL_GetError());
        return false;
    }
    SDL_Window* window = nullptr;
    SDL_Renderer* renderer = nullptr;
    if (!SDL_CreateWindowAndRenderer(
            title.c_str(),
            static_cast<int>(width),
            static_cast<int>(height),
            SDL_WINDOW_RESIZABLE,
            &window,
            &renderer)) {
        error = "cannot create SDL host window: " +
            std::string(SDL_GetError());
        SDL_QuitSubSystem(SDL_INIT_VIDEO | SDL_INIT_AUDIO);
        return false;
    }
    impl_->window = window;
    impl_->renderer = renderer;
    impl_->framebuffer = SDL_CreateTexture(
        renderer,
        SDL_PIXELFORMAT_ABGR8888,
        SDL_TEXTUREACCESS_STREAMING,
        1024,
        512);
    if (impl_->framebuffer == nullptr) {
        error = "cannot create framebuffer texture: " +
            std::string(SDL_GetError());
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        impl_->window = nullptr;
        impl_->renderer = nullptr;
        SDL_QuitSubSystem(SDL_INIT_VIDEO | SDL_INIT_AUDIO);
        return false;
    }

    SDL_AudioSpec spec{};
    spec.freq = 44100;
    spec.format = SDL_AUDIO_S16;
    spec.channels = 2;
    impl_->audio = SDL_OpenAudioDeviceStream(
        SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK,
        &spec,
        nullptr,
        nullptr);
    if (impl_->audio == nullptr) {
        error = "cannot open SDL audio stream: " +
            std::string(SDL_GetError());
        SDL_DestroyTexture(impl_->framebuffer);
        SDL_DestroyRenderer(impl_->renderer);
        SDL_DestroyWindow(impl_->window);
        impl_->framebuffer = nullptr;
        impl_->renderer = nullptr;
        impl_->window = nullptr;
        SDL_QuitSubSystem(SDL_INIT_VIDEO | SDL_INIT_AUDIO);
        return false;
    }

    impl_->initialized = true;
    impl_->closed = false;
    error.clear();
    return true;
}

bool SdlHostRuntime::poll_events(std::string& error)
{
    if (!impl_->initialized) {
        error = "SDL host runtime is not initialized";
        return false;
    }
    SDL_Event event{};
    while (SDL_PollEvent(&event)) {
        if (event.type == SDL_EVENT_QUIT) {
            impl_->closed = true;
        }
    }
    error.clear();
    return true;
}

bool SdlHostRuntime::should_close() const
{
    return impl_->closed;
}

bool SdlHostRuntime::present_framebuffer(
    const std::uint32_t* pixels,
    std::uint32_t width,
    std::uint32_t height,
    std::string& error)
{
    if (!impl_->initialized || impl_->renderer == nullptr ||
        impl_->framebuffer == nullptr) {
        error = "SDL framebuffer runtime is not initialized";
        return false;
    }
    if (pixels == nullptr) {
        error = "SDL framebuffer pixels are null";
        return false;
    }
    if (!SDL_UpdateTexture(
            impl_->framebuffer,
            nullptr,
            pixels,
            static_cast<int>(width * sizeof(std::uint32_t)))) {
        error = "cannot update framebuffer texture: " +
            std::string(SDL_GetError());
        return false;
    }
    SDL_RenderClear(impl_->renderer);
    SDL_RenderTexture(
        impl_->renderer,
        impl_->framebuffer,
        nullptr,
        nullptr);
    SDL_RenderPresent(impl_->renderer);
    (void)height;
    error.clear();
    return true;
}

bool SdlHostRuntime::queue_audio(
    const std::int16_t* samples,
    std::size_t sample_count,
    std::string& error)
{
    if (!impl_->initialized || impl_->audio == nullptr) {
        error = "SDL audio runtime is not initialized";
        return false;
    }
    if (samples == nullptr && sample_count != 0) {
        error = "SDL audio samples are null";
        return false;
    }
    if (sample_count == 0) {
        error.clear();
        return true;
    }
    if (!SDL_PutAudioStreamData(
            impl_->audio,
            samples,
            static_cast<int>(sample_count * sizeof(std::int16_t)))) {
        error = "cannot queue SDL audio samples: " +
            std::string(SDL_GetError());
        return false;
    }
    SDL_ResumeAudioStreamDevice(impl_->audio);
    error.clear();
    return true;
}

void SdlHostRuntime::shutdown()
{
    if (impl_ == nullptr) return;
    if (impl_->audio != nullptr) {
        SDL_DestroyAudioStream(impl_->audio);
        impl_->audio = nullptr;
    }
    if (impl_->framebuffer != nullptr) {
        SDL_DestroyTexture(impl_->framebuffer);
        impl_->framebuffer = nullptr;
    }
    if (impl_->renderer != nullptr) {
        SDL_DestroyRenderer(impl_->renderer);
        impl_->renderer = nullptr;
    }
    if (impl_->window != nullptr) {
        SDL_DestroyWindow(impl_->window);
        impl_->window = nullptr;
    }
    if (impl_->initialized) {
        SDL_Quit();
        impl_->initialized = false;
    }
}

struct NativeHostLoopRuntime::Impl {
    SdlHostRuntime host;
    SdlInputRuntime input;
    MemoryCardRuntime memory_card;
    DiscImageRuntime disc;
    VulkanSwapchainRuntime vulkan;
    std::map<std::uint32_t, std::uint32_t> spu_registers;
    std::vector<std::int16_t> spu_output;
    std::vector<std::uint32_t> gpu_vram;
    Ps1DeviceRuntime devices;
    Ps1InputState input_state;
    std::vector<std::uint8_t> controller_rx;
    std::size_t controller_rx_read = 0;
    std::uint32_t joy_mode = 0;
    std::uint16_t joy_control = 0;
    std::uint16_t joy_baud = 0;
    std::uint8_t cd_status_index = 0;
    std::uint8_t cd_irq = 0;
    std::uint8_t cd_mode = 0;
    std::uint32_t cd_lba = 0;
    std::uint8_t cd_parameter_fifo[16]{};
    std::size_t cd_parameter_count = 0;
    std::vector<std::uint8_t> cd_response;
    std::size_t cd_response_read = 0;
    bool cd_read_active = false;
    bool initialized = false;
    bool headless = false;
    bool headless_closed = false;
    std::uint32_t headless_frames = 0;
    std::uint32_t frame_count = 0;
    std::uint32_t presented_frames = 0;
    std::uint32_t vblank_count = 0;
    std::uint64_t queued_audio_samples = 0;
    std::chrono::steady_clock::time_point last_frame_time{};
};

NativeHostLoopRuntime::NativeHostLoopRuntime()
    : impl_(new Impl{})
{
}

NativeHostLoopRuntime::~NativeHostLoopRuntime()
{
    shutdown();
    delete impl_;
}

bool NativeHostLoopRuntime::initialize(
    const std::string& title,
    std::uint32_t width,
    std::uint32_t height,
    const std::string& input_config_path,
    std::string& error)
{
    if (impl_->initialized) {
        error.clear();
        return true;
    }
    if (!environment_value("ANYPS1_TEST_HEADLESS").empty()) {
        impl_->headless = true;
        impl_->initialized = true;
        impl_->gpu_vram.assign(1024u * 512u, 0u);
        impl_->last_frame_time = std::chrono::steady_clock::now();
        error.clear();
        return true;
    }
    if (environment_value("DISPLAY").empty() &&
        environment_value("WAYLAND_DISPLAY").empty()) {
        impl_->headless = true;
        impl_->initialized = true;
        impl_->gpu_vram.assign(1024u * 512u, 0u);
        impl_->last_frame_time = std::chrono::steady_clock::now();
        error.clear();
        return true;
    }
    if (!impl_->host.initialize(title, width, height, error)) {
        return false;
    }
    const InputConfig input_config =
        load_input_config_or_default(input_config_path, error);
    if (!impl_->input.initialize(input_config, error)) {
        impl_->host.shutdown();
        return false;
    }
    std::string card1_path =
        environment_value("ANYPS1_MEMORY_CARD_1");
    std::string card2_path =
        environment_value("ANYPS1_MEMORY_CARD_2");
    if (card1_path.empty()) card1_path = "anyps1-card1.mcd";
    if (card2_path.empty()) card2_path = "anyps1-card2.mcd";
    std::string storage_error;
    impl_->memory_card.open_slot(0, card1_path, storage_error);
    impl_->memory_card.open_slot(1, card2_path, storage_error);
    std::string disc_path = environment_value("ANYPS1_DISC_IMAGE");
    if (!disc_path.empty()) {
        impl_->disc.open_disc(disc_path, false, storage_error);
    } else if (std::filesystem::exists("disc.bin")) {
        impl_->disc.open_disc("disc.bin", false, storage_error);
    }
    impl_->gpu_vram.assign(1024u * 512u, 0u);
    impl_->initialized = true;
    impl_->last_frame_time = std::chrono::steady_clock::now();
    error.clear();
    return true;
}

bool NativeHostLoopRuntime::poll(Ps1InputState& input, std::string& error)
{
    if (!impl_->initialized) {
        error = "native host loop runtime is not initialized";
        return false;
    }
    if (impl_->headless) {
        impl_->input_state = input;
        ++impl_->headless_frames;
        ++impl_->frame_count;
        ++impl_->presented_frames;
        if (impl_->headless_frames >= 1u) {
            impl_->headless_closed = true;
        }
        error.clear();
        return true;
    }
    if (!impl_->host.poll_events(error)) return false;
    if (impl_->host.should_close()) {
        error.clear();
        return true;
    }
    if (!impl_->input.poll(input, error)) return false;
    impl_->input_state = input;
    const auto now = std::chrono::steady_clock::now();
    const auto elapsed =
        std::chrono::duration_cast<std::chrono::milliseconds>(
            now - impl_->last_frame_time);
    if (elapsed.count() >= 16) {
        std::string presentation_error;
        if (!impl_->host.present_framebuffer(
                impl_->gpu_vram.data(),
                1024u,
                512u,
                presentation_error)) {
            error = presentation_error;
            return false;
        }
        ++impl_->frame_count;
        ++impl_->presented_frames;
        impl_->last_frame_time = now;
    }
    return true;
}

bool NativeHostLoopRuntime::present_framebuffer(
    const std::uint32_t* pixels,
    std::uint32_t width,
    std::uint32_t height,
    std::string& error)
{
    if (!impl_->initialized) {
        error = "native host loop runtime is not initialized";
        return false;
    }
    if (impl_->headless) {
        if (pixels == nullptr) {
            error = "SDL framebuffer pixels are null";
            return false;
        }
        ++impl_->presented_frames;
        ++impl_->frame_count;
        error.clear();
        return true;
    }
    if (!impl_->host.present_framebuffer(
            pixels, width, height, error)) {
        return false;
    }
    ++impl_->presented_frames;
    ++impl_->frame_count;
    return true;
}

bool NativeHostLoopRuntime::queue_audio(
    const std::int16_t* samples,
    std::size_t sample_count,
    std::string& error)
{
    if (!impl_->initialized) {
        error = "native host loop runtime is not initialized";
        return false;
    }
    if (impl_->headless) {
        if (samples == nullptr && sample_count != 0) {
            error = "SDL audio samples are null";
            return false;
        }
        impl_->queued_audio_samples += sample_count;
        error.clear();
        return true;
    }
    if (!impl_->host.queue_audio(samples, sample_count, error)) {
        return false;
    }
    impl_->queued_audio_samples += sample_count;
    return true;
}

bool NativeHostLoopRuntime::should_close() const
{
    if (impl_->headless_closed) return true;
    return impl_->initialized && impl_->host.should_close();
}

bool NativeHostLoopRuntime::open_memory_card(
    int slot,
    const std::string& file_path,
    std::string& error)
{
    return impl_->memory_card.open_slot(slot, file_path, error);
}

bool NativeHostLoopRuntime::read_memory_card(
    int slot,
    std::uint32_t block,
    std::uint8_t* data,
    std::size_t size,
    std::string& error)
{
    return impl_->memory_card.read_block(slot, block, data, size, error);
}

bool NativeHostLoopRuntime::write_memory_card(
    int slot,
    std::uint32_t block,
    const std::uint8_t* data,
    std::size_t size,
    std::string& error)
{
    return impl_->memory_card.write_block(slot, block, data, size, error);
}

void NativeHostLoopRuntime::close_memory_card(int slot)
{
    impl_->memory_card.close_slot(slot);
}

bool NativeHostLoopRuntime::open_disc(
    const std::string& bin_path,
    bool mode2,
    std::string& error)
{
    return impl_->disc.open_disc(bin_path, mode2, error);
}

bool NativeHostLoopRuntime::read_disc(
    std::uint32_t lba,
    std::uint8_t* data,
    std::size_t size,
    std::string& error)
{
    return impl_->disc.read_sector(lba, data, size, error);
}

void NativeHostLoopRuntime::close_disc()
{
    impl_->disc.close_disc();
}

std::string NativeHostLoopRuntime::structured_exit_log(
    HostExitCode code) const
{
    return host_exit_log(code);
}

std::uint32_t NativeHostLoopRuntime::controller_state() const
{
    return impl_->initialized
        ? static_cast<std::uint32_t>(impl_->input_state.buttons)
        : 0xFFFFu;
}

void NativeHostLoopRuntime::attach_ram(std::uint32_t* ram)
{
    impl_->devices.attach_ram(ram);
}

void NativeHostLoopRuntime::set_controller_state(std::uint32_t buttons)
{
    impl_->devices.set_controller_state(buttons);
    impl_->input_state.buttons =
        static_cast<std::uint16_t>(buttons);
}

std::uint32_t NativeHostLoopRuntime::read(std::uint32_t address)
{
    return impl_->devices.read(address);
}

int NativeHostLoopRuntime::write(std::uint32_t address, std::uint32_t value)
{
    return impl_->devices.write(address, value);
}

int NativeHostLoopRuntime::spu_write(
    std::uint32_t address,
    std::uint32_t value)
{
    impl_->spu_registers[address & 0x1FFFu] = value;
    if (address >= 0x1F801C00u && address < 0x1F801D80u) {
        impl_->spu_output.assign(1024u, 0);
        for (std::size_t index = 0; index < impl_->spu_output.size(); index += 2) {
            const std::uint32_t register_value =
                impl_->spu_registers[address & 0x1FFFu];
            const std::int16_t sample =
                static_cast<std::int16_t>((register_value * 0x1041u) >> 16u);
            impl_->spu_output[index] = sample;
            impl_->spu_output[index + 1u] = sample;
        }
        std::string error;
        if (!queue_audio(
                impl_->spu_output.data(),
                impl_->spu_output.size(),
                error)) {
            return 18;
        }
        return 0;
    }
    return 17;
}

std::uint32_t NativeHostLoopRuntime::spu_read(std::uint32_t address) const
{
    const auto found = impl_->spu_registers.find(address & 0x1FFFu);
    return found == impl_->spu_registers.end() ? 0u : found->second;
}

int NativeHostLoopRuntime::gpu_write(
    std::uint32_t address,
    std::uint32_t value)
{
    if (address == 0x1F801810u) {
        if (!impl_->gpu_vram.empty()) impl_->gpu_vram[0] = value;
    } else if (address == 0x1F801814u) {
        if (!impl_->gpu_vram.empty()) impl_->gpu_vram[1] = value;
    } else {
        return 17;
    }
    return 0;
}

std::uint32_t NativeHostLoopRuntime::mmio_read(std::uint32_t address)
{
    return impl_->devices.read(address);
}

int NativeHostLoopRuntime::mmio_write(
    std::uint32_t address,
    std::uint32_t value)
{
    return impl_->devices.write(address, value);
}

int NativeHostLoopRuntime::tick(std::uint32_t cycles)
{
    if (!impl_->initialized) return 18;
    impl_->devices.set_controller_state(
        static_cast<std::uint32_t>(impl_->input_state.buttons));
    impl_->devices.tick(cycles);
    impl_->vblank_count += cycles / 1024u;
    return should_close() ? 1 : 0;
}

std::uint32_t NativeHostLoopRuntime::frame_count() const
{
    return impl_->frame_count;
}

std::uint32_t NativeHostLoopRuntime::presented_frame_count() const
{
    return impl_->presented_frames;
}

std::uint32_t NativeHostLoopRuntime::vblank_count() const
{
    return impl_->vblank_count;
}

std::uint64_t NativeHostLoopRuntime::queued_audio_samples() const
{
    return impl_->queued_audio_samples;
}

int NativeHostLoopRuntime::vulkan_init(
    const std::string& title,
    std::uint32_t width,
    std::uint32_t height,
    std::string& error)
{
    return impl_->vulkan.initialize(title, width, height, error) ? 0 : 18;
}

int NativeHostLoopRuntime::vulkan_present(std::string& error)
{
    return impl_->vulkan.present(error) ? 0 : 18;
}

void NativeHostLoopRuntime::shutdown()
{
    if (impl_ == nullptr) return;
    impl_->vulkan.shutdown();
    impl_->input.shutdown();
    impl_->host.shutdown();
    impl_->initialized = false;
}

bool MemoryCardRuntime::open_slot(
    int slot,
    const std::string& file_path,
    std::string& error)
{
    if (slot < 0 || slot >= 2) {
        error = "memory card slot is out of range";
        return false;
    }
    slot_paths_[slot] = file_path;
    std::fstream card(file_path,
                      std::ios::in | std::ios::out | std::ios::binary);
    if (!card) {
        std::ofstream create(file_path,
                             std::ios::binary | std::ios::trunc);
        if (!create) {
            error = "cannot create memory card file";
            return false;
        }
        create.write(std::string(128 * 1024, '\0').c_str(), 128 * 1024);
        if (!create) {
            error = "cannot initialize memory card file";
            return false;
        }
    }
    error.clear();
    return true;
}

bool MemoryCardRuntime::read_block(
    int slot,
    std::uint32_t block,
    std::uint8_t* data,
    std::size_t size,
    std::string& error)
{
    if (slot < 0 || slot >= 2 ||
        slot_paths_[slot].empty() ||
        block >= 1024u ||
        size != 128u ||
        data == nullptr) {
        error = "memory card read parameters are invalid";
        return false;
    }
    std::fstream card(slot_paths_[slot],
                      std::ios::in | std::ios::binary);
    if (!card) {
        error = "cannot open memory card file";
        return false;
    }
    card.seekg(static_cast<std::streamoff>(block) * 128u);
    card.read(reinterpret_cast<char*>(data), static_cast<std::streamsize>(size));
    if (!card) {
        error = "memory card read failed";
        return false;
    }
    error.clear();
    return true;
}

bool MemoryCardRuntime::write_block(
    int slot,
    std::uint32_t block,
    const std::uint8_t* data,
    std::size_t size,
    std::string& error)
{
    if (slot < 0 || slot >= 2 ||
        slot_paths_[slot].empty() ||
        block >= 1024u ||
        size != 128u ||
        data == nullptr) {
        error = "memory card write parameters are invalid";
        return false;
    }
    std::fstream card(slot_paths_[slot],
                      std::ios::in | std::ios::out | std::ios::binary);
    if (!card) {
        error = "cannot open memory card file";
        return false;
    }
    card.seekp(static_cast<std::streamoff>(block) * 128u);
    card.write(reinterpret_cast<const char*>(data),
               static_cast<std::streamsize>(size));
    if (!card) {
        error = "memory card write failed";
        return false;
    }
    error.clear();
    return true;
}

void MemoryCardRuntime::close_slot(int slot)
{
    if (slot >= 0 && slot < 2) {
        slot_paths_[slot].clear();
    }
}

bool DiscImageRuntime::open_disc(
    const std::string& bin_path,
    bool mode2,
    std::string& error)
{
    std::ifstream disc(bin_path, std::ios::binary);
    if (!disc) {
        error = "cannot open disc image";
        return false;
    }
    bin_path_ = bin_path;
    mode2_ = mode2;
    error.clear();
    return true;
}

bool DiscImageRuntime::read_sector(
    std::uint32_t lba,
    std::uint8_t* data,
    std::size_t size,
    std::string& error)
{
    if (bin_path_.empty() || data == nullptr || size == 0) {
        error = "disc image read parameters are invalid";
        return false;
    }
    const std::size_t sector_size =
        mode2_ ? 2352u : 2048u;
    if (size > sector_size) {
        error = "disc sector buffer is too large";
        return false;
    }
    std::ifstream disc(bin_path_, std::ios::binary);
    if (!disc) {
        error = "cannot reopen disc image";
        return false;
    }
    disc.seekg(static_cast<std::streamoff>(lba) * sector_size);
    disc.read(reinterpret_cast<char*>(data),
              static_cast<std::streamsize>(size));
    if (!disc) {
        error = "disc sector read failed";
        return false;
    }
    error.clear();
    return true;
}

void DiscImageRuntime::close_disc()
{
    bin_path_.clear();
    mode2_ = false;
}

HostLoopPlan plan_native_host_loop()
{
    return {
        true,
        true,
        true,
        true,
        true,
        true,
        true,
        true
    };
}

bool is_supported_bios_vector(std::uint32_t vector)
{
    const std::uint32_t function =
        (vector & 0xFF000000u) == 0xA0000000u ||
        (vector & 0xFF000000u) == 0xB0000000u ||
        (vector & 0xFF000000u) == 0xC0000000u
            ? vector & 0xFFu
            : 0u;
    const std::uint32_t vector_id =
        (vector >> 24) & 0xFFu;
    return map_ps1_bios_call(vector_id, function).kind !=
        Ps1BiosCallKind::Unsupported;
}
}
