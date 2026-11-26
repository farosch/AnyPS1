#include "runtime/Runtime.h"
#include "host/HostVideo.h"
#include "host/HostAudio.h"

#include <array>
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <fstream>
#include <filesystem>
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
    explicit PsxCpu(
        const std::vector<unsigned char>& exe,
        const std::string& cdrom_bin = {},
        const std::string& memory_card_path = {}
    )
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
        if (!cdrom_bin.empty()) {
            std::ifstream disc(cdrom_bin, std::ios::binary);
            cdrom_data.assign(
                std::istreambuf_iterator<char>(disc),
                std::istreambuf_iterator<char>()
            );
            if (!disc && cdrom_data.empty()) {
                throw std::runtime_error("cannot open CD-ROM BIN: " + cdrom_bin);
            }
        }
        if (!memory_card_path.empty()) {
            std::ifstream card(memory_card_path, std::ios::binary);
            std::vector<unsigned char> data{
                std::istreambuf_iterator<char>(card),
                std::istreambuf_iterator<char>()
            };
            if (!data.empty()) {
                if (data.size() != memory_card.size()) {
                    throw std::runtime_error("invalid memory card size: " + memory_card_path);
                }
                std::memcpy(memory_card.data(), data.data(), data.size());
            }
        }
        std::uint32_t initial_sp = sp_base_ + sp_offset_;
        initial_sp = 0x80000000u + (ram_.size() - 0x1000u);
        regs_[29] = initial_sp;
        ram_[0x474] = 0x00;
        ram_[0x475] = 0xF0;
        ram_[0x476] = 0x1F;
        ram_[0x477] = 0x00;
        ram_[0x1FF000] = 0x01;
        ram_[0x1FF001] = 0x80;
        ram_[0x1FF004] = 0x08;
        ram_[0x1FF005] = 0x20;
    }

    bool step()
    {
        if (pc_ >= 0x80000080 && pc_ < 0x80000100 && cop0_[14] != 0) {
            pc_ = cop0_[14] + (in_delay_slot_ ? 0 : 4);
            cop0_[13] &= ~0x0400u;
            return true;
        }
        if (execute_bios_vector()) {
            return true;
        }
        if ((cop0_[12] & 1) != 0
            && (cop0_[12] & cop0_[13] & 0xFF00) != 0) {
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
            if (cop0_[14] != 0) {
                pc_ = cop0_[14] + (in_delay_slot_ ? 0 : 4);
                return true;
            }
            if (regs_[31] == 0) {
                return false;
            }
            pc_ = regs_[31];
            return true;
        }

        if (!in_ram(pc_) && pc_ != 0) {
            raise_exception(4);
            return true;
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
                if (regs_[4] == 1) {
                    cop0_[12] &= ~1u;
                } else if (regs_[4] == 2) {
                    cop0_[12] |= 1u;
                    ++kernel_frame_counter;
                    ++timer_count[1];
                    if (timer_count[1] >= 0x10000) {
                        timer_count[1] = 0;
                    }
                } else {
                    raise_exception(8);
                }
                break;
            case 0x0D:
                if (instruction == 0x0000004D) {
                    break;
                }
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
            case 0x20:
                {
                    const std::int64_t sum =
                        static_cast<std::int64_t>(static_cast<std::int32_t>(regs_[rs]))
                        + static_cast<std::int64_t>(static_cast<std::int32_t>(regs_[rt]));
                    if (sum < -2147483648LL || sum > 2147483647LL) {
                        raise_exception(12);
                    } else {
                        regs_[rd] = static_cast<std::uint32_t>(static_cast<std::int32_t>(sum));
                    }
                }
                break;
            case 0x23:
                regs_[rd] = regs_[rs] - regs_[rt];
                break;
            case 0x22:
                {
                    const std::int64_t difference =
                        static_cast<std::int64_t>(static_cast<std::int32_t>(regs_[rs]))
                        - static_cast<std::int64_t>(static_cast<std::int32_t>(regs_[rt]));
                    if (difference < -2147483648LL || difference > 2147483647LL) {
                        raise_exception(12);
                    } else {
                        regs_[rd] = static_cast<std::uint32_t>(static_cast<std::int32_t>(difference));
                    }
                }
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
                if (opcode == 0x08) {
                    const std::int64_t sum =
                        static_cast<std::int64_t>(static_cast<std::int32_t>(regs_[rs]))
                        + static_cast<std::int64_t>(static_cast<std::int32_t>(sign16(immediate)));
                    if (sum < -2147483648LL || sum > 2147483647LL) {
                        raise_exception(12);
                    } else {
                        regs_[rt] = static_cast<std::uint32_t>(static_cast<std::int32_t>(sum));
                    }
                } else {
                    regs_[rt] = regs_[rs] + sign16(immediate);
                }
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
                        if (cop0_rd == 12) {
                            cop0_[12] = regs_[cop0_rt] & 0x003FFFFFu;
                        } else if (cop0_rd == 13) {
                            cop0_[13] = (cop0_[13] & ~0x300u)
                                | (regs_[cop0_rt] & 0x300u);
                        } else {
                            cop0_[cop0_rd] = regs_[cop0_rt];
                        }
                    } else if (cop0_rs == 16) {
                        cop0_[12] = ((cop0_[12] & ~0x0Fu) | ((cop0_[12] >> 2) & 0x0Fu));
                        in_delay_slot_ = false;
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
            bool cancelled = false;
            if (opcode == 0) {
                const std::uint32_t special = instruction & 63;
                if (special != 0x08 && special != 0x09
                    && special != 0x18 && special != 0x19
                    && special != 0x1A && special != 0x1B
                    && special != 0x0C && special != 0x0D
                    && rd == load_delay_register) {
                    cancelled = true;
                }
            } else if (opcode >= 0x08 && opcode <= 0x0F
                && rt == load_delay_register) {
                cancelled = true;
            } else if (opcode >= 0x20 && opcode <= 0x27
                && opcode != 0x22 && opcode != 0x23
                && opcode != 0x26 && opcode != 0x27
                && rt == load_delay_register) {
                cancelled = opcode == 0x20 || opcode == 0x24 || opcode == 0x25;
            }
            if (!cancelled) {
                regs_[load_delay_register] = load_delay_value;
            }
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

    bool save_memory_card(const std::string& path) const
    {
        std::ofstream card(path, std::ios::binary | std::ios::trunc);
        if (!card) {
            return false;
        }
        card.write(reinterpret_cast<const char*>(memory_card.data()), memory_card.size());
        return static_cast<bool>(card);
    }

    std::uint32_t joy_motor_small() const { return joy_motor_small_; }
    std::uint32_t joy_motor_large() const { return joy_motor_large_; }

    RuntimeSnapshot snapshot() const
    {
        RuntimeSnapshot result;
        result.pc = pc_;
        result.registers = regs_;
        result.cop0 = cop0_;
        result.gte_data = gte_data_;
        result.gte_control = gte_control;
        result.gte_cycle_count = gte_cycle_count_;
        result.gpu_status = gpu_status;
        result.gpu_write_count = static_cast<std::uint32_t>(gpu_write_count);
        result.gpu_vram_write_count = static_cast<std::uint32_t>(gpu_vram_write_count);
        result.gpuread = gpuread;
        result.gpu_draw_x1 = gpu_draw_x1;
        result.gpu_draw_y1 = gpu_draw_y1;
        result.gpu_draw_x2 = gpu_draw_x2;
        result.gpu_draw_y2 = gpu_draw_y2;
        result.gpu_tex_window = gpu_tex_window_x
            | (gpu_tex_window_y << 5)
            | (gpu_tex_window_mask_x << 10)
            | (gpu_tex_window_mask_y << 15)
            | (gpu_tex_window_offset_x << 20)
            | (gpu_tex_window_offset_y << 25);
        result.display_x = display_x;
        result.display_y = display_y;
        result.display_width = display_width;
        result.display_height = display_height;
        result.dma_madr = dma_madr;
        result.dma_bcr = dma_bcr;
        result.dma_chcr = dma_chcr;
        if (ram_.size() >= 0x104) {
            result.dma_otc_first = read_ram32(0x80000100);
            result.dma_otc_last = read_ram32(0x800000FC);
        }
        result.interrupt_status = interrupt_status;
        result.interrupt_mask = interrupt_mask;
        result.timer_count = timer_count;
        result.timer_target = timer_target;
        result.timer_mode = timer_mode;
        result.cdrom_status = cdrom_status;
        result.cdrom_lba = cdrom_lba;
        result.cdrom_sector_size = cdrom_sector_size;
        result.cdrom_response_count = cdrom_response_count;
        result.cdrom_params = cdrom_params;
        result.cdrom_read_active = cdrom_read_active;
        result.spu_control = spu_control;
        result.spu_status = spu_status;
        result.spu_key_on = spu_key_on;
        result.spu_voice_volume_l = spu_voice_volume_l;
        result.spu_voice_volume_r = spu_voice_volume_r;
        for (std::uint32_t voice = 0; voice < 24; ++voice) {
            result.spu_voice_address[voice] = spu_voice_start[voice] * 8u;
        }
        result.joy_stat = joy_stat;
        result.joy_ctrl = joy_ctrl;
        result.joy_mode = joy_mode;
        result.joy_baud = joy_baud;
        result.joy_rx_count = joy_rx_count;
        result.joy_rx = joy_rx;
        result.joy_analog_mode = joy_analog_mode;
        result.joy_config_mode = joy_config_mode;
        result.joy_motor_small = joy_motor_small_;
        result.joy_motor_large = joy_motor_large_;
        result.memory_card = memory_card;
        result.memory_card_write_count = memory_card_write_count;
        return result;
    }

    void tick_frame()
    {
        assert_interrupt(0);
        ++kernel_frame_counter;
        hblank_active = !hblank_active;
        timer_gate_active[0] = hblank_active;
        timer_gate_active[1] = hblank_active;
        for (std::uint32_t voice = 0; voice < 24; ++voice) {
            if (spu_key_on & (1u << voice)) {
                ++spu_voice_counter[voice];
                if (spu_voice_phase[voice] == 1 && spu_voice_counter[voice] >= 4) {
                    spu_voice_phase[voice] = 2;
                    spu_voice_adsr[voice] = 0xF000;
                } else if (spu_voice_phase[voice] == 2 && spu_voice_counter[voice] >= 8) {
                    spu_voice_phase[voice] = 3;
                    spu_voice_adsr[voice] = 0x8000;
                }
            } else if (spu_voice_phase[voice] == 4) {
                spu_voice_adsr[voice] = spu_voice_adsr[voice] > 0x400
                    ? spu_voice_adsr[voice] - 0x400 : 0;
                if (spu_voice_adsr[voice] == 0) {
                    spu_voice_phase[voice] = 0;
                }
            }
        }
        for (std::uint32_t event = 0; event < bios_events.size(); ++event) {
            if (bios_events[event] & 2
                && bios_event_classes[event] == 0xF0000001
                && bios_event_specs[event] == 1
                && bios_event_modes[event] == 0x2000) {
                bios_event_flags[event] = 0x4000;
            }
        }
        tick_timers();
    }

    void set_pad_state(std::uint16_t state)
    {
        pad_state = state;
        controller_state.digital = state;
    }

    void set_controller_state(const ControllerState& state)
    {
        pad_state = state.digital;
        controller_state = state;
    }

    void render_audio(HostAudio& audio)
    {
        std::array<std::int16_t, 734> samples{};
        for (std::uint32_t index = 0; index < samples.size(); index += 2) {
            std::int32_t left = 0;
            std::int32_t right = 0;
            for (std::uint32_t voice = 0; voice < 24; ++voice) {
                if ((spu_key_on & (1u << voice)) != 0) {
                    const std::uint32_t block_address = spu_voice_address[voice] & (spu_ram.size() - 1);
                    const std::uint32_t block_header = spu_ram[block_address];
                    const std::uint32_t block_flags = spu_ram[block_address + 1];
                    const std::uint32_t shift = std::min(block_header & 15, 12u);
                    const std::uint32_t filter = (block_header >> 4) & 3;
                    const std::uint32_t sample_index = spu_voice_block_sample[voice];
                    const std::uint32_t nibble_index = sample_index & 1;
                    const std::uint32_t nibble =
                        (spu_ram[block_address + 2 + (sample_index >> 1)]
                            >> (nibble_index ? 4 : 0)) & 15;
                    std::int32_t raw = nibble & 8
                        ? static_cast<std::int32_t>(nibble | 0xFFFFFFF0)
                        : static_cast<std::int32_t>(nibble);
                    std::int64_t base = (static_cast<std::int64_t>(raw) << std::min(shift, 12u));
                    static const std::int32_t f1[4]{0, 60, 115, 98};
                    static const std::int32_t f2[4]{0, 0, -52, -55};
                    base += (spu_voice_previous1[voice] * f1[filter] + spu_voice_previous2[voice] * f2[filter]) / 64;
                    base = std::clamp<std::int64_t>(base, -32768, 32767);
                    spu_voice_previous2[voice] = spu_voice_previous1[voice];
                    spu_voice_previous1[voice] = static_cast<std::int32_t>(base);
                    const std::int16_t sample = static_cast<std::int16_t>(base);
                    const std::uint32_t envelope = (spu_voice_adsr[voice] >> 8) + 1;
                    left += sample * (spu_voice_volume_l[voice] & 0x3FFF) * envelope / 0x4000 / 256;
                    right += sample * (spu_voice_volume_r[voice] & 0x3FFF) * envelope / 0x4000 / 256;
                    ++spu_voice_block_sample[voice];
                    if (spu_voice_block_sample[voice] == 28) {
                        spu_voice_block_sample[voice] = 0;
                        spu_voice_address[voice] += 16;
                        if (block_flags & 1) {
                            spu_voice_address[voice] =
                                spu_voice_repeat_address[voice] ? spu_voice_repeat_address[voice] : block_address;
                        }
                    }
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
    std::uint64_t gte_cycle_count_ = 0;
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
    std::uint32_t gpu_status = 0x1F400000;
    std::uint64_t gpu_write_count = 0;
    std::uint64_t gpu_vram_write_count = 0;
    std::uint32_t gpuread = 0;
    std::array<std::uint32_t, 4> gpu_read_fifo{};
    std::uint32_t gpu_read_count = 0;
    std::uint32_t gpu_read_cursor = 0;
    std::uint32_t gpu_read_remaining = 0;
    std::uint32_t gpu_read_x = 0;
    std::uint32_t gpu_read_y = 0;
    std::uint32_t gpu_read_width = 0;
    std::uint32_t gpu_read_height = 0;
    std::uint32_t gpu_draw_x1 = 0;
    std::uint32_t gpu_draw_y1 = 0;
    std::uint32_t gpu_draw_x2 = 1024;
    std::uint32_t gpu_draw_y2 = 512;
    std::uint32_t gpu_tex_window_x = 0;
    std::uint32_t gpu_tex_window_y = 0;
    std::uint32_t gpu_tex_window_mask_x = 0;
    std::uint32_t gpu_tex_window_mask_y = 0;
    std::uint32_t gpu_tex_window_offset_x = 0;
    std::uint32_t gpu_tex_window_offset_y = 0;
    std::uint32_t gpu_draw_offset_x = 0;
    std::uint32_t gpu_draw_offset_y = 0;
    std::uint32_t gpu_mask_set = 0;
    std::uint32_t gpu_mask_keep = 0;
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
    std::array<bool, 3> timer_gate_active{};
    std::array<bool, 3> timer_irq_active{};
    bool hblank_active = false;
    std::uint32_t kernel_frame_counter = 0;
    std::uint32_t joy_stat = 0x00000005;
    std::uint32_t joy_ctrl = 0;
    std::uint32_t joy_mode = 0;
    std::uint32_t joy_baud = 0;
    std::uint16_t pad_state = 0xFFFF;
    ControllerState controller_state;
    std::array<unsigned char, 8> joy_rx{};
    std::uint32_t joy_rx_count = 0;
    std::uint32_t joy_rx_cursor = 0;
    bool joy_analog_mode = false;
    bool joy_config_mode = false;
    std::uint32_t joy_motor_small_ = 0;
    std::uint32_t joy_motor_large_ = 0;
    std::uint32_t joy_motor_phase = 0;
    std::array<unsigned char, 128 * 1024> memory_card{};
    std::uint32_t memory_card_address = 0;
    std::uint32_t memory_card_phase = 0;
    std::uint32_t memory_card_write_count = 0;
    bool memory_card_writing = false;
    std::array<std::uint32_t, 16> bios_events{};
    std::array<std::uint32_t, 16> bios_event_classes{};
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
    std::vector<unsigned char> cdrom_data;
    std::uint32_t cdrom_lba = 0;
    std::uint32_t cdrom_sector_size = 2352;
    bool cdrom_read_active = false;
    std::array<unsigned char, 512 * 1024> spu_ram{};
    std::array<std::uint32_t, 24> spu_voice_start{};
    std::array<std::uint32_t, 24> spu_voice_repeat{};
    std::array<std::uint16_t, 24> spu_voice_volume_l{};
    std::array<std::uint16_t, 24> spu_voice_volume_r{};
    std::array<std::uint16_t, 24> spu_voice_adsr{};
    std::array<std::uint32_t, 24> spu_voice_address{};
    std::array<std::uint32_t, 24> spu_voice_repeat_address{};
    std::array<std::uint32_t, 24> spu_voice_phase{};
    std::array<std::uint32_t, 24> spu_voice_counter{};
    std::array<std::int32_t, 24> spu_voice_previous1{};
    std::array<std::int32_t, 24> spu_voice_previous2{};
    std::array<std::uint32_t, 24> spu_voice_block_sample{};
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

bool run_psx(const RunOptions& options, RuntimeSnapshot* snapshot)
{
    std::ifstream input(options.input, std::ios::binary);
    if (!input) {
        throw std::runtime_error("cannot open input PS-X EXE: " + options.input);
    }
    std::vector<unsigned char> exe{
        std::istreambuf_iterator<char>(input),
        std::istreambuf_iterator<char>()
    };
    std::string cdrom_bin = options.cdrom_bin;
    if (cdrom_bin.empty()) {
        std::filesystem::path input_path(options.input);
        for (std::filesystem::path directory = input_path.parent_path();
             !directory.empty();
             directory = directory.parent_path()) {
            std::vector<std::filesystem::path> candidates;
            for (const std::filesystem::directory_entry& entry :
                 std::filesystem::directory_iterator(directory)) {
                if (entry.is_regular_file() && entry.path().extension() == ".bin") {
                    candidates.push_back(entry.path());
                }
            }
            std::sort(candidates.begin(), candidates.end());
            for (const std::filesystem::path& candidate : candidates) {
                const std::string filename = candidate.filename().string();
                if (filename.find("Track 1") != std::string::npos
                    || filename.find("Track1") != std::string::npos) {
                    cdrom_bin = candidate.string();
                    break;
                }
            }
            if (cdrom_bin.empty() && !candidates.empty()) {
                cdrom_bin = candidates.front().string();
            }
            if (!cdrom_bin.empty()) {
                break;
            }
            if (directory == directory.parent_path()) {
                break;
            }
        }
    }
    PsxCpu cpu(exe, cdrom_bin, options.memory_card);
    auto save_memory_card = [&]() {
        if (!options.memory_card.empty() && !cpu.save_memory_card(options.memory_card)) {
            throw std::runtime_error("cannot write memory card: " + options.memory_card);
        }
    };
    std::unique_ptr<HostVideo> video;
    std::unique_ptr<HostAudio> audio = std::make_unique<HostAudio>();
    if (options.video) {
        video = std::make_unique<HostVideo>();
    }
    for (std::uint64_t instructions = 0; instructions < options.max_instructions; ++instructions) {
        if (video && (instructions % 10000 == 0)) {
            if (!video->poll_events()) {
                save_memory_card();
                return true;
            }
            cpu.set_controller_state(video->controller_state());
            video->apply_controller_rumble(
                cpu.joy_motor_small(),
                cpu.joy_motor_large()
            );
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
            if (snapshot != nullptr) {
                *snapshot = cpu.snapshot();
            }
            if (const char* reason = cpu.unsupported()) {
                save_memory_card();
                throw std::runtime_error(
                    std::string(reason) + " at 0x"
                    + [] (std::uint64_t value) {
                        char output[17];
                        std::snprintf(output, sizeof(output), "%08X", static_cast<unsigned>(value));
                        return std::string(output);
                    }(cpu.pc())
                );
            }
            save_memory_card();
            return true;
        }
    }
    if (snapshot != nullptr) {
        *snapshot = cpu.snapshot();
    }
    save_memory_card();
    std::cout << "instruction limit reached\n";
    return true;
}

}
