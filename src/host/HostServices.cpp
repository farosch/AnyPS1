#include "host/HostServices.h"

#include <SDL3/SDL.h>

#include <cstdint>
#include <cstdio>
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
        return {kind, "PS1 CD-ROM registers", "native disc/image API or explicit unsupported exit", false};
    case HostServiceKind::Timers:
        return {kind, "PS1 timers", "native monotonic timer or explicit unsupported exit", false};
    case HostServiceKind::Controllers:
        return {kind, "PS1 controller ports", "SDL3 input or explicit unsupported exit", true};
    case HostServiceKind::MemoryCard:
        return {kind, "PS1 memory-card ports", "native storage mapping or explicit unsupported exit", false};
    case HostServiceKind::Spu:
        return {kind, "PS1 SPU", "SDL3 audio backend or explicit unsupported exit", false};
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
        close(entry.second);
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
    const int result = close(entry->second);
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

}
