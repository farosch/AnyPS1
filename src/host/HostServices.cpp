#include "host/HostServices.h"

#include <SDL3/SDL.h>

#include <cstdint>
#include <cstdio>
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
        static_cast<int>(width),
        static_cast<int>(height));
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
    bool initialized = false;
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
    if (!impl_->host.initialize(title, width, height, error)) {
        return false;
    }
    const InputConfig input_config =
        load_input_config_or_default(input_config_path, error);
    if (!impl_->input.initialize(input_config, error)) {
        impl_->host.shutdown();
        return false;
    }
    impl_->initialized = true;
    error.clear();
    return true;
}

bool NativeHostLoopRuntime::poll(Ps1InputState& input, std::string& error)
{
    if (!impl_->initialized) {
        error = "native host loop runtime is not initialized";
        return false;
    }
    if (!impl_->host.poll_events(error)) return false;
    if (impl_->host.should_close()) {
        error.clear();
        return true;
    }
    return impl_->input.poll(input, error);
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
    return impl_->host.present_framebuffer(pixels, width, height, error);
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
    return impl_->host.queue_audio(samples, sample_count, error);
}

bool NativeHostLoopRuntime::should_close() const
{
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

void NativeHostLoopRuntime::shutdown()
{
    if (impl_ == nullptr) return;
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
        true
    };
}

bool is_supported_bios_vector(std::uint32_t vector)
{
    (void)vector;
    return false;
}

}

extern "C" {

static anyps1::NativeHostLoopRuntime* anyps1_native_host_loop = nullptr;

int anyps1_host_loop_init(const char* title)
{
    if (anyps1_native_host_loop == nullptr) {
        anyps1_native_host_loop = new anyps1::NativeHostLoopRuntime();
    }
    std::string error;
    if (!anyps1_native_host_loop->initialize(
            title == nullptr ? "AnyPS1" : title,
            640,
            480,
            "",
            error)) {
        return 18;
    }
    return 0;
}

int anyps1_host_loop_poll()
{
    if (anyps1_native_host_loop == nullptr) return 18;
    anyps1::Ps1InputState input;
    std::string error;
    if (!anyps1_native_host_loop->poll(input, error)) return 18;
    return anyps1_native_host_loop->should_close() ? 1 : 0;
}

int anyps1_host_loop_present(
    const std::uint32_t* pixels,
    std::uint32_t width,
    std::uint32_t height)
{
    if (anyps1_native_host_loop == nullptr) return 18;
    std::string error;
    if (!anyps1_native_host_loop->present_framebuffer(
            pixels, width, height, error)) {
        return 18;
    }
    return 0;
}

void anyps1_host_loop_shutdown()
{
    if (anyps1_native_host_loop != nullptr) {
        anyps1_native_host_loop->shutdown();
        delete anyps1_native_host_loop;
        anyps1_native_host_loop = nullptr;
    }
}

}
