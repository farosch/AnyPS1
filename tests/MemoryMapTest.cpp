#include "memory/MemoryMap.h"

#include <cstdint>
#include <iostream>
#include <stdexcept>

int main()
{
    try {
        std::uint32_t ram_address = 0xA0001000u;
        if (!anyps1::normalize_ps1_address(ram_address) ||
            ram_address != 0x00001000u) {
            throw std::runtime_error("PS1 RAM address normalization failed");
        }

        const anyps1::Ps1AddressInfo ram =
            anyps1::classify_ps1_address(
                0x80001000u,
                0x80010000u,
                0x100u);
        if (ram.kind != anyps1::Ps1AddressKind::Ram ||
            ram.normalized_address != 0x00001000u) {
            throw std::runtime_error("RAM address classification failed");
        }
        const anyps1::Ps1AddressInfo mirrored_ram =
            anyps1::classify_ps1_address(
                0xA0600010u,
                0x80010000u,
                0x100u);
        if (mirrored_ram.kind != anyps1::Ps1AddressKind::Ram ||
            mirrored_ram.offset != 0x10u) {
            throw std::runtime_error("mirrored RAM classification failed");
        }

        const anyps1::Ps1AddressInfo scratch =
            anyps1::classify_ps1_address(
                0x1F800020u,
                0x80010000u,
                0x100u);
        if (scratch.kind != anyps1::Ps1AddressKind::Scratchpad ||
            scratch.offset != 0x20u) {
            throw std::runtime_error("scratchpad address classification failed");
        }

        const anyps1::Ps1MmioInfo dma =
            anyps1::map_ps1_mmio(0x1F801080u);
        if (dma.kind != anyps1::Ps1MmioKind::Dma ||
            dma.base != 0x1F801080u) {
            throw std::runtime_error("DMA MMIO classification failed");
        }

        const anyps1::Ps1MmioInfo gpu =
            anyps1::map_ps1_mmio(0x1F801810u);
        if (gpu.kind != anyps1::Ps1MmioKind::Gpu) {
            throw std::runtime_error("GPU MMIO classification failed");
        }

        const anyps1::Ps1AddressInfo invalid =
            anyps1::classify_ps1_address(
                0x20000000u,
                0x80010000u,
                0x100u);
        if (invalid.kind != anyps1::Ps1AddressKind::Invalid) {
            throw std::runtime_error("invalid address was not rejected");
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Memory map test failed: " << error.what() << "\n";
        return 1;
    }
}
