#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace anyps1 {

struct Ps1DeviceRuntime {
    static constexpr std::size_t MMIO_WORDS = 0x800u;
    static constexpr std::size_t SPU_WORDS = 0xC0u;
    static constexpr std::size_t GPU_VRAM_WIDTH = 1024u;
    static constexpr std::size_t GPU_VRAM_HEIGHT = 512u;
    static constexpr std::size_t GPU_VRAM_WORDS =
        GPU_VRAM_WIDTH * GPU_VRAM_HEIGHT;

    std::uint32_t mmio[MMIO_WORDS]{};
    std::uint16_t spu[SPU_WORDS]{};
    std::uint8_t cd_parameter_fifo[16]{};
    std::uint8_t cd_response_fifo[16]{};
    std::uint8_t controller_rx[8]{};
    std::uint8_t controller_tx[8]{};
    std::uint32_t timer_cycles = 0;
    std::uint32_t frame_cycles = 0;
    std::uint32_t interrupt_status = 0;
    std::uint32_t interrupt_mask = 0;
    std::uint32_t controller_buttons = 0xFFFFu;
    std::uint32_t gpu_status = 0x14802000u;
    std::vector<std::uint16_t> gpu_vram;

    Ps1DeviceRuntime()
    {
        gpu_vram.resize(GPU_VRAM_WORDS, 0u);
    }
    std::uint32_t* ram = nullptr;
    std::uint32_t ram_word_count = 0x80000u;
    std::uint32_t cd_lba = 0;
    std::uint32_t cd_sector_cycles = 0;
    bool cd_read_active = false;
    std::size_t cd_parameter_count = 0;
    std::size_t cd_response_read = 0;
    std::size_t cd_response_count = 0;
    std::size_t controller_rx_read = 0;
    std::size_t controller_rx_count = 0;
    std::uint8_t cd_status_index = 0;
    std::uint8_t cd_irq = 0;
    std::uint8_t cd_mode = 0;
    bool frame_irq_due = false;
    std::uint8_t cd_sector_data[2048]{};
    std::size_t cd_sector_data_count = 0;
    std::size_t cd_sector_data_read = 0;
    std::uint32_t gpu_command_count = 0;
    std::uint32_t gpu_dma_count = 0;
    std::uint32_t gpu_vram_write_count = 0;
    std::uint32_t spu_write_count = 0;
    std::uint32_t gpu_transfer_phase = 0;
    std::uint32_t gpu_transfer_x = 0;
    std::uint32_t gpu_transfer_y = 0;
    std::uint32_t gpu_transfer_width = 0;
    std::uint32_t gpu_transfer_height = 0;
    std::uint32_t gpu_transfer_remaining = 0;
    std::uint32_t gpu_transfer_offset = 0;
    std::uint32_t gpu_draw_phase = 0;
    std::uint32_t gpu_draw_color = 0;
    std::uint32_t gpu_draw_payload[8]{};
    std::array<std::uint8_t, 512u * 1024u> spu_ram{};

    struct SpuVoice {
        bool active = false;
        std::uint32_t start_address = 0;
        std::uint32_t repeat_address = 0;
        std::uint32_t current_address = 0;
        std::uint32_t phase = 0;
        std::uint32_t pitch = 0x1000u;
        std::uint32_t block_offset = 0;
        std::int32_t previous_left = 0;
        std::int32_t previous_right = 0;
        std::uint16_t volume_left = 0;
        std::uint16_t volume_right = 0;
    };
    std::array<SpuVoice, 24u> voices{};

    void attach_ram(
        std::uint32_t* ram,
        std::uint32_t word_count = 0x80000u);
    void set_controller_state(std::uint32_t buttons);
    void assert_interrupt(std::uint32_t bit);
    std::uint32_t read(std::uint32_t address);
    int write(std::uint32_t address, std::uint32_t value);
    int tick(std::uint32_t cycles);
    void set_cd_sector_data(
        const std::uint8_t* data,
        std::size_t size);
    std::uint32_t get_cd_lba() const;
    const std::uint16_t* gpu_vram_data() const;
    std::uint32_t gpu_vram_write_count_value() const;
    std::uint32_t gpu_command_count_value() const;
    std::uint32_t gpu_dma_count_value() const;
    std::uint32_t spu_write_count_value() const;
    std::uint32_t mix_spu(
        std::int16_t* left,
        std::int16_t* right,
        std::size_t frame_count);

private:
    std::uint32_t& register_at(std::uint32_t address);
    void execute_cd_command(std::uint8_t command);
    void execute_controller_transfer();
    void execute_gpu_command(std::uint32_t word);
    void execute_dma_channel(std::uint32_t channel);
};

extern "C" {
int anyps1_host_loop_attach_ram(std::uint32_t* ram);
int anyps1_host_loop_set_controller(std::uint32_t buttons);
int anyps1_host_loop_device_tick(std::uint32_t cycles);
std::uint32_t anyps1_device_mmio_read(std::uint32_t address);
int anyps1_device_mmio_write(
    std::uint32_t address,
    std::uint32_t value);
}

}
