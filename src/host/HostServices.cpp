#include "host/HostServices.h"

namespace anyps1 {

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
        return {kind, "PS1 SPU", "SDL3 audio backend", false};
    case HostServiceKind::Video:
        return {kind, "PS1 GPU/VRAM", "Vulkan framebuffer", false};
    case HostServiceKind::VulkanFramebuffer:
        return {kind, "PS1 framebuffer", "Vulkan swapchain/resources", false};
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

}
