#include "Relinker.h"
#include "HostVideo.h"
#include "HostAudio.h"

#include <array>
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <memory>
#include <cstdlib>
#include <stdexcept>
#include <vector>

namespace anyps1 {

namespace {

std::uint32_t read32(const std::vector<unsigned char>& data, std::size_t offset)
{
    if (offset + 4 > data.size()) {
        throw std::runtime_error("truncated PS-X EXE header");
    }
    return static_cast<std::uint32_t>(data[offset])
        | (static_cast<std::uint32_t>(data[offset + 1]) << 8)
        | (static_cast<std::uint32_t>(data[offset + 2]) << 16)
        | (static_cast<std::uint32_t>(data[offset + 3]) << 24);
}

std::uint32_t sign16(std::uint32_t value)
{
    return (value & 0x8000) ? (0xFFFF0000 | value) : value;
}

class PsxCpu {
public:
    explicit PsxCpu(const std::vector<unsigned char>& exe)
    {
        if (exe.size() < 2048 || std::memcmp(exe.data(), "PS-X EXE", 8) != 0) {
            throw std::runtime_error("input is not a PS-X EXE");
        }
        pc_ = read32(exe, 0x10);
        regs_[28] = read32(exe, 0x14);
        load_address_ = read32(exe, 0x18);
        file_size_ = read32(exe, 0x1C);
        sp_base_ = read32(exe, 0x30);
        sp_offset_ = read32(exe, 0x34);
        if (load_address_ < 0x80000000 || load_address_ >= 0x80200000) {
            throw std::runtime_error("unsupported PS-X load address");
        }
        if (2048 + static_cast<std::uint64_t>(file_size_) > exe.size()) {
            throw std::runtime_error("PS-X executable section is truncated");
        }
        std::memcpy(ram_.data() + (load_address_ & 0x001FFFFF), exe.data() + 2048, file_size_);
        regs_[29] = sp_base_ + sp_offset_;
        ram_[0x474] = 0x00;
        ram_[0x475] = 0xF0;
        ram_[0x476] = 0x1F;
        ram_[0x477] = 0x00;
        ram_[0x1FF000] = 0x01;
        ram_[0x1FF001] = 0x80;
        ram_[0x1FF004] = 0x08;
        ram_[0x1FF005] = 0x20;
        write_ram32(0x00100000, 0);
        write_ram32(0x00100004, 0);
        write_ram32(0x00100008, 0);
        write_ram32(0x00A9B30, 0x80001000);
        write_ram32(0x00A9B34, 0x80001004);
        write_ram32(0x00A9B38, 0x80001008);
        write_ram32(0x003D374, 0);
    }

