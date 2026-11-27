#include "host/HostServices.h"

#include <cstddef>
#include <cstdint>
#include <string>

namespace {

anyps1::NativeHostLoopRuntime* native_host_loop = nullptr;

int host_runtime_error()
{
    return static_cast<int>(anyps1::HostExitCode::HostRuntimeError);
}

}

extern "C" {

int anyps1_host_loop_init(const char* title)
{
    if (native_host_loop == nullptr) {
        native_host_loop = new anyps1::NativeHostLoopRuntime();
    }
    std::string error;
    return native_host_loop->initialize(
        title == nullptr ? "AnyPS1" : title,
        640,
        480,
        "",
        error) ? 0 : host_runtime_error();
}

int anyps1_host_loop_poll()
{
    if (native_host_loop == nullptr) return host_runtime_error();
    anyps1::Ps1InputState input;
    std::string error;
    if (!native_host_loop->poll(input, error)) return host_runtime_error();
    return native_host_loop->should_close() ? 1 : 0;
}

int anyps1_host_loop_present(
    const std::uint32_t* pixels,
    std::uint32_t width,
    std::uint32_t height)
{
    if (native_host_loop == nullptr) return host_runtime_error();
    std::string error;
    return native_host_loop->present_framebuffer(
        pixels,
        width,
        height,
        error) ? 0 : host_runtime_error();
}

std::uint32_t anyps1_host_loop_controller_state()
{
    return native_host_loop == nullptr
        ? 0xFFFFu
        : native_host_loop->controller_state();
}

int anyps1_host_loop_spu_write(
    std::uint32_t address,
    std::uint32_t value)
{
    return native_host_loop == nullptr
        ? host_runtime_error()
        : native_host_loop->spu_write(address, value);
}

std::uint32_t anyps1_host_loop_spu_read(std::uint32_t address)
{
    return native_host_loop == nullptr
        ? 0u
        : native_host_loop->spu_read(address);
}

int anyps1_host_loop_audio_queue(
    const std::int16_t* samples,
    std::size_t sample_count)
{
    if (native_host_loop == nullptr) return host_runtime_error();
    std::string error;
    return native_host_loop->queue_audio(
        samples,
        sample_count,
        error) ? 0 : host_runtime_error();
}

int anyps1_host_loop_gpu_write(
    std::uint32_t address,
    std::uint32_t value)
{
    return native_host_loop == nullptr
        ? host_runtime_error()
        : native_host_loop->gpu_write(address, value);
}

std::uint32_t anyps1_host_loop_mmio_read(std::uint32_t address)
{
    return native_host_loop == nullptr
        ? 0u
        : native_host_loop->mmio_read(address);
}

int anyps1_host_loop_mmio_write(
    std::uint32_t address,
    std::uint32_t value)
{
    return native_host_loop == nullptr
        ? host_runtime_error()
        : native_host_loop->mmio_write(address, value);
}

int anyps1_host_loop_tick(std::uint32_t cycles)
{
    return native_host_loop == nullptr
        ? host_runtime_error()
        : native_host_loop->tick(cycles);
}

int anyps1_host_loop_vulkan_init(
    const char* title,
    std::uint32_t width,
    std::uint32_t height)
{
    if (native_host_loop == nullptr) {
        native_host_loop = new anyps1::NativeHostLoopRuntime();
    }
    std::string error;
    return native_host_loop->vulkan_init(
        title == nullptr ? "AnyPS1" : title,
        width,
        height,
        error);
}

int anyps1_host_loop_vulkan_present()
{
    if (native_host_loop == nullptr) return host_runtime_error();
    std::string error;
    return native_host_loop->vulkan_present(error);
}

int anyps1_host_loop_memory_card_open(int slot, const char* path)
{
    if (native_host_loop == nullptr) return host_runtime_error();
    std::string error;
    return native_host_loop->open_memory_card(
        slot,
        path == nullptr ? "" : path,
        error) ? 0 : host_runtime_error();
}

int anyps1_host_loop_memory_card_read(
    int slot,
    std::uint32_t block,
    std::uint32_t offset)
{
    static std::uint8_t scratch[2][128]{};
    if (native_host_loop == nullptr ||
        slot < 0 || slot >= 2 ||
        block >= 1024u ||
        offset > 124u) {
        return -1;
    }
    std::string error;
    if (!native_host_loop->read_memory_card(
            slot,
            block,
            scratch[slot],
            128u,
            error)) {
        return -1;
    }
    const std::uint32_t value =
        static_cast<std::uint32_t>(scratch[slot][offset])
        | (static_cast<std::uint32_t>(scratch[slot][offset + 1u]) << 8)
        | (static_cast<std::uint32_t>(scratch[slot][offset + 2u]) << 16)
        | (static_cast<std::uint32_t>(scratch[slot][offset + 3u]) << 24);
    return static_cast<int>(value);
}

int anyps1_host_loop_memory_card_write(
    int slot,
    std::uint32_t block,
    std::uint32_t offset,
    std::uint32_t value)
{
    static std::uint8_t scratch[2][128]{};
    if (native_host_loop == nullptr ||
        slot < 0 || slot >= 2 ||
        block >= 1024u ||
        offset > 124u) {
        return host_runtime_error();
    }
    std::string error;
    if (!native_host_loop->read_memory_card(
            slot,
            block,
            scratch[slot],
            128u,
            error)) {
        return host_runtime_error();
    }
    scratch[slot][offset] = static_cast<std::uint8_t>(value);
    scratch[slot][offset + 1u] = static_cast<std::uint8_t>(value >> 8);
    scratch[slot][offset + 2u] = static_cast<std::uint8_t>(value >> 16);
    scratch[slot][offset + 3u] = static_cast<std::uint8_t>(value >> 24);
    return native_host_loop->write_memory_card(
        slot,
        block,
        scratch[slot],
        128u,
        error) ? 0 : host_runtime_error();
}

void anyps1_host_loop_memory_card_close(int slot)
{
    if (native_host_loop != nullptr) {
        native_host_loop->close_memory_card(slot);
    }
}

int anyps1_host_loop_disc_open(const char* path, int mode2)
{
    if (native_host_loop == nullptr) return host_runtime_error();
    std::string error;
    return native_host_loop->open_disc(
        path == nullptr ? "" : path,
        mode2 != 0,
        error) ? 0 : host_runtime_error();
}

std::uint32_t anyps1_host_loop_disc_read(
    std::uint32_t lba,
    std::uint32_t offset)
{
    static std::uint8_t sector[2048]{};
    if (native_host_loop == nullptr || offset > 2044u) return 0u;
    std::string error;
    if (!native_host_loop->read_disc(
            lba,
            sector,
            2048u,
            error)) {
        return 0u;
    }
    return static_cast<std::uint32_t>(sector[offset])
        | (static_cast<std::uint32_t>(sector[offset + 1u]) << 8)
        | (static_cast<std::uint32_t>(sector[offset + 2u]) << 16)
        | (static_cast<std::uint32_t>(sector[offset + 3u]) << 24);
}

void anyps1_host_loop_disc_close()
{
    if (native_host_loop != nullptr) native_host_loop->close_disc();
}

void anyps1_host_loop_shutdown()
{
    if (native_host_loop != nullptr) {
        native_host_loop->shutdown();
        delete native_host_loop;
        native_host_loop = nullptr;
    }
}

}
