#pragma once

#include <array>
#include <cstdint>
#include <string>

namespace anyps1 {

enum class TargetPlatform {
    Linux,
    Windows
};

struct RelinkOptions {
    std::string input;
    std::string output;
    TargetPlatform target = TargetPlatform::Linux;
};

bool relink(const RelinkOptions& options);

struct RunOptions {
    std::string input;
    std::string cdrom_bin;
    std::string memory_card;
    std::uint64_t max_instructions = 1000000;
    bool video = false;
};

struct ControllerState {
    std::uint16_t digital = 0xFFFF;
    std::uint8_t left_x = 0x80;
    std::uint8_t left_y = 0x80;
    std::uint8_t right_x = 0x80;
    std::uint8_t right_y = 0x80;
};

struct RuntimeSnapshot {
    std::uint32_t pc = 0;
    std::array<std::uint32_t, 32> registers{};
    std::array<std::uint32_t, 32> cop0{};
    std::array<std::int32_t, 32> gte_data{};
    std::array<std::uint32_t, 32> gte_control{};
    std::uint32_t gpu_status = 0;
    std::uint32_t gpu_write_count = 0;
    std::uint32_t gpu_vram_write_count = 0;
    std::uint32_t gpuread = 0;
    std::uint32_t gpu_draw_x1 = 0;
    std::uint32_t gpu_draw_y1 = 0;
    std::uint32_t gpu_draw_x2 = 0;
    std::uint32_t gpu_draw_y2 = 0;
    std::uint32_t gpu_tex_window = 0;
    std::uint32_t display_x = 0;
    std::uint32_t display_y = 0;
    std::uint32_t display_width = 0;
    std::uint32_t display_height = 0;
    std::array<std::uint32_t, 7> dma_madr{};
    std::array<std::uint32_t, 7> dma_bcr{};
    std::array<std::uint32_t, 7> dma_chcr{};
    std::uint32_t dma_otc_first = 0;
    std::uint32_t dma_otc_last = 0;
    std::uint32_t interrupt_status = 0;
    std::uint32_t interrupt_mask = 0;
    std::array<std::uint32_t, 3> timer_count{};
    std::array<std::uint32_t, 3> timer_target{};
    std::array<std::uint32_t, 3> timer_mode{};
    std::uint32_t cdrom_status = 0;
    std::uint32_t cdrom_lba = 0;
    std::uint32_t cdrom_sector_size = 0;
    std::uint32_t cdrom_response_count = 0;
    std::array<unsigned char, 16> cdrom_params{};
    bool cdrom_read_active = false;
    std::uint32_t spu_control = 0;
    std::uint32_t spu_status = 0;
    std::uint32_t spu_key_on = 0;
    std::array<std::uint16_t, 24> spu_voice_volume_l{};
    std::array<std::uint16_t, 24> spu_voice_volume_r{};
    std::array<std::uint32_t, 24> spu_voice_address{};
    std::uint32_t joy_stat = 0;
    std::uint32_t joy_ctrl = 0;
    std::uint32_t joy_mode = 0;
    std::uint32_t joy_baud = 0;
    std::uint32_t joy_rx_count = 0;
    std::array<unsigned char, 8> joy_rx{};
    bool joy_analog_mode = false;
    bool joy_config_mode = false;
    std::uint32_t joy_motor_small = 0;
    std::uint32_t joy_motor_large = 0;
    std::array<unsigned char, 128 * 1024> memory_card{};
    std::uint32_t memory_card_write_count = 0;
};

bool run_psx(const RunOptions& options, RuntimeSnapshot* snapshot = nullptr);

}
