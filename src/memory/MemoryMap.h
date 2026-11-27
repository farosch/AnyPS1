#pragma once

#include <cstdint>
#include <string>

namespace anyps1 {

enum class Ps1AddressKind {
    Ram,
    Scratchpad,
    Mmio,
    Bios,
    Executable,
    Invalid
};

enum class Ps1MmioKind {
    MemoryControl,
    Interrupt,
    Dma,
    Timers,
    CdRom,
    Gpu,
    Mdec,
    Spu,
    Controllers,
    MemoryCard,
    Peripheral,
    Unsupported
};

struct Ps1AddressInfo {
    Ps1AddressKind kind = Ps1AddressKind::Invalid;
    std::uint32_t normalized_address = 0;
    std::uint32_t offset = 0;
    std::uint32_t size = 0;
};

struct Ps1MmioInfo {
    Ps1MmioKind kind = Ps1MmioKind::Unsupported;
    std::uint32_t base = 0;
    std::uint32_t end = 0;
    std::string native_mapping;
};

Ps1AddressInfo classify_ps1_address(
    std::uint32_t address,
    std::uint32_t executable_load,
    std::uint32_t executable_size);

Ps1MmioInfo map_ps1_mmio(std::uint32_t address);
bool normalize_ps1_address(std::uint32_t& address);

}