    bool step()
    {
        if (execute_bios_vector()) {
            return true;
        }
        if ((cop0_[12] & 1) != 0 && (cop0_[12] & cop0_[13] & 0x0400) != 0) {
            raise_exception(0);
            return true;
        }
        if ((pc_ & 0xF0000000) == 0xA0000000
            || (pc_ & 0xF0000000) == 0xB0000000
            || (pc_ & 0xF0000000) == 0xC0000000) {
            if (regs_[31] == 0) {
                return false;
            }
            pc_ = regs_[31];
            return true;
        }
        if (pc_ <= 0x00000100) {
            if (regs_[31] == 0) {
                return false;
            }
            pc_ = regs_[31];
            return true;
        }

        if (!in_ram(pc_) && pc_ != 0) {
            unsupported_ = "PC outside PS1 RAM";
            return false;
        }

        std::uint32_t instruction = read_ram32(pc_);
        const bool apply_load_delay = pending_load_valid_;
        const std::uint32_t load_delay_register = pending_load_register_;
        const std::uint32_t load_delay_value = pending_load_value_;
        pending_load_valid_ = false;
        if (std::getenv("ANYPS1_TRACE") != nullptr) {
            std::fprintf(stderr, "0x%08X: %08X\n",
                static_cast<unsigned>(pc_ & 0xFFFFFFFF),
                static_cast<unsigned>(instruction));
        }
        std::uint32_t next_pc = pc_ + 4;
        std::uint32_t scheduled_target = 0;
        bool schedule_delay = false;
        std::uint32_t opcode = instruction >> 26;
        std::uint32_t rs = (instruction >> 21) & 31;
        std::uint32_t rt = (instruction >> 16) & 31;
        std::uint32_t rd = (instruction >> 11) & 31;
        std::uint32_t shamt = (instruction >> 6) & 31;
        std::uint32_t immediate = instruction & 0xFFFF;

        if (opcode == 0) {
            switch (instruction & 63) {
            case 0x00:
                regs_[rd] = regs_[rt] << shamt;
                break;
            case 0x02:
                regs_[rd] = regs_[rt] >> shamt;
                break;
            case 0x03:
                regs_[rd] = static_cast<std::uint32_t>(static_cast<std::int32_t>(regs_[rt]) >> shamt);
                break;
            case 0x04:
                regs_[rd] = regs_[rt] << (regs_[rs] & 31);
                break;
            case 0x06:
                regs_[rd] = regs_[rt] >> (regs_[rs] & 31);
                break;
            case 0x07:
                regs_[rd] = static_cast<std::uint32_t>(
                    static_cast<std::int32_t>(regs_[rt]) >> (regs_[rs] & 31)
                );
                break;
            case 0x08:
                scheduled_target = regs_[rs];
                schedule_delay = true;
                break;
            case 0x09:
                regs_[rd] = next_pc;
                scheduled_target = regs_[rs];
                schedule_delay = true;
                break;
            case 0x10:
                regs_[rd] = hi_;
                break;
            case 0x11:
                hi_ = regs_[rs];
                break;
            case 0x12:
                regs_[rd] = lo_;
                break;
            case 0x13:
                lo_ = regs_[rs];
                break;
            case 0x0C:
                raise_exception(8);
                break;
            case 0x0D:
                raise_exception(9);
                break;
            case 0x18:
                mult(static_cast<std::int32_t>(regs_[rs]), static_cast<std::int32_t>(regs_[rt]));
                break;
            case 0x19:
                mult(static_cast<std::uint32_t>(regs_[rs]), static_cast<std::uint32_t>(regs_[rt]));
                break;
            case 0x1A:
                div(static_cast<std::int32_t>(regs_[rs]), static_cast<std::int32_t>(regs_[rt]));
                break;
            case 0x1B:
                divu(regs_[rs], regs_[rt]);
                break;
            case 0x21:
                regs_[rd] = regs_[rs] + regs_[rt];
                break;
            case 0x23:
                regs_[rd] = regs_[rs] - regs_[rt];
                break;
            case 0x24:
                regs_[rd] = regs_[rs] & regs_[rt];
                break;
            case 0x25:
                regs_[rd] = regs_[rs] | regs_[rt];
                break;
            case 0x26:
                regs_[rd] = regs_[rs] ^ regs_[rt];
                break;
            case 0x27:
                regs_[rd] = ~(regs_[rs] | regs_[rt]);
                break;
            case 0x2A:
                regs_[rd] = static_cast<std::int32_t>(regs_[rs]) < static_cast<std::int32_t>(regs_[rt]);
                break;
            case 0x2B:
                regs_[rd] = regs_[rs] < regs_[rt];
                break;
            default:
                unsupported_ = "unsupported MIPS special opcode";
                return false;
            }
        } else {
            switch (opcode) {
            case 0x01:
                if ((rt == 0 && static_cast<std::int32_t>(regs_[rs]) < 0)
                    || (rt == 1 && static_cast<std::int32_t>(regs_[rs]) >= 0)) {
                    scheduled_target = pc_ + 4 + (static_cast<std::int32_t>(sign16(immediate)) << 2);
                    schedule_delay = true;
                }
                break;
            case 0x02:
                scheduled_target = ((pc_ + 4) & 0xF0000000) | ((instruction & 0x03FFFFFF) << 2);
                schedule_delay = true;
                break;
            case 0x03:
                regs_[31] = pc_ + 8;
                scheduled_target = ((pc_ + 4) & 0xF0000000) | ((instruction & 0x03FFFFFF) << 2);
                schedule_delay = true;
                break;
            case 0x04:
                if (regs_[rs] == regs_[rt]) {
                    scheduled_target = pc_ + 4 + (static_cast<std::int32_t>(sign16(immediate)) << 2);
                    schedule_delay = true;
                }
                break;
            case 0x05:
                if (regs_[rs] != regs_[rt]) {
                    scheduled_target = pc_ + 4 + (static_cast<std::int32_t>(sign16(immediate)) << 2);
                    schedule_delay = true;
                }
                break;
            case 0x06:
                if (static_cast<std::int32_t>(regs_[rs]) <= 0) {
                    scheduled_target = pc_ + 4 + (static_cast<std::int32_t>(sign16(immediate)) << 2);
                    schedule_delay = true;
                }
                break;
            case 0x07:
                if (static_cast<std::int32_t>(regs_[rs]) > 0) {
                    scheduled_target = pc_ + 4 + (static_cast<std::int32_t>(sign16(immediate)) << 2);
                    schedule_delay = true;
                }
                break;
            case 0x08:
            case 0x09:
                regs_[rt] = regs_[rs] + sign16(immediate);
                break;
            case 0x0A:
                regs_[rt] = static_cast<std::int32_t>(regs_[rs]) < static_cast<std::int32_t>(sign16(immediate));
                break;
            case 0x0B:
                regs_[rt] = regs_[rs] < sign16(immediate);
                break;
            case 0x0C:
                regs_[rt] = regs_[rs] & immediate;
                break;
            case 0x0D:
                regs_[rt] = regs_[rs] | immediate;
                break;
            case 0x0E:
                regs_[rt] = regs_[rs] ^ immediate;
                break;
            case 0x0F:
                regs_[rt] = immediate << 16;
                break;
            case 0x20:
            case 0x24:
                if (!load_byte(regs_[rs] + sign16(immediate), rt, opcode == 0x24)) {
                    return false;
                }
                break;
            case 0x21:
            case 0x25:
                if (!load_half(regs_[rs] + sign16(immediate), rt, opcode == 0x25)) {
                    return false;
                }
                break;
            case 0x22:
                if (!load_left(regs_[rs] + sign16(immediate), rt)) {
                    return false;
                }
                break;
            case 0x23:
                if (!load_word(regs_[rs] + sign16(immediate), rt)) {
                    return false;
                }
                break;
            case 0x26:
                if (!load_right(regs_[rs] + sign16(immediate), rt)) {
                    return false;
                }
                break;
            case 0x28:
                if (!store_byte(regs_[rs] + sign16(immediate), regs_[rt])) {
                    return false;
                }
                break;
            case 0x29:
                if (!store_half(regs_[rs] + sign16(immediate), regs_[rt])) {
                    return false;
                }
                break;
            case 0x2A:
                if (!store_left(regs_[rs] + sign16(immediate), regs_[rt])) {
                    return false;
                }
                break;
            case 0x2B:
                if (!store_word(regs_[rs] + sign16(immediate), regs_[rt])) {
                    return false;
                }
                break;
            case 0x2E:
                if (!store_right(regs_[rs] + sign16(immediate), regs_[rt])) {
                    return false;
                }
                break;
            case 0x10:
            case 0x11:
                {
                    std::uint32_t cop0_rs = rs;
                    std::uint32_t cop0_rt = rt;
                    std::uint32_t cop0_rd = rd;
                    if (cop0_rs == 0) {
                        regs_[cop0_rt] = cop0_[cop0_rd];
                    } else if (cop0_rs == 4) {
                        cop0_[cop0_rd] = regs_[cop0_rt];
                    } else if (cop0_rs == 16) {
                        cop0_[12] = ((cop0_[12] & ~0x0Fu) | ((cop0_[12] >> 2) & 0x0Fu));
                    } else {
                        unsupported_ = "unsupported COP0 operation";
                        return false;
                    }
                }
                break;
            case 0x12:
                if (!execute_gte(rs, rt, rd, shamt, instruction)) {
                    return false;
                }
                break;
            case 0x13:
                unsupported_ = "COP3 instruction is not implemented";
                return false;
            default:
                unsupported_ = "unsupported MIPS opcode";
                return false;
            }
        }

        if (apply_load_delay
            && (!pending_load_valid_
                || pending_load_register_ != load_delay_register)) {
            regs_[load_delay_register] = load_delay_value;
        }
        if (exception_pending_) {
            exception_pending_ = false;
            in_delay_slot_ = false;
            regs_[0] = 0;
            return true;
        }
        regs_[0] = 0;
        bool had_delay = delay_pending_;
        std::uint32_t prior_target = delayed_target_;
        if (had_delay) {
            pc_ = prior_target;
            delay_pending_ = false;
        } else {
            pc_ = next_pc;
        }
        if (schedule_delay) {
            delayed_target_ = scheduled_target;
            delay_pending_ = true;
        }
        in_delay_slot_ = schedule_delay || had_delay;
        return true;
    }

