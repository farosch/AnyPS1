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
    }
    return {kind, "unknown PS1 host service", "unsupported", false};
}

}
