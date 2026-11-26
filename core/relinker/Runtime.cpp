#include "Relinker.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <fstream>
#include <iostream>
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
    }

    bool step()
    {
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
                if (regs_[31] != 0) {
                    next_pc = regs_[31];
                }
                break;
            case 0x0D:
                unsupported_ = "MIPS break instruction";
                return false;
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
            case 0x23:
                if (!load_word(regs_[rs] + sign16(immediate), rt)) {
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
            case 0x2B:
                if (!store_word(regs_[rs] + sign16(immediate), regs_[rt])) {
                    return false;
                }
                break;
            case 0x10:
            case 0x11:
                break;
            case 0x12:
                break;
            case 0x13:
                break;
            default:
                unsupported_ = "unsupported MIPS opcode";
                return false;
            }
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

private:
    std::array<std::uint32_t, 32> regs_{};
    std::array<unsigned char, 2 * 1024 * 1024> ram_{};
    std::array<unsigned char, 1024> scratchpad_{};
    std::uint32_t pc_ = 0;
    std::uint32_t load_address_ = 0;
    std::uint32_t file_size_ = 0;
    std::uint32_t sp_base_ = 0;
    std::uint32_t sp_offset_ = 0;
    std::uint32_t hi_ = 0;
    std::uint32_t lo_ = 0;
    std::uint32_t delayed_target_ = 0;
    bool delay_pending_ = false;
    std::string unsupported_;

    bool in_ram(std::uint32_t address) const
    {
        address &= 0x1FFFFFFF;
        return address < ram_.size();
    }

    std::uint32_t read_ram32(std::uint32_t address) const
    {
        address &= 0x1FFFFFFF;
        return static_cast<std::uint32_t>(ram_[address])
            | (static_cast<std::uint32_t>(ram_[address + 1]) << 8)
            | (static_cast<std::uint32_t>(ram_[address + 2]) << 16)
            | (static_cast<std::uint32_t>(ram_[address + 3]) << 24);
    }

    bool load_byte(std::uint32_t address, std::uint32_t reg, bool unsigned_value)
    {
        if (in_ram(address)) {
            std::uint32_t value = ram_[address & 0x1FFFFFFF];
            regs_[reg] = unsigned_value ? value : (value & 0x80 ? 0xFFFFFF00 | value : value);
            return true;
        }
        std::uint32_t value = 0;
        if (io_load(address, 1, value)) {
            regs_[reg] = unsigned_value ? value : (value & 0x80 ? 0xFFFFFF00 | value : value);
            return true;
        }
        unsupported_ = "read from unsupported PS1 memory";
        return false;
    }

    bool load_half(std::uint32_t address, std::uint32_t reg, bool unsigned_value)
    {
        if (address & 1) {
            unsupported_ = "unaligned 16-bit load";
            return false;
        }
        if (in_ram(address) && in_ram(address + 1)) {
            std::uint32_t value = ram_[address & 0x1FFFFFFF]
                | (static_cast<std::uint32_t>(ram_[(address & 0x1FFFFFFF) + 1]) << 8);
            regs_[reg] = unsigned_value ? value : (value & 0x8000 ? 0xFFFF0000 | value : value);
            return true;
        }
        std::uint32_t value = 0;
        if (io_load(address, 2, value)) {
            regs_[reg] = unsigned_value ? value : (value & 0x8000 ? 0xFFFF0000 | value : value);
            return true;
        }
        unsupported_ = "read from unsupported PS1 memory";
        return false;
    }

    bool load_word(std::uint32_t address, std::uint32_t reg)
    {
        if (address & 3) {
            unsupported_ = "unaligned 32-bit load";
            return false;
        }
        if (in_ram(address) && in_ram(address + 3)) {
            regs_[reg] = read_ram32(address);
            return true;
        }
        std::uint32_t value = 0;
        if (io_load(address, 4, value)) {
            regs_[reg] = value;
            return true;
        }
        unsupported_ = "read from unsupported PS1 memory";
        return false;
    }

    bool store_byte(std::uint32_t address, std::uint32_t value)
    {
        if (in_ram(address)) {
            ram_[address & 0x1FFFFFFF] = static_cast<unsigned char>(value);
            return true;
        }
        return io_store(address, value, 1);
    }

    bool store_half(std::uint32_t address, std::uint32_t value)
    {
        if (address & 1) {
            unsupported_ = "unaligned 16-bit store";
            return false;
        }
        if (in_ram(address) && in_ram(address + 1)) {
            ram_[address & 0x1FFFFFFF] = static_cast<unsigned char>(value);
            ram_[(address & 0x1FFFFFFF) + 1] = static_cast<unsigned char>(value >> 8);
            return true;
        }
        return io_store(address, value, 2);
    }

    bool store_word(std::uint32_t address, std::uint32_t value)
    {
        if (address & 3) {
            unsupported_ = "unaligned 32-bit store";
            return false;
        }
        if (in_ram(address) && in_ram(address + 3)) {
            address &= 0x1FFFFFFF;
            ram_[address] = static_cast<unsigned char>(value);
            ram_[address + 1] = static_cast<unsigned char>(value >> 8);
            ram_[address + 2] = static_cast<unsigned char>(value >> 16);
            ram_[address + 3] = static_cast<unsigned char>(value >> 24);
            return true;
        }
        return io_store(address, value, 4);
    }

    bool io_store(std::uint32_t address, std::uint32_t value, std::uint32_t size)
    {
        std::uint32_t physical = address & 0x1FFFFFFF;
        if (physical >= 0x1F800000 && physical < 0x1F800400) {
            std::uint32_t offset = physical - 0x1F800000;
            for (std::uint32_t index = 0; index < size && offset + index < scratchpad_.size(); ++index) {
                scratchpad_[offset + index] = static_cast<unsigned char>(value >> (index * 8));
            }
            return true;
        }
        if (physical >= 0x1F801000 && physical < 0x1F803000) {
            return true;
        }
        unsupported_ = "store to unsupported PS1 memory";
        return false;
    }

    bool io_load(std::uint32_t address, std::uint32_t size, std::uint32_t& value)
    {
        std::uint32_t physical = address & 0x1FFFFFFF;
        if (physical >= 0x1F800000 && physical < 0x1F800400) {
            std::uint32_t offset = physical - 0x1F800000;
            value = 0;
            for (std::uint32_t index = 0; index < size && offset + index < scratchpad_.size(); ++index) {
                value |= static_cast<std::uint32_t>(scratchpad_[offset + index]) << (index * 8);
            }
            return true;
        }
        if (physical >= 0x1F801000 && physical < 0x1F803000) {
            value = 0;
            return true;
        }
        return false;
    }

    void mult(std::int32_t left, std::int32_t right)
    {
        std::int64_t product = static_cast<std::int64_t>(left) * right;
        lo_ = static_cast<std::uint32_t>(product);
        hi_ = static_cast<std::uint32_t>(static_cast<std::uint64_t>(product) >> 32);
    }

    void mult(std::uint32_t left, std::uint32_t right)
    {
        std::uint64_t product = static_cast<std::uint64_t>(left) * right;
        lo_ = static_cast<std::uint32_t>(product);
        hi_ = static_cast<std::uint32_t>(product >> 32);
    }

    void div(std::int32_t left, std::int32_t right)
    {
        if (right == 0) {
            lo_ = left >= 0 ? 0xFFFFFFFF : 1;
            hi_ = left;
        } else if (left == -2147483648 && right == -1) {
            lo_ = 0x80000000;
            hi_ = 0;
        } else {
            lo_ = static_cast<std::uint32_t>(left / right);
            hi_ = static_cast<std::uint32_t>(left % right);
        }
    }

    void divu(std::uint32_t left, std::uint32_t right)
    {
        if (right == 0) {
            lo_ = 0xFFFFFFFF;
            hi_ = left;
        } else {
            lo_ = left / right;
            hi_ = left % right;
        }
    }
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
    for (std::uint64_t instructions = 0; instructions < options.max_instructions; ++instructions) {
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