    const char* unsupported() const
    {
        return unsupported_.empty() ? nullptr : unsupported_.c_str();
    }

    std::uint64_t pc() const
    {
        return pc_;
    }

    void tick_frame()
    {
        interrupt_status |= 1;
        for (std::uint32_t event = 0; event < bios_events.size(); ++event) {
            if (bios_events[event] & 2
                && bios_event_specs[event] == 1
                && bios_event_modes[event] == 0x2000) {
                bios_event_flags[event] = 0x4000;
            }
        }
        write_ram32(0x00100000, read_ram32(0x00100000) + 1);
        write_ram32(0x00100004, read_ram32(0x00100004) + 1);
        write_ram32(0x00100008, read_ram32(0x00100008));
        write_ram32(0x003D374, read_ram32(0x003D374) + 1);
        for (std::uint32_t timer = 0; timer < timer_count.size(); ++timer) {
            const std::uint32_t increment = (timer == 2 && (timer_mode[timer] & 0x200) != 0) ? 0 : 1;
            timer_count[timer] += increment;
            if (timer_target[timer] != 0 && timer_count[timer] >= timer_target[timer]) {
                if ((timer_mode[timer] & 8) != 0) {
                    timer_count[timer] = 0;
                }
                if ((timer_mode[timer] & 32) != 0) {
                    interrupt_status |= 1u << (10 + timer);
                }
            }
            if (timer_count[timer] >= 0x10000) {
                timer_count[timer] &= 0xFFFF;
            }
        }
        if (interrupt_status & interrupt_mask & 1) {
            cop0_[13] |= 0x0400;
        }
    }

