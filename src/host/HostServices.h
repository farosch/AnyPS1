#pragma once

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

}
