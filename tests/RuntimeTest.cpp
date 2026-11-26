#include "relinker/Relinker.h"

#include <cstdint>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

void require(bool condition, const char* message)
{
    if (!condition) {
        throw std::runtime_error(message);
    }
}

anyps1::RuntimeSnapshot run_fixture(const std::filesystem::path& fixtures, const char* name,
    std::uint64_t instructions = 100000)
{
    anyps1::RunOptions options;
    options.input = (fixtures / name).string();
    options.max_instructions = instructions;
    anyps1::RuntimeSnapshot snapshot;
    if (!anyps1::run_psx(options, &snapshot)) {
        throw std::runtime_error(std::string("runtime stopped for fixture ") + name);
    }
    return snapshot;
}

void test_mips(const std::filesystem::path& fixtures)
{
    const auto snapshot = run_fixture(fixtures, "runtime-mips.ps-exe");
    require(snapshot.registers[8] == 0x12345678, "MIPS LUI/ORI result");
    require(snapshot.registers[9] == 0x2468acf0, "MIPS addu result");
    require(snapshot.registers[10] == 0x12345678, "MIPS load result");
    require(snapshot.registers[11] == 0x80010084, "MIPS post-load address");
}

void test_load_delay(const std::filesystem::path& fixtures)
{
    const auto snapshot = run_fixture(fixtures, "runtime-load-delay.ps-exe");
    require(snapshot.registers[10] == 0x12345678, "load-delay register value");
}

void test_cop0(const std::filesystem::path& fixtures)
{
    const auto snapshot = run_fixture(fixtures, "runtime-cop0.ps-exe", 20);
    require(snapshot.cop0[12] == 4, "COP0 status after exception");
    require((snapshot.cop0[13] & 0xFCu) == 32, "COP0 cause code");
    require(snapshot.cop0[14] == 0x80010008, "COP0 exception EPC");
}

void test_gte(const std::filesystem::path& fixtures)
{
    const auto snapshot = run_fixture(fixtures, "runtime-gte.ps-exe");
    require(snapshot.gte_control[24] == 0x12345678, "GTE data transfer result");
    require(snapshot.gte_control[25] == 0x00005678, "GTE y result");
    require(snapshot.gte_control[26] == 0xFFFFFFF6, "GTE z result");
}

void test_gpu(const std::filesystem::path& fixtures)
{
    const auto snapshot = run_fixture(fixtures, "runtime-gpu.ps-exe");
    require(snapshot.gpu_write_count == 2, "GP0 command count");
    require(snapshot.gpu_tex_window == 0x00123456, "GP0 texture window");
    require(snapshot.gpu_status != 0, "GPU status");
    require(snapshot.display_x == 0x123, "GP1 display x");
}

void test_dma(const std::filesystem::path& fixtures)
{
    const auto snapshot = run_fixture(fixtures, "runtime-dma.ps-exe");
    std::cerr << "DMA OTC first=" << snapshot.dma_otc_first
              << " last=" << snapshot.dma_otc_last << "\n";
    require(snapshot.dma_madr[6] == 0x00000100, "DMA OTC base address");
    require(snapshot.dma_bcr[6] == 2, "DMA OTC block count");
    require(snapshot.dma_otc_first == 0x000000FC, "DMA OTC first chain value");
    require(snapshot.dma_otc_last == 0x00FFFFFF, "DMA OTC terminal chain value");
}

void test_cdrom(const std::filesystem::path& fixtures)
{
    const auto snapshot = run_fixture(fixtures, "runtime-cdrom.ps-exe");
    require(snapshot.cdrom_lba == 4653, "CD-ROM Setloc LBA");
    require(snapshot.cdrom_read_active, "CD-ROM ReadN active state");
    require(snapshot.cdrom_status == 0x23, "CD-ROM ReadN status");
    require(snapshot.cdrom_sector_size == 2352, "CD-ROM sector size");
}

void test_spu(const std::filesystem::path& fixtures)
{
    const auto snapshot = run_fixture(fixtures, "runtime-spu.ps-exe", 100);
    require(snapshot.spu_voice_volume_l[0] == 0x1234, "SPU voice left volume");
    require(snapshot.spu_voice_volume_r[0] == 0x5678, "SPU voice right volume");
    require(snapshot.spu_voice_address[0] == 0x400, "SPU voice start address");
    require(snapshot.spu_key_on == 1, "SPU key-on state");
}

void test_timers(const std::filesystem::path& fixtures)
{
    const auto snapshot = run_fixture(fixtures, "runtime-timers.ps-exe", 10020);
    require(snapshot.timer_target[0] == 1, "timer target");
    require(snapshot.timer_mode[0] == 0x410, "timer IRQ mode and IRQ flag");
    require(snapshot.timer_count[0] >= 1, "timer count");
    require((snapshot.interrupt_status & (1u << 10)) != 0, "timer interrupt line");
}

void test_controller(const std::filesystem::path& fixtures)
{
    const auto snapshot = run_fixture(fixtures, "runtime-controller.ps-exe");
    require(snapshot.joy_ctrl == 0x2000, "controller transfer control");
    require(snapshot.joy_config_mode, "controller config mode");
    require(snapshot.joy_analog_mode, "DualShock analog mode");
    require(snapshot.joy_rx_count == 8, "DualShock receive data count");
    require(snapshot.joy_rx[0] == 0x73 && snapshot.joy_rx[1] == 0x5A,
        "DualShock analog handshake response");
}

void test_memory_card(const std::filesystem::path& fixtures)
{
    const auto snapshot = run_fixture(fixtures, "runtime-memory-card.ps-exe");
    require(snapshot.memory_card_write_count == 1, "memory card write count");
    require(snapshot.memory_card[0] == 0xAA, "memory card first write byte");
}

}

int main(int argc, char** argv)
{
    if (argc != 2) {
        std::cerr << "Usage: anyps1_runtime_test <fixtures/runtime>\n";
        return 1;
    }
    try {
        const std::filesystem::path fixtures(argv[1]);
        test_mips(fixtures);
        test_load_delay(fixtures);
        test_cop0(fixtures);
        test_gte(fixtures);
        test_gpu(fixtures);
        test_dma(fixtures);
        test_cdrom(fixtures);
        test_spu(fixtures);
        test_timers(fixtures);
        test_controller(fixtures);
        test_memory_card(fixtures);
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Runtime test failed: " << error.what() << "\n";
        return 1;
    }
}