    void set_pad_state(std::uint16_t state)
    {
        pad_state = state;
    }

    void render_audio(HostAudio& audio)
    {
        std::array<std::int16_t, 734> samples{};
        for (std::uint32_t index = 0; index < samples.size(); index += 2) {
            std::int32_t left = 0;
            std::int32_t right = 0;
            for (std::uint32_t voice = 0; voice < 24; ++voice) {
                if ((spu_key_on & (1u << voice)) != 0) {
                    const std::uint32_t address =
                        (spu_voice_start[voice] * 8u + voice * 16u) & (spu_ram.size() - 1);
                    const std::int16_t sample =
                        static_cast<std::int16_t>(spu_ram[address] | (spu_ram[address + 1] << 8));
                    left += sample * (spu_voice_volume_l[voice] & 0x3FFF) / 0x4000;
                    right += sample * (spu_voice_volume_r[voice] & 0x3FFF) / 0x4000;
                }
            }
            left = std::clamp(left, -32768, 32767);
            right = std::clamp(right, -32768, 32767);
            samples[index] = static_cast<std::int16_t>(left);
            samples[index + 1] = static_cast<std::int16_t>(right);
        }
        audio.put_samples(samples.data(), samples.size());
    }

    const std::array<std::uint16_t, 1024 * 512>& frame_vram() const
    {
        return vram_;
    }

