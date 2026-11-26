    bool execute_bios_vector()
    {
        std::uint32_t table_pc = pc_ & 0x1FFFFFFF;
        bool cached_a0 = (pc_ & 0xF0000000) == 0xA0000000;
        bool cached_b0 = (pc_ & 0xF0000000) == 0xB0000000;
        bool cached_c0 = (pc_ & 0xF0000000) == 0xC0000000;

        if (cached_a0 && table_pc == 0x0000E800) {
            std::uint32_t destination = regs_[4] & 0x1FFFFFFF;
            std::uint32_t size = regs_[5];
            if (destination + size <= ram_.size()) {
                std::memset(ram_.data() + destination, 0, size);
            }
            pc_ = regs_[31];
            return true;
        }

        if (table_pc == 0xB0 || cached_b0) {
            std::uint32_t function = regs_[9];
            if (function == 0) {
                std::uint32_t size = (regs_[4] + 3) & ~3u;
                if (size > 0x001F0000 - heap_cursor_) {
                    regs_[2] = 0xFFFFFFFF;
                } else {
                    regs_[2] = 0x80000000 | heap_cursor_;
                    heap_cursor_ += size;
                }
            } else if (function == 0x07) {
                regs_[2] = 0;
            } else if (function == 0x08) {
                regs_[2] = 0;
            } else if (function == 0x09) {
                regs_[2] = 0;
            } else if (function == 0x0A) {
                std::uint32_t handle = 0;
                for (; handle < bios_events.size(); ++handle) {
                    if (bios_events[handle] == 0) {
                        bios_events[handle] = 1;
                        bios_event_classes[handle] = regs_[4];
                        bios_event_specs[handle] = regs_[5];
                        bios_event_modes[handle] = regs_[6];
                        bios_event_flags[handle] = 0x1000;
                        break;
                    }
                }
                regs_[2] = handle < bios_events.size() ? 0x00000100u | handle : 0xFFFFFFFF;
            } else if (function == 0x0B) {
                std::uint32_t handle = regs_[4] & 0xFu;
                bios_events[handle] = 0;
                bios_event_flags[handle] = 0;
                regs_[2] = 1;
            } else if (function == 0x0C) {
                std::uint32_t handle = regs_[4] & 0xFu;
                if (bios_event_flags[handle] != 0) {
                    bios_event_flags[handle] = 0x2000;
                    bios_events[handle] |= 2;
                    regs_[2] = 1;
                } else {
                    regs_[2] = 0;
                }
            } else if (function == 0x0D) {
                std::uint32_t handle = regs_[4] & 0xFu;
                if (bios_event_flags[handle] != 0) {
                    bios_event_flags[handle] = 0x1000;
                    bios_events[handle] &= ~2u;
                    regs_[2] = 1;
                } else {
                    regs_[2] = 0;
                }
            } else if (function == 0x0F) {
                std::uint32_t handle = regs_[4] & 0xFu;
                regs_[2] = (bios_event_flags[handle] == 0x4000)
                    ? ([&]() { bios_event_flags[handle] = 0x2000; return 1u; }())
                    : 0xFFFFFFFFu;
            } else if (function == 0x10) {
                std::uint32_t handle = regs_[4] & 0xFu;
                if (bios_event_flags[handle] == 0x4000) {
                    bios_event_flags[handle] = 0x2000;
                    regs_[2] = 1;
                } else {
                    regs_[2] = 0;
                }
            } else if (function == 0x11) {
                std::uint32_t handle = regs_[4] & 0xFu;
                bios_event_flags[handle] |= 0x2000;
                regs_[2] = 1;
            } else if (function == 0x12) {
                std::uint32_t handle = regs_[4] & 0xFu;
                bios_event_flags[handle] &= ~0x2000u;
                regs_[2] = 1;
            } else if (function == 0x3A || function == 0x3B) {
                ++kernel_frame_counter;
                regs_[2] = kernel_frame_counter;
                if (timer_count[1] >= 0xFFFF) {
                    timer_count[1] = 0;
                } else {
                    ++timer_count[1];
                }
            } else if (function == 0x18) {
                regs_[2] = 0;
            } else if (function == 0x19) {
                const std::uint32_t address = regs_[4] & 0x1FFFFFFF;
                const std::uint32_t increment = regs_[5];
                if (in_ram(address) && in_ram(address + 3)) {
                    write_ram32(address, read_ram32(address) + increment);
                }
                regs_[2] = regs_[4];
            } else if (function == 0x35 || function == 0x5B || function == 0x72) {
                ++kernel_frame_counter;
                regs_[2] = kernel_frame_counter;
            } else {
                regs_[2] = 0;
            }
            pc_ = regs_[31];
            return true;
        }

        if (table_pc == 0xC0 || cached_c0) {
            std::uint32_t function = regs_[9];
            if (function == 8) {
                std::uint32_t destination = regs_[4] & 0x1FFFFFFF;
                std::uint32_t size = regs_[5];
                if (destination + size <= ram_.size()) {
                    std::memset(ram_.data() + destination, 0, size);
                }
                regs_[2] = 0;
            } else if (function == 10) {
                regs_[2] = kernel_frame_counter;
                ++timer_count[1];
                if (timer_count[1] >= 0x10000) {
                    timer_count[1] = 0;
                }
                pc_ = regs_[31];
                return true;
            } else if (function == 0xA) {
                std::uint32_t destination = regs_[4] & 0x1FFFFFFF;
                std::uint32_t source = regs_[5] & 0x1FFFFFFF;
                std::uint32_t size = regs_[6];
                if (destination + size <= ram_.size() && source + size <= ram_.size()) {
                    std::memmove(ram_.data() + destination, ram_.data() + source, size);
                    regs_[2] = destination | 0x80000000;
                } else {
                    regs_[2] = 0;
                }
            } else if (function == 12) {
                std::uint32_t destination = regs_[4] & 0x1FFFFFFF;
                std::uint32_t source = regs_[5] & 0x1FFFFFFF;
                std::uint32_t size = regs_[6];
                if (destination + size <= ram_.size() && source + size <= ram_.size()) {
                    std::memmove(ram_.data() + destination, ram_.data() + source, size);
                }
                regs_[2] = destination | 0x80000000;
            } else {
                regs_[2] = 0;
            }
            pc_ = regs_[31];
            return true;
        }

        return false;
    }

    bool in_ram(std::uint32_t address) const
    {
        if ((address & 0xFF000000u) == 0x1F000000u) {
            return false;
        }
        const std::uint32_t physical = address & 0x1FFFFFFF;
        return physical < ram_.size();
    }

    std::uint32_t read_ram32(std::uint32_t address) const
    {
        address &= 0x1FFFFFFF;
        return static_cast<std::uint32_t>(ram_[address])
            | (static_cast<std::uint32_t>(ram_[address + 1]) << 8)
            | (static_cast<std::uint32_t>(ram_[address + 2]) << 16)
            | (static_cast<std::uint32_t>(ram_[address + 3]) << 24);
    }

    void write_ram32(std::uint32_t address, std::uint32_t value)
    {
        address &= 0x1FFFFFFF;
        ram_[address] = static_cast<unsigned char>(value);
        ram_[address + 1] = static_cast<unsigned char>(value >> 8);
        ram_[address + 2] = static_cast<unsigned char>(value >> 16);
        ram_[address + 3] = static_cast<unsigned char>(value >> 24);
    }

    bool load_left(std::uint32_t address, std::uint32_t reg)
    {
        const std::uint32_t aligned = address & ~3u;
        const std::uint32_t mem = read_ram32(aligned);
        std::uint32_t value = regs_[reg];
        switch (address & 3) {
        case 0: value = (value & 0x00FFFFFFu) | (mem << 24); break;
        case 1: value = (value & 0x0000FFFFu) | (mem << 16); break;
        case 2: value = (value & 0x000000FFu) | (mem << 8); break;
        case 3: value = mem; break;
        }
        pending_load_register_ = reg;
        pending_load_value_ = value;
        pending_load_valid_ = true;
        return true;
    }

    bool load_right(std::uint32_t address, std::uint32_t reg)
    {
        const std::uint32_t aligned = address & ~3u;
        const std::uint32_t mem = read_ram32(aligned);
        std::uint32_t value = regs_[reg];
        switch (address & 3) {
        case 0: value = mem; break;
        case 1: value = (value & 0xFF000000u) | (mem >> 8); break;
        case 2: value = (value & 0xFFFF0000u) | (mem >> 16); break;
        case 3: value = (value & 0xFFFFFF00u) | (mem >> 24); break;
        }
        pending_load_register_ = reg;
        pending_load_value_ = value;
        pending_load_valid_ = true;
        return true;
    }

    bool store_left(std::uint32_t address, std::uint32_t value)
    {
        const std::uint32_t aligned = address & ~3u;
        if (!in_ram(aligned) || !in_ram(aligned + 3)) {
            return io_store(address, value, 1);
        }
        switch (address & 3) {
        case 0: ram_[aligned] = static_cast<unsigned char>(value >> 24); break;
        case 1:
            ram_[aligned] = static_cast<unsigned char>(value >> 24);
            ram_[aligned + 1] = static_cast<unsigned char>(value >> 16);
            break;
        case 2:
            ram_[aligned] = static_cast<unsigned char>(value >> 24);
            ram_[aligned + 1] = static_cast<unsigned char>(value >> 16);
            ram_[aligned + 2] = static_cast<unsigned char>(value >> 8);
            break;
        case 3:
            write_ram32(aligned, value);
            break;
        }
        return true;
    }

    bool store_right(std::uint32_t address, std::uint32_t value)
    {
        const std::uint32_t aligned = address & ~3u;
        if (!in_ram(aligned) || !in_ram(aligned + 3)) {
            return io_store(address, value, 1);
        }
        switch (address & 3) {
        case 0:
            write_ram32(aligned, value);
            break;
        case 1:
            ram_[aligned + 1] = static_cast<unsigned char>(value);
            ram_[aligned + 2] = static_cast<unsigned char>(value >> 8);
            ram_[aligned + 3] = static_cast<unsigned char>(value >> 16);
            break;
        case 2:
            ram_[aligned + 2] = static_cast<unsigned char>(value);
            ram_[aligned + 3] = static_cast<unsigned char>(value >> 8);
            break;
        case 3:
            ram_[aligned + 3] = static_cast<unsigned char>(value);
            break;
        }
        return true;
    }

    bool load_byte(std::uint32_t address, std::uint32_t reg, bool unsigned_value)
    {
        if (in_ram(address)) {
            std::uint32_t value = ram_[address & 0x1FFFFFFF];
            pending_load_register_ = reg;
            pending_load_value_ = unsigned_value ? value : (value & 0x80 ? 0xFFFFFF00 | value : value);
            pending_load_valid_ = true;
            return true;
        }
        std::uint32_t value = 0;
        if (io_load(address, 1, value)) {
            pending_load_register_ = reg;
            pending_load_value_ = unsigned_value ? value : (value & 0x80 ? 0xFFFFFF00 | value : value);
            pending_load_valid_ = true;
            return true;
        }
        unsupported_ = "read from unsupported PS1 memory";
        return false;
    }

    bool load_half(std::uint32_t address, std::uint32_t reg, bool unsigned_value)
    {
        if (address & 1) {
            raise_exception(4);
            return true;
        }
        if (in_ram(address) && in_ram(address + 1)) {
            std::uint32_t value = ram_[address & 0x1FFFFFFF]
                | (static_cast<std::uint32_t>(ram_[(address & 0x1FFFFFFF) + 1]) << 8);
            pending_load_register_ = reg;
            pending_load_value_ = unsigned_value ? value : (value & 0x8000 ? 0xFFFF0000 | value : value);
            pending_load_valid_ = true;
            return true;
        }
        std::uint32_t value = 0;
        if (io_load(address, 2, value)) {
            pending_load_register_ = reg;
            pending_load_value_ = unsigned_value ? value : (value & 0x8000 ? 0xFFFF0000 | value : value);
            pending_load_valid_ = true;
            return true;
        }
        unsupported_ = "read from unsupported PS1 memory";
        return false;
    }

    bool load_word(std::uint32_t address, std::uint32_t reg)
    {
        if (address & 3) {
            raise_exception(4);
            return true;
        }
        if (in_ram(address) && in_ram(address + 3)) {
            pending_load_register_ = reg;
            pending_load_value_ = read_ram32(address);
            pending_load_valid_ = true;
            return true;
        }
        std::uint32_t value = 0;
        if (io_load(address, 4, value)) {
            pending_load_register_ = reg;
            pending_load_value_ = value;
            pending_load_valid_ = true;
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
            raise_exception(5);
            return true;
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
            raise_exception(5);
            return true;
        }
        const std::uint32_t original_address = address;
        const std::uint32_t ram_address = address & 0x1FFFFFFF;
        if (in_ram(ram_address) && in_ram(ram_address + 3)) {
            ram_[ram_address] = static_cast<unsigned char>(value);
            ram_[ram_address + 1] = static_cast<unsigned char>(value >> 8);
            ram_[ram_address + 2] = static_cast<unsigned char>(value >> 16);
            ram_[ram_address + 3] = static_cast<unsigned char>(value >> 24);
            return true;
        }
        return io_store(original_address, value, 4);
    }

