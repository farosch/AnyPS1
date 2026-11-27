#include "host/Ps1DeviceRuntime.h"

#include <cstdint>

#ifdef _MSC_VER
extern "C" void _RTC_CheckStackVars(void*, void*) {}
#endif

namespace {

anyps1::Ps1DeviceRuntime device_runtime;

}

namespace anyps1 {

std::uint32_t& Ps1DeviceRuntime::register_at(std::uint32_t address)
{
    const std::uint32_t normalized = address & 0x1FFFFFFFu;
    if (normalized < 0x1F801000u || normalized >= 0x1F802000u) {
        static std::uint32_t invalid_register = 0;
        invalid_register = 0;
        return invalid_register;
    }
    return mmio[(normalized - 0x1F801000u) / 4u];
}

void Ps1DeviceRuntime::attach_ram(
    std::uint32_t* value,
    std::uint32_t word_count)
{
    ram = value;
    ram_word_count = word_count;
}

void Ps1DeviceRuntime::set_controller_state(std::uint32_t buttons)
{
    controller_buttons = buttons;
}

void Ps1DeviceRuntime::assert_interrupt(std::uint32_t bit)
{
    interrupt_status |= bit;
}

void Ps1DeviceRuntime::queue_cd_response(
    const std::uint8_t* values,
    std::size_t count)
{
    if (count > 16u) count = 16u;
    for (std::size_t index = 0; index < count; ++index) {
        cd_response_fifo[index] = values[index];
    }
    cd_response_read = 0;
    cd_response_count = count;
}

void Ps1DeviceRuntime::execute_cd_command(std::uint8_t command)
{
    if (command == 0x01u) {
        const std::uint8_t response[] = {
            static_cast<std::uint8_t>(cd_mode | 0x02u)
        };
        queue_cd_response(response, 1u);
        cd_irq = 3u;
    } else if (command == 0x02u) {
        if (cd_parameter_count >= 3u) {
            const std::uint32_t minute =
                ((cd_parameter_fifo[0] >> 4) * 10u) +
                (cd_parameter_fifo[0] & 0x0Fu);
            const std::uint32_t second =
                ((cd_parameter_fifo[1] >> 4) * 10u) +
                (cd_parameter_fifo[1] & 0x0Fu);
            const std::uint32_t sector =
                ((cd_parameter_fifo[2] >> 4) * 10u) +
                (cd_parameter_fifo[2] & 0x0Fu);
            cd_lba =
                minute * 60u * 75u + second * 75u + sector;
        } else {
            cd_lba = 0u;
        }
        const std::uint8_t response[] = {0x02u};
        queue_cd_response(response, 1u);
        cd_irq = 3u;
    } else if (command == 0x06u) {
        cd_mode |= 0x80u;
        const std::uint8_t response[] = {0x02u};
        queue_cd_response(response, 1u);
        cd_irq = 1u;
    } else if (command == 0x08u) {
        const std::uint8_t response[] = {0x02u};
        queue_cd_response(response, 1u);
        cd_irq = 3u;
    } else if (command == 0x09u) {
        cd_mode &= ~0x80u;
        const std::uint8_t response[] = {0x02u};
        queue_cd_response(response, 1u);
        cd_irq = 3u;
    } else if (command == 0x0Au) {
        const std::uint8_t response[] = {0x02u};
        queue_cd_response(response, 1u);
        cd_irq = 3u;
    } else if (command == 0x19u && cd_parameter_count > 0u &&
               cd_parameter_fifo[0] == 0x20u) {
        const std::uint8_t response[] = {0x95u, 0x03u, 0x00u};
        queue_cd_response(response, 3u);
        cd_irq = 3u;
    } else {
        const std::uint8_t response[] = {0x11u};
        queue_cd_response(response, 1u);
        cd_irq = 5u;
    }
    cd_parameter_count = 0;
}

void Ps1DeviceRuntime::execute_controller_transfer()
{
    controller_rx_read = 0;
    if (controller_tx[0] == 0x81u) {
        controller_rx_count = 5u;
        controller_rx[0] = 0xFFu;
        controller_rx[1] = 0xFFu;
        controller_rx[2] = 0x5Au;
        controller_rx[3] = 0x5Du;
        controller_rx[4] = 0x00u;
        return;
    }
    controller_rx_count = 5;
    controller_rx[0] = 0x00u;
    controller_rx[1] = 0x41u;
    controller_rx[2] = 0x5Au;
    controller_rx[3] =
        static_cast<std::uint8_t>(controller_buttons);
    controller_rx[4] =
        static_cast<std::uint8_t>(controller_buttons >> 8);
}

void Ps1DeviceRuntime::execute_gpu_command(std::uint32_t word)
{
    const std::uint32_t command = word >> 24;
    if (command == 0x00u || command == 0x01u) {
        gpu_status = 0x14802000u;
    } else if (command == 0xE6u) {
        gpu_status |= 1u << 26;
    } else if (command >= 0x20u && command <= 0x7Fu) {
        gpu_status |= 1u << 26;
    }
}

void Ps1DeviceRuntime::execute_dma_channel(std::uint32_t channel)
{
    if (ram == nullptr || channel > 6u) return;
    const std::uint32_t base = 0x1F801080u + channel * 0x10u;
    std::uint32_t control = register_at(base + 8u);
    if ((control & 0x01000000u) == 0) return;
    const std::uint32_t address =
        register_at(base) & 0x001FFFFCu;
    const std::uint32_t block = register_at(base + 4u);
    std::uint32_t words = block & 0xFFFFu;
    std::uint32_t blocks = (block >> 16) & 0xFFFFu;
    if (blocks == 0) blocks = 1u;
    if (words == 0) words = 1u;
    const std::uint64_t count64 =
        static_cast<std::uint64_t>(words) * blocks;
    if (count64 > 0x80000u) {
        control &= ~0x01000000u;
        register_at(base + 8u) = control;
        return;
    }
    const std::uint32_t count = static_cast<std::uint32_t>(count64);
    if (static_cast<std::uint64_t>(address) +
            static_cast<std::uint64_t>(count) * 4u >
        0x200000u) {
        control &= ~0x01000000u;
        register_at(base + 8u) = control;
        return;
    }
    const bool to_ram = (control & 1u) == 0;
    if (address / 4u > ram_word_count ||
        count > ram_word_count - address / 4u) {
        control &= ~0x01000000u;
        register_at(base + 8u) = control;
        return;
    }
    for (std::uint32_t index = 0; index < count; ++index) {
        const std::uint32_t ram_index =
            address / 4u + index;
        if (to_ram) {
            ram[ram_index] = 0;
        } else if (channel == 2u) {
            execute_gpu_command(ram[ram_index]);
        } else if (channel == 4u) {
            const std::uint32_t spu_address =
                (index * 2u) % SPU_WORDS;
            spu[spu_address] =
                static_cast<std::uint16_t>(ram[ram_index]);
        }
    }
    control &= ~0x01000000u;
    register_at(base + 8u) = control;
    interrupt_status |= 0x8u;
    register_at(0x1F8010F4u) = 0x00800000u |
        (1u << (16u + channel)) | channel;
}

std::uint32_t Ps1DeviceRuntime::read(std::uint32_t address)
{
    const std::uint32_t normalized = address & 0x1FFFFFFFu;
    if (normalized < 0x1F801000u ||
        normalized >= 0x1F802000u) {
        return 0;
    }
    if (normalized == 0x1F801070u) return interrupt_status;
    if (normalized == 0x1F801074u) return interrupt_mask;
    if (normalized >= 0x1F801100u &&
        normalized < 0x1F801130u) {
        const std::uint32_t timer =
            (normalized - 0x1F801100u) / 16u;
        const std::uint32_t offset =
            (normalized - 0x1F801100u) % 16u;
        if (offset == 0u) return timer_cycles + timer * 0x400u;
        return register_at(normalized);
    }
    if (normalized >= 0x1F801C00u &&
        normalized < 0x1F801C00u + SPU_WORDS * 2u) {
        return spu[(normalized - 0x1F801C00u) / 2u];
    }
    if (normalized == 0x1F801040u) {
        if (controller_rx_read < controller_rx_count) {
            return controller_rx[controller_rx_read++];
        }
        return 0xFFu;
    }
    if (normalized == 0x1F801044u) {
        return (controller_rx_count > controller_rx_read ? 0x02u : 0u);
    }
    if (normalized == 0x1F801814u) return gpu_status;
    if (normalized >= 0x1F801800u &&
        normalized < 0x1F801804u) {
        const std::uint32_t index = cd_status_index & 3u;
        if (normalized == 0x1F801800u) {
            return index |
                (cd_parameter_count < 16u ? 0x08u : 0u) |
                (cd_response_count > cd_response_read ? 0x20u : 0u) |
                0x02u;
        }
        if (index == 1u) {
            if (cd_response_read < cd_response_count) {
                return cd_response_fifo[cd_response_read++];
            }
            return 0;
        }
        if (index == 3u) return cd_irq | 0x10u;
        return 0;
    }
    return register_at(normalized);
}

int Ps1DeviceRuntime::write(
    std::uint32_t address,
    std::uint32_t value)
{
    const std::uint32_t normalized = address & 0x1FFFFFFFu;
    if (normalized < 0x1F801000u ||
        normalized >= 0x1F802000u) {
        return -1;
    }
    if (normalized == 0x1F801070u) {
        interrupt_status &= ~value;
        return 0;
    }
    if (normalized == 0x1F801074u) {
        interrupt_mask = value;
        return 0;
    }
    if (normalized >= 0x1F801C00u &&
        normalized < 0x1F801C00u + SPU_WORDS * 2u) {
        spu[(normalized - 0x1F801C00u) / 2u] =
            static_cast<std::uint16_t>(value);
        return 0;
    }
    if (normalized == 0x1F801040u) {
        controller_tx[0] = static_cast<std::uint8_t>(value);
        execute_controller_transfer();
        return 0;
    }
    if (normalized == 0x1F801048u) {
        if (normalized < 0x1F801000u ||
            normalized >= 0x1F802000u) {
            return -1;
        }
        register_at(normalized) = value;
        execute_controller_transfer();
        return 0;
    }
    if (normalized == 0x1F801810u) {
        execute_gpu_command(value);
        return 0;
    }
    if (normalized == 0x1F801814u) {
        const std::uint32_t command = value >> 24;
        if (command == 0x02u) interrupt_status &= ~0x1u;
        if (command == 0x00u) gpu_status = 0x14802000u;
        register_at(normalized) = value;
        return 0;
    }
    if (normalized >= 0x1F801800u &&
        normalized < 0x1F801804u) {
        if (normalized == 0x1F801800u) {
            cd_status_index = static_cast<std::uint8_t>(value & 3u);
            return 0;
        }
        if (cd_status_index == 0u) {
            execute_cd_command(static_cast<std::uint8_t>(value));
        } else if (cd_status_index == 2u &&
                   cd_parameter_count < 16u) {
            cd_parameter_fifo[cd_parameter_count++] =
                static_cast<std::uint8_t>(value);
        } else if (cd_status_index == 3u) {
            cd_irq &= 0;
            if ((value & 0x40u) != 0) cd_parameter_count = 0;
        }
        return 0;
    }
    register_at(normalized) = value;
    if (normalized >= 0x1F801080u &&
        normalized < 0x1F801100u &&
        (normalized & 15u) == 8u &&
        (value & 0x01000000u) != 0) {
        execute_dma_channel((normalized - 0x1F801080u) / 16u);
    }
    return 0;
}

int Ps1DeviceRuntime::tick(std::uint32_t cycles)
{
    timer_cycles += cycles;
    frame_cycles += cycles;
    while (frame_cycles >= 1024u) {
        frame_cycles -= 1024u;
        frame_irq_due = true;
    }
    if (frame_irq_due) {
        frame_irq_due = false;
        interrupt_status |= 0x1u;
    }
    for (std::uint32_t channel = 0; channel < 7u; ++channel) {
        execute_dma_channel(channel);
    }
    if (cd_irq != 0) interrupt_status |= 0x4u;
    if ((gpu_status & (1u << 26)) != 0) {
        gpu_status &= ~(1u << 26);
    }
    return 0;
}



extern "C" std::uint32_t anyps1_device_mmio_read(
    std::uint32_t address)
{
    return device_runtime.read(address);
}

extern "C" int anyps1_device_mmio_write(
    std::uint32_t address,
    std::uint32_t value)
{
    return device_runtime.write(address, value);
}

extern "C" int anyps1_host_loop_set_controller(
    std::uint32_t buttons)
{
    device_runtime.set_controller_state(buttons);
    return 0;
}

extern "C" int anyps1_host_loop_device_tick(
    std::uint32_t cycles)
{
    return device_runtime.tick(cycles);
}

extern "C" int anyps1_host_loop_attach_ram(
    std::uint32_t* ram)
{
    device_runtime.attach_ram(ram);
    return 0;
}

}