    std::uint32_t frame_x() const { return display_x; }
    std::uint32_t frame_y() const { return display_y; }
    std::uint32_t frame_width() const { return display_width; }
    std::uint32_t frame_height() const { return display_height; }

private:
    std::array<std::uint32_t, 32> regs_{};
    std::array<std::uint32_t, 32> cop0_{};
    std::array<std::int32_t, 32> gte_data_{};
    std::array<std::uint32_t, 32> gte_control{};
    std::array<unsigned char, 2 * 1024 * 1024> ram_{};
    std::array<unsigned char, 1024> scratchpad_{};
    std::array<std::uint16_t, 1024 * 512> vram_{};
    std::array<std::uint32_t, 16> gp0_params_{};
    std::uint32_t gp0_param_count = 0;
    std::uint32_t gp0_expected = 0;
    std::uint32_t gp0_command = 0;
    std::uint32_t gp0_transfer_remaining = 0;
    std::uint32_t gp0_transfer_x = 0;
    std::uint32_t gp0_transfer_y = 0;
    std::uint32_t gp0_transfer_start_x = 0;
    std::uint32_t gp0_transfer_start_y = 0;
    std::uint32_t gp0_transfer_width = 0;
    std::uint32_t gp0_transfer_height = 0;
    std::uint32_t gp0_transfer_cursor = 0;
    std::uint32_t display_x = 0;
    std::uint32_t display_y = 0;
    std::uint32_t display_width = 320;
    std::uint32_t display_height = 240;
    std::uint32_t gpu_status = 0x1C000000;
    std::uint64_t gpu_write_count = 0;
    std::uint64_t gpu_vram_write_count = 0;
    std::uint32_t spu_dma_word = 0;
    std::array<std::uint32_t, 7> dma_madr{};
    std::array<std::uint32_t, 7> dma_bcr{};
    std::array<std::uint32_t, 7> dma_chcr{};
    std::uint32_t dpcr = 0x07654321;
    std::uint32_t dicr = 0;
    std::uint32_t interrupt_status = 0;
    std::uint32_t interrupt_mask = 0;
    std::array<std::uint32_t, 3> timer_count{};
    std::array<std::uint32_t, 3> timer_target{};
    std::array<std::uint32_t, 3> timer_mode{};
    std::uint32_t joy_stat = 0x00000005;
    std::uint32_t joy_ctrl = 0;
    std::uint32_t joy_mode = 0;
    std::uint32_t joy_baud = 0;
    std::uint16_t pad_state = 0xFFFF;
    std::array<unsigned char, 8> joy_rx{};
    std::uint32_t joy_rx_count = 0;
    std::uint32_t joy_rx_cursor = 0;
    std::array<std::uint32_t, 16> bios_events{};
    std::array<std::uint32_t, 16> bios_event_specs{};
    std::array<std::uint32_t, 16> bios_event_modes{};
    std::array<std::uint32_t, 16> bios_event_flags{};
    std::uint32_t cdrom_index = 0;
    std::uint32_t cdrom_status = 2;
    std::uint32_t cdrom_interrupt = 0;
    std::uint32_t cdrom_interrupt_enable = 0;
    std::array<unsigned char, 16> cdrom_params{};
    std::uint32_t cdrom_param_count = 0;
    std::array<unsigned char, 32> cdrom_response{};
    std::uint32_t cdrom_response_count = 0;
    std::uint32_t cdrom_response_cursor = 0;
    std::array<unsigned char, 512 * 1024> spu_ram{};
    std::array<std::uint32_t, 24> spu_voice_start{};
    std::array<std::uint32_t, 24> spu_voice_repeat{};
    std::array<std::uint16_t, 24> spu_voice_volume_l{};
    std::array<std::uint16_t, 24> spu_voice_volume_r{};
    std::array<std::uint16_t, 24> spu_voice_adsr{};
    std::uint32_t spu_control = 0;
    std::uint32_t spu_status = 0;
    std::uint32_t spu_main_volume_l = 0;
    std::uint32_t spu_main_volume_r = 0;
    std::uint32_t spu_key_on = 0;
    std::uint32_t spu_key_off = 0;
    std::uint32_t spu_dma_address = 0;
    std::uint32_t pc_ = 0;
    std::uint32_t load_address_ = 0;
    std::uint32_t file_size_ = 0;
    std::uint32_t sp_base_ = 0;
    std::uint32_t sp_offset_ = 0;
    std::uint32_t hi_ = 0;
    std::uint32_t lo_ = 0;
    std::uint32_t delayed_target_ = 0;
    bool delay_pending_ = false;
    bool in_delay_slot_ = false;
    bool exception_pending_ = false;
    bool pending_load_valid_ = false;
    std::uint32_t pending_load_register_ = 0;
    std::uint32_t pending_load_value_ = 0;
    std::uint32_t heap_cursor_ = 0x00180000;
    std::string unsupported_;

#include "RuntimeGpu.inc"
#include "RuntimeMemory.inc"
#include "RuntimeMmio.inc"
#include "RuntimeArith.inc"
};

}

bool run_psx(const RunOptions& options)
{
    std::ifstream input(options.input, std::ios::binary);
    if (!input) {
        throw std::runtime_error("cannot open input PS-X EXE: " + options.input);
    }
    std::vector<unsigned char> exe{
        std::istreambuf_iterator<char>(input),
        std::istreambuf_iterator<char>()
    };
    PsxCpu cpu(exe);
    std::unique_ptr<HostVideo> video;
    std::unique_ptr<HostAudio> audio = std::make_unique<HostAudio>();
    if (options.video) {
        video = std::make_unique<HostVideo>();
    }
    for (std::uint64_t instructions = 0; instructions < options.max_instructions; ++instructions) {
        if (video && (instructions % 10000 == 0)) {
            if (!video->poll_events()) {
                return true;
            }
            cpu.set_pad_state(video->pad_state());
            cpu.tick_frame();
            video->present_vram(
                cpu.frame_vram(),
                cpu.frame_x(),
                cpu.frame_y(),
                cpu.frame_width(),
                cpu.frame_height()
            );
            cpu.render_audio(*audio);
        } else if (!video && instructions % 10000 == 0) {
            cpu.tick_frame();
            cpu.render_audio(*audio);
        }
        if (!cpu.step()) {
            if (const char* reason = cpu.unsupported()) {
                throw std::runtime_error(
                    std::string(reason) + " at 0x"
                    + [] (std::uint64_t value) {
                        char output[17];
                        std::snprintf(output, sizeof(output), "%08X", static_cast<unsigned>(value));
                        return std::string(output);
                    }(cpu.pc())
                );
            }
            return true;
        }
    }
    std::cout << "instruction limit reached\n";
    return true;
}

}
