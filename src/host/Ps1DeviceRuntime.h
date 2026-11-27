#pragma once

#include <cstddef>
#include <cstdint>

namespace anyps1 {

struct Ps1DeviceRuntime {
    static constexpr std::size_t MMIO_WORDS = 0x800u;
    static constexpr std::size_t SPU_WORDS = 0xC0u;

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
    std::uint32_t* ram = nullptr;
    std::uint32_t ram_word_count = 0x80000u;
    std::uint32_t cd_lba = 0;
    std::size_t cd_parameter_count = 0;
    std::size_t cd_response_read = 0;
    std::size_t cd_response_count = 0;
    std::size_t controller_rx_read = 0;
    std::size_t controller_rx_count = 0;
    std::uint8_t cd_status_index = 0;
    std::uint8_t cd_irq = 0;
    std::uint8_t cd_mode = 0;
    bool frame_irq_due = false;

    void attach_ram(
        std::uint32_t* ram,
        std::uint32_t word_count = 0x80000u);
    void set_controller_state(std::uint32_t buttons);
    void assert_interrupt(std::uint32_t bit);
    std::uint32_t read(std::uint32_t address);
    int write(std::uint32_t address, std::uint32_t value);
    int tick(std::uint32_t cycles);

private:
    std::uint32_t& register_at(std::uint32_t address);
    void queue_cd_response(const std::uint8_t* values, std::size_t count);
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
