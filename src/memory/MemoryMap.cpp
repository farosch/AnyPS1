#include "memory/MemoryMap.h"

namespace anyps1 {

bool normalize_ps1_address(std::uint32_t& address)
{
    address &= 0x1FFFFFFFu;
    return true;
}

Ps1AddressInfo classify_ps1_address(
    std::uint32_t address,
    std::uint32_t executable_load,
    std::uint32_t executable_size)
{
    if (address >= 0x20000000u &&
        address < 0x80000000u) {
        return {Ps1AddressKind::Invalid, address, 0, 0};
    }
    normalize_ps1_address(address);
    if (address < 0x00200000u) {
        return {Ps1AddressKind::Ram, address, address, 0x00200000u};
    }
    if (address >= 0x1F800000u && address < 0x1F800400u) {
        return {Ps1AddressKind::Scratchpad,
                address,
                address - 0x1F800000u,
                0x400u};
    }
    if (address >= 0x1F801000u && address < 0x1F803000u) {
        return {Ps1AddressKind::Mmio,
                address,
                address - 0x1F801000u,
                0x2000u};
    }
    if (address >= 0x1FC00000u && address < 0x1FC80000u) {
        return {Ps1AddressKind::Bios,
                address,
                address - 0x1FC00000u,
                0x80000u};
    }
    if (address >= executable_load &&
        address < executable_load + executable_size) {
        return {Ps1AddressKind::Executable,
                address,
                address - executable_load,
                executable_size};
    }
    return {Ps1AddressKind::Invalid, address, 0, 0};
}

Ps1MmioInfo map_ps1_mmio(std::uint32_t address)
{
    normalize_ps1_address(address);
    if (address >= 0x1F801080u && address < 0x1F801100u) {
        return {Ps1MmioKind::Dma,
                0x1F801080u,
                0x1F801100u,
                "native host memory copy or explicit unsupported exit"};
    }
    if (address >= 0x1F801100u && address < 0x1F801130u) {
        return {Ps1MmioKind::Timers,
                0x1F801100u,
                0x1F801130u,
                "native monotonic timer or unsupported timer state"};
    }
    if (address >= 0x1F801800u && address < 0x1F801810u) {
        return {Ps1MmioKind::CdRom,
                0x1F801800u,
                0x1F801810u,
                "native disc image API or unsupported CD-ROM state"};
    }
    if (address >= 0x1F801810u && address < 0x1F801820u) {
        return {Ps1MmioKind::Gpu,
                0x1F801810u,
                0x1F801820u,
                "Vulkan graphics IR or unsupported GPU command"};
    }
    if (address >= 0x1F801820u && address < 0x1F801824u) {
        return {Ps1MmioKind::Mdec,
                0x1F801820u,
                0x1F801824u,
                "native MDEC decode or unsupported MDEC state"};
    }
    if (address >= 0x1F801C00u && address < 0x1F801D80u) {
        return {Ps1MmioKind::Spu,
                0x1F801C00u,
                0x1F801D80u,
                "SDL3 audio backend or unsupported SPU state"};
    }
    if (address >= 0x1F801040u && address < 0x1F801080u) {
        return {Ps1MmioKind::Controllers,
                0x1F801040u,
                0x1F801080u,
                "SDL3 controller input or unsupported peripheral state"};
    }
    if (address >= 0x1F801800u && address < 0x1F801820u) {
        return {Ps1MmioKind::CdRom,
                0x1F801800u,
                0x1F801820u,
                "native disc/memory-card API or unsupported state"};
    }
    return {Ps1MmioKind::Unsupported,
            address,
            address,
            "explicit unsupported MMIO conversion"};
}

}
